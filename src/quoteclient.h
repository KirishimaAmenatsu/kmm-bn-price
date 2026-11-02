// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "quotes.h"
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace Binance {
class QuoteClient : public QObject {
    Q_OBJECT
public:
    struct Limits { int requestMs = 15000; int totalMs = 90000; qint64 bodyBytes = 16 * 1024 * 1024; };
    explicit QuoteClient(QObject* parent = nullptr, QNetworkAccessManager* manager = nullptr);
    ~QuoteClient() override { cancel(); }
    void start(const QVector<Selection>& selections);
    void cancel();
    bool busy() const { return m_busy; }
    void setLimits(Limits limits); // Before start; useful for deterministic offline tests.
    QDateTime retryAfter(const QString& host) const { return m_cooldowns.value(host); }
Q_SIGNALS:
    void succeeded(const QVector<Binance::Quote>& quotes);
    void failed(const QString& error);
private:
    void request(const QUrl& url, std::function<void(const QByteArray&)> done);
    void fail(const QString& message);
    void nextTickers();
    void requestFx();
    void finish();
    QNetworkAccessManager* m_manager;
    QPointer<QNetworkReply> m_reply;
    QTimer m_totalTimer;
    QTimer m_requestTimer;
    Limits m_limits;
    bool m_busy = false;
    quint64 m_epoch = 0;
    QMap<QString, QDateTime> m_cooldowns;
    QVector<Selection> m_selections;
    QVector<Plan> m_plans;
    QStringList m_pending;
    Prices m_prices;
    FxRates m_fx;
    QDate m_spotDate;
};
}
