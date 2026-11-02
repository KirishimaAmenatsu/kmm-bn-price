// SPDX-License-Identifier: GPL-2.0-or-later
#include <QApplication>
#include "fakehost.h"
#include <QDialog>
#include <QFile>
#include <QDir>
#include <QMenu>
#include <QPluginLoader>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QTableWidget>
#include <kmymoneyplugin.h>
#include <mymoneyprice.h>
#include <KActionCollection>
#include <KPluginFactory>
#include <KPluginMetaData>
#include <KXmlGuiWindow>
#include <KXMLGUIFactory>
#include <QTextStream>
#include <memory>
class TestWindow : public KXmlGuiWindow {
public:
    using KXMLGUIClient::setXML;
    using KXMLGUIClient::setComponentName;
};
int main(int argc, char** argv)
{
    QTemporaryDir config(QDir::currentPath() + "/plugin-config-XXXXXX");
    if (!config.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QApplication app(argc, argv);
    app.setApplicationName("kmm-binance-plugin-test");
    const auto args = app.arguments();
    QTemporaryDir stage(QDir::currentPath() + "/discovery-stage-XXXXXX");
    QString path;
    if (args.size() == 5 && args[1] == "--discover-install") {
        // DESTDIR also confines a user-configured absolute install destination;
        // a test must never install into the actual system or home prefix.
        if (!stage.isValid() || QProcess::execute(args[4], {"-E", "env", "DESTDIR=" + stage.path(), args[4], "--install", args[2], "--prefix", "/kmm-binance-test"}) != 0) return 1;
        const auto installedDir = QDir::isAbsolutePath(args[3]) ? args[3] : "/kmm-binance-test/" + args[3];
        const auto root = QFileInfo(stage.path() + installedDir).absolutePath();
        QCoreApplication::setLibraryPaths({root});
        // Same namespace and discovery API as KMyMoney v5.2.2 pluginloader.cpp.
        for (const auto& candidate : KPluginMetaData::findPlugins(QStringLiteral("kmymoney_plugins")))
            if (candidate.pluginId() == "kmm_binance_price") { path = candidate.fileName(); break; }
        if (path.isEmpty() || QFileInfo(path).canonicalFilePath() != QFileInfo(root + "/kmymoney_plugins/kmm_binance_price.so").canonicalFilePath()) {
            QTextStream(stderr) << "Installed plugin was NOT discovered in host namespace kmymoney_plugins\n";
            return 1;
        }
    } else if (args.size() == 3 && args[1] == "--discover-root") {
        QCoreApplication::setLibraryPaths({args[2]});
        for (const auto& candidate : KPluginMetaData::findPlugins(QStringLiteral("kmymoney_plugins")))
            if (candidate.pluginId() == "kmm_binance_price") { path = candidate.fileName(); break; }
        if (path.isEmpty() || QFileInfo(path).canonicalFilePath() != QFileInfo(args[2] + "/kmymoney_plugins/kmm_binance_price.so").canonicalFilePath()) return 1;
    } else if (args.size() == 2) path = args[1];
    else return 2;
    const KPluginMetaData metadata(path);
    if (!metadata.isValid() || metadata.pluginId() != "kmm_binance_price" || metadata.isEnabledByDefault()) return 1;
    QPluginLoader loader(path);
    if (!loader.load() || !qobject_cast<KPluginFactory*>(loader.instance())) {
        QTextStream(stderr) << loader.errorString() << '\n';
        return 1;
    }
    QFile resource(":/binanceprice.rc");
    if (!resource.open(QIODevice::ReadOnly) || !resource.readAll().contains("binance_configure")) return 1;
    FakeAppInterface host;
    HostInterfaceScope interfaces(&host);
    InMemoryLedger ledger;
    const auto result = KPluginFactory::instantiatePlugin<KMyMoneyPlugin::Plugin>(metadata);
    if (!result) { QTextStream(stderr) << result.errorString << '\n'; return 1; }
    std::unique_ptr<KMyMoneyPlugin::Plugin> plugin(result.plugin);
    // Real plugin + fake AppInterface + real in-memory engine. This is not a
    // running KMyMoney host and never opens a ledger file or starts a quote job.
    TestWindow window;
    window.setComponentName("kmm-binance-host-test", "Test");
    window.setXML("<gui name=\"test\" version=\"1\"><MenuBar><Menu name=\"kmmtools\"><text>Tools</text></Menu></MenuBar></gui>");
    window.guiFactory()->addClient(&window);
    window.guiFactory()->addClient(plugin.get());
    auto menu = qobject_cast<QMenu*>(window.guiFactory()->container("kmmtools", &window));
    auto action = plugin->actionCollection()->action("binance_configure");
    if (!menu || !action || !menu->actions().contains(action) || action->isEnabled()) return 1;

    plugin->plug(window.guiFactory());
    if (action->isEnabled()) return 1; // No open ledger.
    host.opened = true;
    const auto uuid = QUuid::createUuid();
    ledger.open(uuid);
    if (!action->isEnabled()) return 1;
    MyMoneyFile::instance()->unload(); // Authoritative model reset, no old signal.
    if (action->isEnabled()) return 1;
    ledger.open(uuid); // Same UUID, distinct opening session.
    if (!action->isEnabled()) return 1;
    host.kmmFilePlugin(2); // v5.2.2 FileAction::Closing.
    if (action->isEnabled()) return 1;
    host.kmmFilePlugin(0); // Opened, supplementary legacy hook.
    if (!action->isEnabled()) return 1;
    plugin->unplug();
    if (action->isEnabled()) return 1;
    host.kmmFilePlugin(0);
    MyMoneyFile::instance()->modelsReadyToUse();
    if (action->isEnabled()) return 1; // Unplug disconnected lifecycle hooks.

    plugin->plug(window.guiFactory());
    if (!action->isEnabled()) return 1;
    action->trigger();
    QPointer<QDialog> dialog;
    for (auto widget : QApplication::topLevelWidgets())
        if (widget->windowTitle() == QStringLiteral("币安证券报价 — 当前账本")) dialog = qobject_cast<QDialog*>(widget);
    if (!dialog || !dialog->isVisible()) return 1;
    auto table = dialog->findChild<QTableWidget*>();
    if (!table || table->rowCount() != 1 || table->item(0, 0)->checkState() != Qt::Unchecked) return 1;
    plugin->unplug(); // Also rejects a live configuration dialog safely.
    if (action->isEnabled() || dialog->isVisible() || dialog->result() != QDialog::Rejected) return 1;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (dialog || !MyMoneyFile::instance()->priceList().isEmpty() || MyMoneyFile::instance()->dirty()) return 1;
    window.guiFactory()->removeClient(plugin.get());
    window.guiFactory()->removeClient(&window);
    QTextStream(stdout) << "Native Qt6/KF6 metadata/XMLGUI verified; plug/unplug, close/reopen and dialog cleanup verified with fake AppInterface + in-memory engine only (not the real host UI).\n";
    return 0;
}
