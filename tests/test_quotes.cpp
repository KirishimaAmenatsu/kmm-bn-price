// SPDX-License-Identifier: GPL-2.0-or-later
#include "quotes.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>
using namespace Binance;

static QByteArray marketJson(QString status = "TRADING", bool spot = true)
{
    return QJsonDocument(QJsonObject{{"symbols", QJsonArray{QJsonObject{{"symbol", "BTCUSDT"}, {"baseAsset", "BTC"}, {"quoteAsset", "USDT"}, {"status", status}, {"isSpotTradingAllowed", spot}}}}}).toJson();
}
static Markets bridgeMarkets() { return {{"BTCUSDT", "BTC", "USDT"}, {"EURUSDT", "EUR", "USDT"}}; }
static QByteArray fxBody(const QString& date, const QByteArray& rate = "7.7")
{
    return "[{\"date\":\"" + date.toUtf8() + "\",\"base\":\"EUR\",\"quote\":\"CNY\",\"rate\":" + rate + "}]";
}
class QuoteTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void directAndInverse()
    {
        for (bool inverse : {false, true}) {
            Markets markets = inverse ? Markets{{"EURBTC", "EUR", "BTC"}} : Markets{{"BTCEUR", "BTC", "EUR"}};
            const auto plans = makePlans(markets, {{"S1", "BTC", "EUR"}});
            const auto prices = Prices{{inverse ? "EURBTC" : "BTCEUR", decimal(inverse ? "0.00002" : "50000")}};
            const auto quotes = calculate(plans, prices, {}, QDate(2026, 11, 2));
            QCOMPARE(quotes.size(), 1);
            QCOMPARE(quotes[0].rate, decimal("50000"));
            QVERIFY(quotes[0].source.contains("no FX"));
        }
    }
    void bridgeAndMultipleFiats()
    {
        const auto plans = makePlans(bridgeMarkets(), {{"S1", "BTC", "CNY"}, {"S2", "BTC", "USD"}, {"S3", "BTC", "EUR"}});
        QCOMPARE(symbols(plans), QStringList({"BTCUSDT", "EURUSDT"}));
        const auto date = QDate(2026, 11, 2);
        auto q = calculate(plans, {{"BTCUSDT", decimal("62500")}, {"EURUSDT", decimal("1.25")}},
            {{"CNY", {decimal("7.7"), date.addDays(-3)}}, {"USD", {decimal("1.1"), date}}}, date);
        QCOMPARE(q[0].rate, decimal("385000"));
        QCOMPARE(q[1].rate, decimal("55000"));
        QCOMPARE(q[2].rate, decimal("50000"));
        QVERIFY(q[0].source.contains("FX 2026-10-30"));
        QVERIFY(q[0].source.contains("1/EURUSDT"));
    }
    void stablecoinsAreNotDollars()
    {
        auto markets = bridgeMarkets();
        markets << Market{"USDCUSDT", "USDC", "USDT"};
        const auto plans = makePlans(markets, {{"S1", "USDT", "USD"}, {"S2", "USDC", "USD"}});
        const auto date = QDate(2026, 11, 2);
        auto q = calculate(plans, {{"EURUSDT", decimal("1.25")}, {"USDCUSDT", decimal("0.99")}}, {{"USD", {decimal("1.1"), date}}}, date);
        QCOMPARE(q[0].rate, decimal("0.88"));
        QCOMPARE(q[1].rate, decimal("0.8712"));
        QVERIFY(q[0].rate != MyMoneyMoney::ONE);
    }
    void alternativeBridgeAndPriority()
    {
        Markets markets{{"ETHBTC", "ETH", "BTC"}, {"BTCEUR", "BTC", "EUR"}};
        auto plans = makePlans(markets, {{"S1", "ETH", "EUR"}});
        QCOMPARE(symbols(plans), QStringList({"BTCEUR", "ETHBTC"}));
        QCOMPARE(calculate(plans, {{"ETHBTC", decimal("0.04")}, {"BTCEUR", decimal("50000")}}, {}, QDate(2026, 11, 2))[0].rate, decimal("2000"));
        markets << Market{"ETHEUR", "ETH", "EUR"};
        plans = makePlans(markets, {{"S1", "ETH", "EUR"}});
        QCOMPARE(symbols(plans), QStringList({"ETHEUR"}));
    }
    void exactDecimals()
    {
        QCOMPARE(decimal("0.100000000000000001"), MyMoneyMoney(QStringLiteral("100000000000000001/1000000000000000000")));
        QCOMPARE(decimal("12.5e-2"), MyMoneyMoney(QStringLiteral("1/8")));
        auto fx = parseFx(fxBody("2026-11-02", "7.123456789012345678901"), {"CNY"}, QDate(2026, 11, 2));
        QCOMPARE(fx["CNY"].rate, MyMoneyMoney(QStringLiteral("7123456789012345678901/1000000000000000000000")));
        const auto prices = parseTickers(R"([{"symbol":"BTCUSDT","price":"0.000000000000000001"}])", {"BTCUSDT"});
        QCOMPARE(prices["BTCUSDT"], decimal("1e-18"));
    }
    void rejectBadDecimal_data()
    {
        QTest::addColumn<QString>("value");
        for (auto value : {"0", "-1", "NaN", "nan", "inf", "Infinity", "1/2", " 1", "1,2", "+1", "01", "1.", ".1", "1e9999999999", "1e31", "0.000", "1<script>"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }
    void rejectBadDecimal()
    {
        QFETCH(QString, value);
        QVERIFY_EXCEPTION_THROWN(decimal(value), Error);
    }
    void rejectMalformedJson()
    {
        for (const QByteArray body : {QByteArray("oops"), QByteArray("{}garbage"), QByteArray("{\"rate\":NaN}"), QByteArray("{\"rate\":01}"), QByteArray("[1,]")})
            QVERIFY_EXCEPTION_THROWN(exactJson(body), Error);
        QVERIFY_EXCEPTION_THROWN(parseMarkets("{}"), Error);
        QVERIFY_EXCEPTION_THROWN(parseTickers("{}", {"BTCUSDT"}), Error);
        QVERIFY_EXCEPTION_THROWN(parseFx("{}", {"CNY"}, QDate(2026, 11, 2)), Error);
    }
    void activeMarketsOnly()
    {
        QVERIFY(validAsset(QStringLiteral("币安人生")));
        QVERIFY(!validAsset(QStringLiteral("BTC/USDT")));
        QCOMPARE(parseMarkets(marketJson()).size(), 1);
        QVERIFY(parseMarkets(marketJson("BREAK")).isEmpty());
        QVERIFY(parseMarkets(marketJson("TRADING", false)).isEmpty());
        QVERIFY_EXCEPTION_THROWN(makePlans(parseMarkets(marketJson("BREAK")), {{"S1", "BTC", "EUR"}}), Error);
        QVERIFY_EXCEPTION_THROWN(makePlans({}, {{"S1", "1000PEPE", "EUR"}}), Error);
        QVERIFY_EXCEPTION_THROWN(makePlans(bridgeMarkets(), {{"S1", "BTC", "USDT"}}), Error);
        QVERIFY_EXCEPTION_THROWN(makePlans(bridgeMarkets(), {{"S1", "BTC", "CNY"}, {"S1", "BTC", "USD"}}), Error);
    }
    void rejectTickers()
    {
        QVERIFY_EXCEPTION_THROWN(parseTickers("[]", {"BTCUSDT"}), Error);
        QVERIFY_EXCEPTION_THROWN(parseTickers(R"([{"symbol":"BTCUSDT","price":"0"}])", {"BTCUSDT"}), Error);
        QVERIFY_EXCEPTION_THROWN(parseTickers(R"([{"symbol":"BTCUSDT","price":"NaN"}])", {"BTCUSDT"}), Error);
        QVERIFY_EXCEPTION_THROWN(parseTickers(R"([{"symbol":"BTCUSDT","price":"1"},{"symbol":"BTCUSDT","price":"2"}])", {"BTCUSDT"}), Error);
        QVERIFY_EXCEPTION_THROWN(calculate(makePlans(bridgeMarkets(), {{"S1", "BTC", "EUR"}}), {{"BTCUSDT", decimal("100")}}, {}, QDate(2026, 11, 2)), Error);
    }
    void fxDatesAndMissingRates()
    {
        const auto today = QDate(2026, 11, 2);
        QCOMPARE(parseFx(fxBody("2026-10-30"), {"CNY"}, today)["CNY"].date, QDate(2026, 10, 30));
        QVERIFY_EXCEPTION_THROWN(parseFx(fxBody("2026-11-03"), {"CNY"}, today), Error);
        QVERIFY_EXCEPTION_THROWN(parseFx(fxBody("2026-10-25"), {"CNY"}, today), Error);
        QVERIFY_EXCEPTION_THROWN(parseFx(fxBody("2026-11-02", "0"), {"CNY"}, today), Error);
        QVERIFY_EXCEPTION_THROWN(parseFx(fxBody("2026-11-02", "-1"), {"CNY"}, today), Error);
        QVERIFY_EXCEPTION_THROWN(parseFx(fxBody("2026-11-02"), {"CNY", "USD"}, today), Error);
        QVERIFY_EXCEPTION_THROWN(parseFx("[]", {"CNY"}, today), Error);
    }
    void ledgerGenerations()
    {
        SessionGuard guard;
        guard.open("ledger-A");
        const auto a = guard.ticket();
        QVERIFY(guard.accepts(a, true, "ledger-A"));
        QVERIFY(!guard.accepts(a, false, "ledger-A"));
        QVERIFY(!guard.accepts(a, true, "ledger-B"));
        guard.invalidate();
        QVERIFY(!guard.accepts(a, true, "ledger-A"));
        guard.open("ledger-A");
        QVERIFY(!guard.accepts(a, true, "ledger-A")); // Same UUID, new open session.
        const auto reopened = guard.ticket();
        guard.open("ledger-A"); // Configuration change invalidates the ticket too.
        QVERIFY(!guard.accepts(reopened, true, "ledger-A"));
    }
};
QTEST_GUILESS_MAIN(QuoteTests)
#include "test_quotes.moc"
