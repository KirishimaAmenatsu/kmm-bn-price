// SPDX-License-Identifier: GPL-2.0-or-later
#include "quoteclient.h"
#include "ledgerwriter.h"
#include <kmymoneyplugin.h>
#include <appinterface.h>
#include <mymoneyfile.h>
#include <mymoneysecurity.h>
#include <mymoneyprice.h>
#include <mymoneyenums.h>
#include <securitiesmodel.h>
#include <KActionCollection>
#include <KConfigGroup>
#include <KPluginFactory>
#include <KSharedConfig>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QTableWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

using namespace Binance;

namespace {
// kmymoneyenums.h is not installed by Arch 5.2.2. Exact values from KDE's
// 5.2/kmymoney/kmymoneyenums.h; the signal is only a supplementary hook:
// 5.2 builds do not consistently emit it. Model reset + modelsReadyToUse
// below provide authoritative close/reopen invalidation even for the same UUID.
enum class FileAction : unsigned { Opened = 0, Saved, Closing, Closed, Changed, AboutToClose };
constexpr int Interval = 15 * 60 * 1000;
}

class BinancePricePlugin : public KMyMoneyPlugin::Plugin {
    Q_OBJECT
public:
    BinancePricePlugin(QObject* parent, const KPluginMetaData& metadata, const QVariantList& args)
        : Plugin(parent, metadata, args), m_client(this), m_config(KSharedConfig::openConfig(QStringLiteral("kmm-binance-pricerc")))
    {
        QFile xml(QStringLiteral(":/binanceprice.rc"));
        if (xml.open(QIODevice::ReadOnly)) setXML(QString::fromUtf8(xml.readAll()));
        m_configure = actionCollection()->addAction(QStringLiteral("binance_configure"));
        m_configure->setText(QStringLiteral("配置币安证券报价…"));
        m_update = actionCollection()->addAction(QStringLiteral("binance_update"));
        m_update->setText(QStringLiteral("更新币安证券报价"));
        auto status = actionCollection()->addAction(QStringLiteral("binance_status"));
        status->setText(QStringLiteral("币安报价状态…"));
        connect(m_configure, &QAction::triggered, this, &BinancePricePlugin::configure);
        connect(m_update, &QAction::triggered, this, [this] { update(true); });
        connect(status, &QAction::triggered, this, [this] { showMessage(m_status); });
        m_timer.setInterval(Interval);
        connect(&m_timer, &QTimer::timeout, this, [this] { update(false); });
        connect(&m_client, &QuoteClient::failed, this, [this](const QString& error) {
            setStatus(QStringLiteral("本批次未写入任何价格：%1").arg(error));
            updateActionsState();
            if (m_manual) showMessage(m_status);
        });
        connect(&m_client, &QuoteClient::succeeded, this, &BinancePricePlugin::apply);
        updateActionsState();
    }
    ~BinancePricePlugin() override { unplug(); }
    void plug(KXMLGUIFactory*) override
    {
        if (m_active) return;
        m_active = true;
        const auto file = MyMoneyFile::instance();
        connect(appInterface(), &KMyMoneyPlugin::AppInterface::kmmFilePlugin, this, [this](unsigned action) {
            switch (static_cast<FileAction>(action)) {
            case FileAction::Opened: ledgerReady(); break;
            case FileAction::Closing:
            case FileAction::Closed: invalidate(); break;
            default: break;
            }
        });
        connect(file, &MyMoneyFile::modelsReadyToUse, this, &BinancePricePlugin::ledgerReady);
        // MyMoneyFile::unload() resets this model synchronously before unloading
        // the ledger. This also covers builds where kmmFilePlugin is not emitted.
        connect(file->securitiesModel(), &QAbstractItemModel::modelAboutToBeReset, this, &BinancePricePlugin::invalidate);
        if (appInterface()->fileOpen()) ledgerReady();
        updateActionsState();
    }
    void unplug() override
    {
        if (!m_active) return;
        m_active = false;
        invalidate();
        disconnect(appInterface(), nullptr, this, nullptr);
        disconnect(MyMoneyFile::instance(), nullptr, this, nullptr);
        disconnect(MyMoneyFile::instance()->securitiesModel(), nullptr, this, nullptr);
    }
private:
    QString storage() const {
        const auto file = MyMoneyFile::instance();
        // storageId() creates a UUID when missing. Do not dirty an unconfigured
        // or unsaved ledger merely by loading this plugin. KMyMoney's XML writer
        // initializes the UUID on save (as do its normal old-file fixes).
        if (QUuid(file->value(QStringLiteral("kmm-id"))).isNull()) return {};
        return file->storageId().toString(QUuid::WithoutBraces);
    }
    KConfigGroup group(const QString& id) const { return m_config->group(QStringLiteral("Ledger-%1").arg(id)); }
    bool usable() const { return m_active && m_ready && appInterface()->fileOpen(); }
    bool current(const SessionGuard::Ticket& ticket) const {
        return usable() && m_guard.accepts(ticket, appInterface()->fileOpen(), storage());
    }
    void invalidate()
    {
        m_guard.invalidate();
        m_ready = false;
        m_timer.stop();
        m_client.cancel();
        if (m_dialog) m_dialog->reject();
        updateActionsState();
    }
    void ledgerReady()
    {
        invalidate();
        if (!m_active || !appInterface()->fileOpen()) return;
        if (storage().isEmpty()) {
            setStatus(QStringLiteral("账本尚无稳定 UUID。请先保存账本，再关闭并重新打开；插件不会自行修改账本元数据。"));
            return;
        }
        m_ready = true;
        m_guard.open(storage());
        setStatus(QStringLiteral("账本已打开；仅更新显式启用的股票证券。"));
        schedule(true);
        updateActionsState();
    }
    void schedule(bool immediate)
    {
        m_timer.stop();
        if (!usable()) return;
        const auto g = group(storage());
        if (!g.readEntry("Automatic", true) || g.readEntry("Enabled", QStringList()).isEmpty()) return;
        m_timer.start();
        if (immediate) {
            const auto ticket = m_guard.ticket();
            QTimer::singleShot(0, this, [this, ticket] { if (current(ticket)) update(false); });
        }
    }
    void updateActionsState()
    {
        const bool ready = usable();
        m_configure->setEnabled(ready && !m_dialog);
        m_update->setEnabled(ready && !m_client.busy() && !m_dialog);
    }
    void setStatus(const QString& value)
    {
        m_status = value;
        m_update->setToolTip(QStringLiteral("<qt><pre>%1</pre></qt>").arg(value.toHtmlEscaped()));
        qInfo().noquote() << "Binance securities:" << value;
    }
    void showMessage(const QString& text)
    {
        auto message = new QMessageBox(QMessageBox::Information, QStringLiteral("币安证券报价"), text, QMessageBox::Ok, QApplication::activeWindow());
        message->setTextFormat(Qt::PlainText);
        message->setAttribute(Qt::WA_DeleteOnClose);
        message->open();
    }
    void validate(const Selection& selection) const
    {
        const auto file = MyMoneyFile::instance();
        const auto security = file->security(selection.securityId);
        if (security.id() != selection.securityId || security.securityType() != eMyMoney::Security::Type::Stock
            || security.isCurrency() || security.tradingCurrency() != selection.fiat || !validAsset(selection.asset)
            || !supportedFiat(selection.fiat)) throw Error(QStringLiteral("证券或目标法币无效/已改变：%1").arg(selection.securityId));
        const auto fiat = file->currency(selection.fiat);
        if (fiat.id() != selection.fiat || !fiat.isCurrency()) throw Error(QStringLiteral("目标必须是账本中已存在的 ECB 法币"));
    }
    QVector<Selection> selections() const
    {
        QVector<Selection> result;
        const auto g = group(storage());
        for (const auto& id : g.readEntry("Enabled", QStringList())) {
            const auto security = MyMoneyFile::instance()->security(id);
            Selection selection{id, g.readEntry(QStringLiteral("Asset-%1").arg(id), QString()), security.tradingCurrency()};
            validate(selection);
            result.push_back(selection);
        }
        if (result.size() > 100) throw Error(QStringLiteral("每批次最多 100 只证券"));
        return result;
    }
    void update(bool manual)
    {
        if (!usable() || m_client.busy() || m_dialog) return;
        m_manual = manual;
        try {
            m_snapshot = selections();
            if (m_snapshot.isEmpty()) {
                setStatus(QStringLiteral("尚未启用证券，请先在工具菜单中配置。"));
                if (manual) configure();
                return;
            }
            m_ticket = m_guard.ticket();
            setStatus(QStringLiteral("正在异步获取现货和 ECB 参考汇率…"));
            m_client.start(m_snapshot);
            updateActionsState();
        } catch (const std::exception& e) {
            setStatus(QStringLiteral("未写入价格：%1").arg(QString::fromUtf8(e.what())));
            if (manual) showMessage(m_status);
        }
    }
    void apply(const QVector<Quote>& quotes)
    {
        if (!current(m_ticket)) { updateActionsState(); return; }
        try {
            if (selections() != m_snapshot || quotes.size() != m_snapshot.size()) throw Error(QStringLiteral("配置已改变，丢弃本批次"));
            // Revalidate every security before opening the transaction. No event
            // loop is entered between validation and commit.
            for (qsizetype i = 0; i < quotes.size(); ++i) {
                if (!(quotes[i].selection == m_snapshot[i]) || !quotes[i].rate.isPositive()) throw Error(QStringLiteral("报价与配置不匹配"));
                validate(quotes[i].selection);
            }
            if (!current(m_ticket)) throw Error(QStringLiteral("账本会话已改变"));
            writePricesAtomically(MyMoneyFile::instance(), quotes);
            QStringList references;
            for (const auto& quote : quotes) references << quote.selection.asset + '/' + quote.selection.fiat + ": " + quote.source;
            setStatus(QStringLiteral("%1 UTC：已原子更新 %2 只证券。保存账本以持久化。\n%3")
                .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)).arg(quotes.size()).arg(references.join('\n')));
            if (m_manual) showMessage(m_status);
        } catch (const std::exception& e) {
            setStatus(QStringLiteral("本批次未写入任何价格：%1").arg(QString::fromUtf8(e.what())));
            if (m_manual) showMessage(m_status);
        }
        updateActionsState();
    }
    void configure()
    {
        if (!usable() || m_dialog) return;
        const auto ticket = m_guard.ticket();
        const auto g = group(ticket.storage);
        const auto enabled = g.readEntry("Enabled", QStringList());
        auto dialog = new QDialog(QApplication::activeWindow());
        m_dialog = dialog;
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(QStringLiteral("币安证券报价 — 当前账本"));
        dialog->resize(780, 470);
        auto layout = new QVBoxLayout(dialog);
        auto note = new QLabel(QStringLiteral("只列出股票类型证券，默认全部不选。请仅启用加密资产。\n目标法币来自证券的交易货币；如需更改请在 KMyMoney 中编辑证券。\n现货 × ECB 工作日参考汇率不是 C2C/P2P 可成交报价；USDT 不视为 USD。"), dialog);
        note->setWordWrap(true);
        layout->addWidget(note);
        auto table = new QTableWidget(0, 4, dialog);
        table->setHorizontalHeaderLabels({QStringLiteral("启用"), QStringLiteral("证券"), QStringLiteral("Binance asset"), QStringLiteral("目标法币")});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        for (const auto& security : MyMoneyFile::instance()->securityList()) {
            if (security.securityType() != eMyMoney::Security::Type::Stock || security.isCurrency()) continue;
            const int row = table->rowCount();
            table->insertRow(row);
            auto check = new QTableWidgetItem;
            check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
            check->setCheckState(enabled.contains(security.id()) ? Qt::Checked : Qt::Unchecked);
            check->setData(Qt::UserRole, security.id());
            table->setItem(row, 0, check);
            auto name = new QTableWidgetItem(security.name());
            name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            table->setItem(row, 1, name);
            table->setItem(row, 2, new QTableWidgetItem(g.readEntry(QStringLiteral("Asset-%1").arg(security.id()), security.tradingSymbol().trimmed().toUpper())));
            auto fiat = new QTableWidgetItem(security.tradingCurrency());
            fiat->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            table->setItem(row, 3, fiat);
        }
        layout->addWidget(table);
        auto automatic = new QCheckBox(QStringLiteral("打开已配置账本时及每 15 分钟自动更新"), dialog);
        automatic->setChecked(g.readEntry("Automatic", true));
        layout->addWidget(automatic);
        auto immediate = new QCheckBox(QStringLiteral("保存配置后立即更新所选证券"), dialog);
        immediate->setChecked(true);
        layout->addWidget(immediate);
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        connect(dialog, &QDialog::accepted, this, [this, ticket, table, automatic, immediate] {
            if (!current(ticket)) return;
            try {
                QStringList enabledIds;
                QMap<QString, QString> assets;
                for (int row = 0; row < table->rowCount(); ++row) {
                    const auto id = table->item(row, 0)->data(Qt::UserRole).toString();
                    const auto asset = table->item(row, 2)->text().trimmed().toUpper();
                    if (table->item(row, 0)->checkState() == Qt::Checked) {
                        validate({id, asset, table->item(row, 3)->text()});
                        enabledIds << id;
                        assets.insert(id, asset);
                    }
                }
                if (enabledIds.size() > 100) throw Error(QStringLiteral("每批次最多 100 只证券"));
                auto g = group(ticket.storage);
                g.writeEntry("Enabled", enabledIds);
                g.writeEntry("Automatic", automatic->isChecked());
                for (auto it = assets.cbegin(); it != assets.cend(); ++it) g.writeEntry(QStringLiteral("Asset-%1").arg(it.key()), it.value());
                if (!g.sync()) throw Error(QStringLiteral("无法保存配置文件"));
                m_client.cancel();
                m_guard.open(ticket.storage); // All in-flight configuration tickets expire.
                setStatus(QStringLiteral("配置已保存；已显式启用 %1 只证券。自动更新：%2。")
                    .arg(enabledIds.size()).arg(automatic->isChecked() ? QStringLiteral("开") : QStringLiteral("关")));
                schedule(false);
                const bool updateNow = immediate->isChecked() && !enabledIds.isEmpty();
                const auto newTicket = m_guard.ticket();
                QTimer::singleShot(0, this, [this, updateNow, newTicket] {
                    if (!current(newTicket)) return;
                    updateActionsState();
                    if (updateNow) update(true);
                });
            } catch (const std::exception& e) { showMessage(QString::fromUtf8(e.what())); }
        });
        connect(dialog, &QDialog::finished, this, [this] { m_dialog.clear(); updateActionsState(); });
        dialog->open();
        updateActionsState();
    }
    QuoteClient m_client;
    QTimer m_timer;
    KSharedConfigPtr m_config;
    QAction* m_configure = nullptr;
    QAction* m_update = nullptr;
    QPointer<QDialog> m_dialog;
    bool m_active = false;
    bool m_ready = false;
    bool m_manual = false;
    SessionGuard m_guard;
    SessionGuard::Ticket m_ticket;
    QVector<Selection> m_snapshot;
    QString m_status = QStringLiteral("尚未更新；请打开账本并显式配置加密资产证券。 ");
};

K_PLUGIN_CLASS_WITH_JSON(BinancePricePlugin, "plugin.json")
#include "plugin.moc"
