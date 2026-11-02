// SPDX-License-Identifier: GPL-2.0-or-later
#include "quoteclient.h"
#include <QNetworkReply>
#include <QUrlQuery>
#include <QtTest>
#include <cstring>
using namespace Binance;

struct Response { QByteArray body; int status = 200; int delayMs = 0; QByteArray retry; qint64 declaredSize = -1; };
class FakeReply : public QNetworkReply {
public:
    FakeReply(const QNetworkRequest& request, Response response, QObject* parent) : QNetworkReply(parent), m_response(response)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(response.delayMs, this, [this] {
            if (isFinished()) return;
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, m_response.status);
            if (!m_response.retry.isEmpty()) setRawHeader("Retry-After", m_response.retry);
            if (m_response.declaredSize >= 0) setHeader(QNetworkRequest::ContentLengthHeader, m_response.declaredSize);
            Q_EMIT metaDataChanged();
            if (isFinished()) return;
            m_available = true;
            Q_EMIT readyRead();
            if (isFinished()) return;
            setFinished(true);
            Q_EMIT finished();
        });
    }
    void abort() override {
        if (isFinished()) return;
        setError(OperationCanceledError, "cancelled");
        setFinished(true);
        Q_EMIT finished();
    }
    qint64 bytesAvailable() const override { return (m_available ? m_response.body.size() - m_offset : 0) + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char* data, qint64 maxSize) override {
        if (!m_available) return 0;
        const auto count = qMin(maxSize, m_response.body.size() - m_offset);
        if (count <= 0) return -1;
        std::memcpy(data, m_response.body.constData() + m_offset, count);
        m_offset += count;
        return count;
    }
private:
    Response m_response;
    qint64 m_offset = 0;
    bool m_available = false;
};
class FakeManager : public QNetworkAccessManager {
public:
    QList<Response> responses;
    QList<QUrl> urls;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        urls << request.url();
        const auto response = responses.isEmpty() ? Response{"unexpected request", 500} : responses.takeFirst();
        return new FakeReply(request, response, this);
    }
};
static QByteArray markets()
{
    return R"({"symbols":[{"symbol":"BTCUSDT","baseAsset":"BTC","quoteAsset":"USDT","status":"TRADING","isSpotTradingAllowed":true},{"symbol":"EURUSDT","baseAsset":"EUR","quoteAsset":"USDT","status":"TRADING","isSpotTradingAllowed":true}]})";
}
static QByteArray tickers() { return R"([{"symbol":"BTCUSDT","price":"62500"},{"symbol":"EURUSDT","price":"1.25"}])"; }
class NetworkTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void successfulBatch()
    {
        FakeManager manager;
        const auto date = QDateTime::currentDateTimeUtc().date().toString(Qt::ISODate).toUtf8();
        manager.responses = {{markets()}, {tickers()}, {"[{\"date\":\"" + date + "\",\"base\":\"EUR\",\"quote\":\"CNY\",\"rate\":7.7}]"}};
        QuoteClient client(nullptr, &manager);
        QSignalSpy success(&client, &QuoteClient::succeeded), failure(&client, &QuoteClient::failed);
        client.start({{"S1", "BTC", "CNY"}, {"S2", "USDT", "EUR"}});
        client.start({{"ignored", "ETH", "USD"}}); // Reentrancy is ignored.
        QTRY_COMPARE(success.size(), 1);
        QCOMPARE(failure.size(), 0);
        QCOMPARE(manager.urls.size(), 3);
        QCOMPARE(QUrlQuery(manager.urls[1]).queryItemValue("symbols"), QStringLiteral("[\"BTCUSDT\",\"EURUSDT\"]"));
        const auto quotes = qvariant_cast<QVector<Quote>>(success[0][0]);
        QCOMPARE(quotes.size(), 2);
        QCOMPARE(quotes[0].rate, decimal("385000"));
        QCOMPARE(quotes[1].rate, decimal("0.8"));
        QVERIFY(!client.busy());
    }
    void partialBatchNeverSucceeds()
    {
        FakeManager manager;
        manager.responses = {{markets()}, {R"([{"symbol":"BTCUSDT","price":"62500"}])"}};
        QuoteClient client(nullptr, &manager);
        QSignalSpy success(&client, &QuoteClient::succeeded), failure(&client, &QuoteClient::failed);
        client.start({{"S1", "BTC", "EUR"}, {"S2", "USDT", "EUR"}});
        QTRY_COMPARE(failure.size(), 1);
        QCOMPARE(success.size(), 0);
        QCOMPARE(manager.urls.size(), 2);
    }
    void httpFailures_data()
    {
        QTest::addColumn<int>("status");
        for (int status : {301, 403, 418, 429, 500}) QTest::newRow(qPrintable(QString::number(status))) << status;
    }
    void httpFailures()
    {
        QFETCH(int, status);
        FakeManager manager;
        manager.responses = {{"{}", status, 0, "120"}};
        QuoteClient client(nullptr, &manager);
        QSignalSpy success(&client, &QuoteClient::succeeded), failure(&client, &QuoteClient::failed);
        client.start({{"S1", "BTC", "EUR"}});
        QTRY_COMPARE(failure.size(), 1);
        QCOMPARE(success.size(), 0);
        if (status == 429 || status == 418) {
            QVERIFY(client.retryAfter("data-api.binance.vision") > QDateTime::currentDateTimeUtc().addSecs(100));
            client.start({{"S1", "BTC", "EUR"}});
            QCOMPARE(failure.size(), 2);
            QCOMPARE(manager.urls.size(), 1); // No request during Retry-After.
        }
    }
    void timeoutAndTotalDeadline()
    {
        for (bool total : {false, true}) {
            FakeManager manager;
            manager.responses = {{markets(), 200, 1000}};
            QuoteClient client(nullptr, &manager);
            client.setLimits({total ? 1000 : 20, total ? 20 : 2000, 1024});
            QSignalSpy success(&client, &QuoteClient::succeeded), failure(&client, &QuoteClient::failed);
            client.start({{"S1", "BTC", "EUR"}});
            QTRY_COMPARE(failure.size(), 1);
            QCOMPARE(success.size(), 0);
            QVERIFY(!client.busy());
        }
    }
    void retryAfterHttpDateAndFallback()
    {
        const auto future = QDateTime::currentDateTimeUtc().addSecs(600);
        for (const QByteArray retry : {QLocale::c().toString(future, "ddd, dd MMM yyyy HH:mm:ss 'GMT'").toLatin1(), QByteArray("not-a-date"), QByteArray()}) {
            FakeManager manager;
            manager.responses = {{"{}", 429, 0, retry}};
            QuoteClient client(nullptr, &manager);
            QSignalSpy failure(&client, &QuoteClient::failed);
            client.start({{"S1", "BTC", "EUR"}});
            QTRY_COMPARE(failure.size(), 1);
            QVERIFY(client.retryAfter("data-api.binance.vision") > QDateTime::currentDateTimeUtc().addSecs(290));
        }
    }
    void cancelAndRestart()
    {
        FakeManager manager;
        manager.responses = {{markets(), 200, 1000}, {markets()}, {tickers()}};
        QuoteClient client(nullptr, &manager);
        QSignalSpy success(&client, &QuoteClient::succeeded), failure(&client, &QuoteClient::failed);
        client.start({{"S1", "BTC", "EUR"}});
        client.cancel();
        QVERIFY(!client.busy());
        client.start({{"S2", "BTC", "EUR"}});
        QTRY_COMPARE(success.size(), 1);
        QCOMPARE(qvariant_cast<QVector<Quote>>(success[0][0])[0].selection.securityId, QStringLiteral("S2"));
        QCOMPARE(failure.size(), 0);
    }
    void responseSizeLimits()
    {
        for (qint64 declared : {-1, 10000}) {
            FakeManager manager;
            manager.responses = {{QByteArray(1025, ' '), 200, 0, {}, declared}};
            QuoteClient client(nullptr, &manager);
            client.setLimits({1000, 2000, 1024});
            QSignalSpy success(&client, &QuoteClient::succeeded), failure(&client, &QuoteClient::failed);
            client.start({{"S1", "BTC", "EUR"}});
            QTRY_COMPARE(failure.size(), 1);
            QCOMPARE(success.size(), 0);
        }
    }
    void destructionCancels()
    {
        FakeManager manager;
        manager.responses = {{markets(), 200, 1000}};
        auto client = new QuoteClient(nullptr, &manager);
        client->start({{"S1", "BTC", "EUR"}});
        delete client;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(manager.findChildren<QNetworkReply*>().size(), 0);
    }
};
QTEST_GUILESS_MAIN(NetworkTests)
#include "test_network.moc"
