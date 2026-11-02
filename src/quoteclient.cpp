// SPDX-License-Identifier: GPL-2.0-or-later
#include "quoteclient.h"
#include <QJsonArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <memory>

namespace Binance {
QuoteClient::QuoteClient(QObject* parent, QNetworkAccessManager* manager)
    : QObject(parent), m_manager(manager ? manager : new QNetworkAccessManager(this))
{
    m_totalTimer.setSingleShot(true);
    m_requestTimer.setSingleShot(true);
    connect(&m_totalTimer, &QTimer::timeout, this, [this] { fail(QStringLiteral("Quote batch total deadline exceeded")); });
    connect(&m_requestTimer, &QTimer::timeout, this, [this] { fail(QStringLiteral("HTTPS request timed out")); });
}
void QuoteClient::setLimits(Limits limits)
{
    if (!m_busy) m_limits = limits;
}
void QuoteClient::cancel()
{
    ++m_epoch;
    m_busy = false;
    m_totalTimer.stop();
    m_requestTimer.stop();
    if (m_reply) {
        const auto reply = m_reply;
        m_reply.clear();
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}
void QuoteClient::fail(const QString& message)
{
    if (!m_busy) return;
    cancel();
    Q_EMIT failed(message);
}
void QuoteClient::start(const QVector<Selection>& selections)
{
    if (m_busy) return; // Never overlap two batches.
    m_busy = true;
    ++m_epoch;
    m_selections = selections;
    m_prices.clear();
    m_fx.clear();
    m_plans.clear();
    m_totalTimer.start(m_limits.totalMs);
    QUrl url(QStringLiteral("https://data-api.binance.vision/api/v3/exchangeInfo"));
    QUrlQuery query;
    query.addQueryItem("permissions", "SPOT");
    query.addQueryItem("showPermissionSets", "false");
    url.setQuery(query);
    request(url, [this](const QByteArray& body) {
        m_plans = makePlans(parseMarkets(body), m_selections);
        m_pending = symbols(m_plans);
        m_spotDate = QDateTime::currentDateTimeUtc().date();
        nextTickers();
    });
}
void QuoteClient::request(const QUrl& url, std::function<void(const QByteArray&)> done)
{
    if (!m_busy) return;
    if (url.scheme() != "https" || (url.host() != "data-api.binance.vision" && url.host() != "api.frankfurter.dev")) {
        fail(QStringLiteral("Refusing non-approved HTTPS endpoint"));
        return;
    }
    const auto now = QDateTime::currentDateTimeUtc();
    if (m_cooldowns.value(url.host()) > now) {
        fail(QStringLiteral("Rate limited by %1; retry after %2").arg(url.host(), m_cooldowns[url.host()].toString(Qt::ISODate)));
        return;
    }
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "KMyMoney-Binance-Price/0.1");
    // TLS errors are intentionally never ignored. Each request has a wall-clock
    // deadline, not only Qt's inactivity timeout.
    const auto epoch = m_epoch;
    auto reply = m_manager->get(req);
    m_reply = reply;
    reply->setReadBufferSize(m_limits.bodyBytes + 1);
    auto bytes = std::make_shared<QByteArray>();
    m_requestTimer.start(m_limits.requestMs);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, bytes, epoch] {
        if (!m_busy || epoch != m_epoch) return;
        bytes->append(reply->read(m_limits.bodyBytes + 1 - bytes->size()));
        if (bytes->size() > m_limits.bodyBytes) fail(QStringLiteral("HTTPS response exceeds size limit"));
    });
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply, epoch] {
        if (!m_busy || epoch != m_epoch) return;
        if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > m_limits.bodyBytes)
            fail(QStringLiteral("HTTPS Content-Length exceeds size limit"));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, epoch, url, done] {
        if (!m_busy || epoch != m_epoch) return;
        m_requestTimer.stop();
        m_reply.clear();
        reply->deleteLater();
        bytes->append(reply->read(m_limits.bodyBytes + 1 - bytes->size()));
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429 || status == 418) {
            const auto now = QDateTime::currentDateTimeUtc();
            const auto raw = reply->rawHeader("Retry-After");
            bool ok = false;
            const auto seconds = raw.toLongLong(&ok);
            auto until = ok && seconds >= 0 ? now.addSecs(seconds) : QDateTime::fromString(QString::fromLatin1(raw), Qt::RFC2822Date);
            if (!until.isValid() || until <= now) until = now.addSecs(300);
            m_cooldowns.insert(url.host(), until);
            fail(QStringLiteral("HTTP %1 from %2; retry after %3 (no automatic retry)").arg(status).arg(url.host(), until.toString(Qt::ISODate)));
            return;
        }
        if (bytes->size() > m_limits.bodyBytes) { fail(QStringLiteral("HTTPS response exceeds size limit")); return; }
        if (status != 200 || reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("HTTPS %1: HTTP %2, %3").arg(url.host()).arg(status).arg(reply->errorString()));
            return;
        }
        try { done(*bytes); }
        catch (const std::exception& e) { fail(QString::fromUtf8(e.what())); }
    });
}
void QuoteClient::nextTickers()
{
    if (m_pending.isEmpty()) { requestFx(); return; }
    const auto batch = m_pending.mid(0, 80);
    m_pending = m_pending.mid(batch.size());
    QUrl url(QStringLiteral("https://data-api.binance.vision/api/v3/ticker/price"));
    QUrlQuery query;
    // Query encoding is performed by QUrl/QNetworkRequest (including quotes).
    query.addQueryItem("symbols", QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(batch)).toJson(QJsonDocument::Compact)));
    url.setQuery(query);
    request(url, [this, batch](const QByteArray& body) {
        const auto prices = parseTickers(body, batch);
        for (auto it = prices.cbegin(); it != prices.cend(); ++it) m_prices.insert(it.key(), it.value());
        nextTickers();
    });
}
void QuoteClient::requestFx()
{
    QStringList fiats;
    for (const auto& s : m_selections) if (s.fiat != "EUR" && !fiats.contains(s.fiat)) fiats << s.fiat;
    fiats.sort();
    if (fiats.isEmpty()) { finish(); return; }
    QUrl url(QStringLiteral("https://api.frankfurter.dev/v2/providers/ecb/rates"));
    QUrlQuery query;
    query.addQueryItem("base", "EUR");
    query.addQueryItem("quotes", fiats.join(','));
    url.setQuery(query);
    request(url, [this, fiats](const QByteArray& body) {
        m_fx = parseFx(body, fiats, QDateTime::currentDateTimeUtc().date());
        finish();
    });
}
void QuoteClient::finish()
{
    // ticker/price has no exchange timestamp: record UTC retrieval date, and
    // reject the rare batch crossing UTC midnight rather than misdate a price.
    if (m_spotDate != QDateTime::currentDateTimeUtc().date()) throw Error(QStringLiteral("UTC date changed during batch; please retry"));
    const auto quotes = calculate(m_plans, m_prices, m_fx, m_spotDate);
    cancel();
    Q_EMIT succeeded(quotes);
}
}
