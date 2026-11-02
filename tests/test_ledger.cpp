// SPDX-License-Identifier: GPL-2.0-or-later
#include "ledgerwriter.h"
#include <mymoneyfile.h>
#include <mymoneyaccount.h>
#include <mymoneysecurity.h>
#include <mymoneyprice.h>
#include <mymoneyenums.h>
#include <securitiesmodel.h>
#include <QtTest>
#include <QUuid>
using namespace Binance;

class LedgerTests : public QObject {
    Q_OBJECT
    QString m_btc, m_eth;
    QDate m_date{2026, 11, 2};
    QVector<Quote> batch() const { return {{{m_btc, "BTC", "EUR"}, decimal("50000"), m_date, "test"}, {{m_eth, "ETH", "USD"}, decimal("2200"), m_date, "test"}}; }
private Q_SLOTS:
    void init()
    {
        auto file = MyMoneyFile::instance();
        file->unload();
        MyMoneyFileTransaction transaction;
        file->addCurrency(MyMoneySecurity("EUR", "Euro", "EUR"));
        file->addCurrency(MyMoneySecurity("USD", "US Dollar", "USD"));
        file->setBaseCurrency(file->currency("EUR"));
        file->setValue("kmm-id", QUuid::createUuid().toString());
        MyMoneySecurity btc, eth;
        btc.setName("Bitcoin security"); btc.setTradingSymbol("BTC"); btc.setTradingCurrency("EUR"); btc.setSecurityType(eMyMoney::Security::Type::Stock);
        eth.setName("Ethereum security"); eth.setTradingSymbol("ETH"); eth.setTradingCurrency("USD"); eth.setSecurityType(eMyMoney::Security::Type::Stock);
        file->addSecurity(btc); file->addSecurity(eth);
        m_btc = btc.id(); m_eth = eth.id();
        transaction.commit();
        file->setDirty(false);
    }
    void successfulPricesOnly()
    {
        auto file = MyMoneyFile::instance();
        const auto securities = file->securityList();
        const auto currencies = file->currencyList();
        const auto transactions = file->transactionCount();
        writePricesAtomically(file, batch());
        QCOMPARE(file->price(m_btc, "EUR", m_date, true).rate("EUR"), decimal("50000"));
        QCOMPARE(file->price(m_eth, "USD", m_date, true).rate("USD"), decimal("2200"));
        QCOMPARE(file->securityList(), securities);
        QCOMPARE(file->currencyList(), currencies);
        QCOMPARE(file->transactionCount(), transactions);
    }
    void unownedSecuritiesNotifyAfterIndependentCommit()
    {
        auto file = MyMoneyFile::instance();
        QSignalSpy changed(file, &MyMoneyFile::dataChanged);
        bool notifiedInsideTransaction = false;
        QObject context;
        connect(file, &MyMoneyFile::dataChanged, &context, [&] {
            notifiedInsideTransaction |= file->hasTransaction();
        });
        writePricesAtomically(file, batch());
        QVERIFY(file->dirty());
        QCOMPARE(changed.size(), 1);
        QVERIFY(!notifiedInsideTransaction);
        QVERIFY(!file->hasTransaction());
    }
    void refusesExistingTransactionBeforeAnyWrite()
    {
        auto file = MyMoneyFile::instance();
        const auto before = file->priceList();
        QSignalSpy changed(file, &MyMoneyFile::dataChanged);
        int writerCalls = 0;
        MyMoneyFileTransaction outer;
        QVERIFY_EXCEPTION_THROWN(writePricesAtomically(file, batch(), [&](const MyMoneyPrice& price) {
            ++writerCalls;
            file->addPrice(price);
        }), Error);
        QCOMPARE(writerCalls, 0);
        QCOMPARE(file->priceList(), before);
        QVERIFY(file->hasTransaction());
        outer.rollback();
        QCOMPARE(file->priceList(), before);
        QCOMPARE(changed.size(), 0);
    }
    void associatedAccountDoesNotDuplicateCommitNotification()
    {
        auto file = MyMoneyFile::instance();
        {
            MyMoneyFileTransaction transaction;
            auto asset = file->asset();
            MyMoneyAccount investment;
            investment.setName("Test investment");
            investment.setAccountType(eMyMoney::Account::Type::Investment);
            investment.setCurrencyId("EUR");
            file->addAccount(investment, asset);
            MyMoneyAccount stock;
            stock.setName("Test Bitcoin holding");
            stock.setAccountType(eMyMoney::Account::Type::Stock);
            stock.setCurrencyId(m_btc);
            file->addAccount(stock, investment);
            transaction.commit();
        }
        file->setDirty(false);
        QSignalSpy changed(file, &MyMoneyFile::dataChanged);
        writePricesAtomically(file, batch());
        QCOMPARE(changed.size(), 1);
        QVERIFY(!file->hasTransaction());
    }
    void compensatesPartialInsertion()
    {
        auto file = MyMoneyFile::instance();
        {
            MyMoneyFileTransaction transaction;
            file->addPrice(MyMoneyPrice(m_btc, "EUR", m_date, decimal("40000"), "old-source"));
            file->addPrice(MyMoneyPrice("USD", m_eth, m_date, decimal("0.0005"), "reverse-source"));
            transaction.commit();
        }
        const auto before = file->priceList();
        QSignalSpy changed(file, &MyMoneyFile::dataChanged);
        int calls = 0;
        QVERIFY_EXCEPTION_THROWN(writePricesAtomically(file, batch(), [&](const MyMoneyPrice& price) {
            file->addPrice(price);
            if (++calls == 2) throw Error("injected writer failure");
        }), Error);
        QCOMPARE(calls, 2);
        QCOMPARE(file->priceList(), before);
        QCOMPARE(file->price(m_btc, "EUR", m_date, true).source(), QStringLiteral("old-source"));
        QVERIFY(!file->hasTransaction());
        QCOMPARE(changed.size(), 0);
    }
    void rejectsInvalidBatchBeforeAnyWrite()
    {
        auto file = MyMoneyFile::instance();
        auto quotes = batch();
        quotes[1].rate = MyMoneyMoney();
        QVERIFY_EXCEPTION_THROWN(writePricesAtomically(file, quotes), Error);
        QVERIFY(file->priceList().isEmpty());
        QVERIFY(!file->hasTransaction());
    }
    void realEngineUnloadInvalidatesSameUuid()
    {
        auto file = MyMoneyFile::instance();
        const auto id = file->storageId().toString();
        SessionGuard guard;
        guard.open(id);
        const auto old = guard.ticket();
        QObject context;
        connect(file->securitiesModel(), &QAbstractItemModel::modelAboutToBeReset, &context, [&] { guard.invalidate(); });
        file->unload();
        guard.open(id);
        QVERIFY(!guard.accepts(old, true, id));
    }
    void cleanup() { MyMoneyFile::instance()->unload(); }
};
QTEST_MAIN(LedgerTests)
#include "test_ledger.moc"
