// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "upstream/interfaceloader.h"
#include <appinterface.h>
#include <mymoneyfile.h>
#include <mymoneysecurity.h>
#include <mymoneyenums.h>
#include <QUrl>
#include <QUuid>

// The test executable does not link the real KMyMoneyApp. This friend bridge
// installs/restores only the fake AppInterface in the real plugin registry.
class KMyMoneyApp {
public:
    static KMyMoneyPlugin::AppInterface* exchangeApp(KMyMoneyPlugin::AppInterface* app) {
        auto& interfaces = KMyMoneyPlugin::pluginInterfaces();
        const auto previous = interfaces.appInterface;
        interfaces.appInterface = app;
        return previous;
    }
};
class FakeAppInterface : public KMyMoneyPlugin::AppInterface {
public:
    FakeAppInterface() : AppInterface(nullptr) {}
    bool opened = false;
    bool fileOpen() override { return opened; }
    bool isDatabase() override { return false; }
    bool isNativeFile() override { return true; }
    QUrl filenameURL() const override { return {}; }
    void writeFilenameURL(const QUrl&) override {}
    QUrl lastOpenedURL() override { return {}; }
    void writeLastUsedFile(const QString&) override {}
    void slotFileOpenRecent(const QUrl&) override {}
    void addToRecentFiles(const QUrl&) override {}
    KMyMoneyAppCallback progressCallback() override { return nullptr; }
    void writeLastUsedDir(const QString&) override {}
    QString readLastUsedDir() const override { return {}; }
    void consistencyCheck(bool) override {}
};
class HostInterfaceScope {
public:
    explicit HostInterfaceScope(FakeAppInterface* app) : m_previous(KMyMoneyApp::exchangeApp(app)) {}
    ~HostInterfaceScope() { KMyMoneyApp::exchangeApp(m_previous); }
private:
    KMyMoneyPlugin::AppInterface* m_previous;
};
class InMemoryLedger {
public:
    InMemoryLedger() { MyMoneyFile::instance()->unload(); }
    ~InMemoryLedger() { MyMoneyFile::instance()->unload(); }
    void open(const QUuid& uuid) {
        auto file = MyMoneyFile::instance();
        file->unload();
        {
            MyMoneyFileTransaction transaction;
            file->addCurrency(MyMoneySecurity("EUR", "Euro", "EUR"));
            file->setBaseCurrency(file->currency("EUR"));
            file->setValue("kmm-id", uuid.toString());
            MyMoneySecurity stock;
            stock.setName("Test Bitcoin security");
            stock.setTradingSymbol("BTC");
            stock.setTradingCurrency("EUR");
            stock.setSecurityType(eMyMoney::Security::Type::Stock);
            file->addSecurity(stock);
            transaction.commit();
        }
        file->setDirty(false);
        file->modelsReadyToUse();
    }
};
