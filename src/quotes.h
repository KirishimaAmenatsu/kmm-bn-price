// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDate>
#include <QJsonDocument>
#include <QMap>
#include <QStringList>
#include <QVector>
#include <mymoneymoney.h>
#include <stdexcept>

namespace Binance {
class Error : public std::runtime_error {
public:
    explicit Error(const QString& message) : std::runtime_error(message.toStdString()) {}
};
struct Selection {
    QString securityId;
    QString asset;
    QString fiat;
    bool operator==(const Selection& other) const { return securityId == other.securityId && asset == other.asset && fiat == other.fiat; }
};
struct Market { QString symbol, base, quote; };
struct Leg { QString symbol; bool inverse = false; };
struct Plan { Selection selection; QVector<Leg> legs; };
struct FxRate { MyMoneyMoney rate; QDate date; };
struct Quote { Selection selection; MyMoneyMoney rate; QDate spotDate; QString source; };
using Markets = QVector<Market>;
using Prices = QMap<QString, MyMoneyMoney>;
using FxRates = QMap<QString, FxRate>;
bool validAsset(const QString& value);
bool supportedFiat(const QString& value);
MyMoneyMoney decimal(const QString& text);
QJsonDocument exactJson(const QByteArray& bytes);
Markets parseMarkets(const QByteArray& bytes);
QVector<Plan> makePlans(const Markets& markets, const QVector<Selection>& selections);
QStringList symbols(const QVector<Plan>& plans);
Prices parseTickers(const QByteArray& bytes, const QStringList& required);
FxRates parseFx(const QByteArray& bytes, const QStringList& required, QDate today);
QVector<Quote> calculate(const QVector<Plan>& plans, const Prices& prices, const FxRates& fx, QDate today);

// A UUID alone cannot distinguish closing and reopening the same ledger.
// Invalidation also covers configuration changes and plugin unload.
class SessionGuard {
public:
    struct Ticket { QString storage; quint64 generation; };
    void invalidate() { ++m_generation; m_storage.clear(); }
    void open(const QString& storage) { invalidate(); m_storage = storage; }
    Ticket ticket() const { return {m_storage, m_generation}; }
    bool accepts(const Ticket& ticket, bool fileOpen, const QString& currentStorage) const {
        return fileOpen && !m_storage.isEmpty() && ticket.generation == m_generation
            && ticket.storage == m_storage && ticket.storage == currentStorage;
    }
private:
    quint64 m_generation = 0;
    QString m_storage;
};
}
Q_DECLARE_METATYPE(Binance::Quote)
Q_DECLARE_METATYPE(QVector<Binance::Quote>)
