// SPDX-License-Identifier: GPL-2.0-or-later
#include "quotes.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <optional>

namespace Binance {
bool validAsset(const QString& value)
{
    // Binance also lists official non-Latin asset codes (e.g. 币安人生).
    // Accept Unicode uppercase/uncased letters and numbers, not separators.
    static const QRegularExpression re(QStringLiteral("^[\\p{Lu}\\p{Lo}\\p{N}]{1,20}$"));
    return re.match(value).hasMatch();
}
bool supportedFiat(const QString& value)
{
    // ECB reference currencies; crypto/stablecoins are deliberately not here.
    static const QStringList supported = QStringLiteral("EUR AUD BRL CAD CHF CNY CZK DKK GBP HKD HUF IDR ILS INR ISK JPY KRW MXN MYR NOK NZD PHP PLN RON SEK SGD THB TRY USD ZAR").split(' ');
    return supported.contains(value);
}
MyMoneyMoney decimal(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("^(0|[1-9][0-9]*)(?:\\.([0-9]+))?(?:[eE]([+-]?[0-9]+))?$"));
    const auto match = re.match(text);
    if (text.size() > 64 || !match.hasMatch())
        throw Error(QStringLiteral("Invalid positive decimal: %1").arg(text.left(64)));
    bool ok = true;
    const int exponent = match.captured(3).isEmpty() ? 0 : match.captured(3).toInt(&ok);
    if (!ok || qAbs(qint64(exponent)) > 30 || match.captured(1).size() + match.captured(2).size() > 40)
        throw Error(QStringLiteral("Decimal exceeds precision limits"));
    QString numerator = match.captured(1) + match.captured(2);
    // GMP's string constructor can auto-detect a leading-zero integer as
    // octal. Normalize decimal digits before passing the rational to Alkimia.
    while (numerator.startsWith('0')) numerator.remove(0, 1);
    if (numerator.isEmpty()) throw Error(QStringLiteral("Zero price/rate"));
    int scale = match.captured(2).size() - exponent;
    QString denominator = QStringLiteral("1");
    if (scale < 0)
        numerator += QString(-scale, '0');
    else
        denominator += QString(scale, '0');
    const MyMoneyMoney value(numerator + '/' + denominator);
    if (!value.isPositive())
        throw Error(QStringLiteral("Zero or negative price/rate"));
    return value;
}

QJsonDocument exactJson(const QByteArray& bytes)
{
    // Qt's JSON DOM stores floating-point numbers as double. Validate the original
    // JSON first, then quote numeric lexemes outside strings before building the
    // DOM used for rates. Arithmetic therefore receives the exact wire decimal.
    QJsonParseError error;
    QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError)
        throw Error(QStringLiteral("Malformed JSON: %1").arg(error.errorString()));
    QByteArray exact;
    exact.reserve(bytes.size());
    bool inString = false, escaped = false;
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        const char c = bytes[i];
        if (inString) {
            exact += c;
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
        } else if (c == '"') {
            inString = true;
            exact += c;
        } else if ((c >= '0' && c <= '9') || c == '-') {
            exact += '"';
            while (i < bytes.size() && QByteArray("0123456789.eE+-").contains(bytes[i])) exact += bytes[i++];
            exact += '"';
            --i;
        } else exact += c;
    }
    const auto result = QJsonDocument::fromJson(exact, &error);
    if (error.error != QJsonParseError::NoError || result.isNull())
        throw Error(QStringLiteral("Invalid JSON document"));
    return result;
}
Markets parseMarkets(const QByteArray& bytes)
{
    const auto doc = exactJson(bytes);
    if (!doc.isObject() || !doc.object().value("symbols").isArray())
        throw Error(QStringLiteral("exchangeInfo has no symbols array"));
    Markets markets;
    QSet<QString> seen;
    for (const auto& item : doc.object().value("symbols").toArray()) {
        if (!item.isObject()) throw Error(QStringLiteral("Invalid market entry"));
        const auto o = item.toObject();
        if (o.value("status").toString() != "TRADING" || !o.value("isSpotTradingAllowed").toBool()) continue;
        Market market{o.value("symbol").toString(), o.value("baseAsset").toString(), o.value("quoteAsset").toString()};
        if (!validAsset(market.base) || !validAsset(market.quote) || market.symbol != market.base + market.quote || seen.contains(market.symbol))
            throw Error(QStringLiteral("Invalid or duplicate market"));
        seen.insert(market.symbol);
        markets.push_back(market);
    }
    return markets;
}
static std::optional<Leg> pair(const Markets& markets, const QString& asset, const QString& quote)
{
    for (const auto& market : markets) if (market.base == asset && market.quote == quote) return Leg{market.symbol, false};
    for (const auto& market : markets) if (market.base == quote && market.quote == asset) return Leg{market.symbol, true};
    return std::nullopt;
}
QVector<Plan> makePlans(const Markets& markets, const QVector<Selection>& selections)
{
    if (selections.isEmpty() || selections.size() > 100) throw Error(QStringLiteral("Select between 1 and 100 securities"));
    QVector<Plan> plans;
    QSet<QString> ids;
    for (const auto& s : selections) {
        if (s.securityId.isEmpty() || ids.contains(s.securityId) || !validAsset(s.asset) || !supportedFiat(s.fiat))
            throw Error(QStringLiteral("Invalid/duplicate security, Binance asset or unsupported ECB fiat: %1 / %2").arg(s.asset, s.fiat));
        ids.insert(s.securityId);
        Plan plan{s, {}};
        if (s.asset == "EUR") {
            // EUR as a security still has one EUR of value, never create a currency.
        } else if (auto direct = pair(markets, s.asset, "EUR")) {
            plan.legs.push_back(*direct);
        } else {
            bool found = false;
            for (const auto& bridge : {QStringLiteral("USDT"), QStringLiteral("USDC"), QStringLiteral("BTC")}) {
                if (bridge == s.asset) continue;
                auto first = pair(markets, s.asset, bridge);
                auto second = pair(markets, bridge, "EUR");
                if (first && second) { plan.legs = {*first, *second}; found = true; break; }
            }
            if (!found) throw Error(QStringLiteral("No active spot path to EUR for %1").arg(s.asset));
        }
        plans.push_back(plan);
    }
    return plans;
}
QStringList symbols(const QVector<Plan>& plans)
{
    QSet<QString> result;
    for (const auto& plan : plans) for (const auto& leg : plan.legs) result.insert(leg.symbol);
    auto list = result.values();
    list.sort();
    return list;
}
Prices parseTickers(const QByteArray& bytes, const QStringList& required)
{
    const auto doc = exactJson(bytes);
    if (!doc.isArray()) throw Error(QStringLiteral("Ticker response is not an array"));
    Prices prices;
    for (const auto& item : doc.array()) {
        const auto o = item.toObject();
        const auto symbol = o.value("symbol").toString();
        if (!required.contains(symbol) || prices.contains(symbol) || !o.value("price").isString())
            throw Error(QStringLiteral("Unexpected/duplicate/malformed ticker"));
        prices.insert(symbol, decimal(o.value("price").toString()));
    }
    for (const auto& symbol : required) if (!prices.contains(symbol)) throw Error(QStringLiteral("Missing ticker: %1").arg(symbol));
    return prices;
}
FxRates parseFx(const QByteArray& bytes, const QStringList& required, QDate today)
{
    const auto doc = exactJson(bytes);
    if (!doc.isArray()) throw Error(QStringLiteral("FX response is not an array"));
    FxRates rates;
    for (const auto& item : doc.array()) {
        const auto o = item.toObject();
        const auto fiat = o.value("quote").toString();
        const auto dateString = o.value("date").toString();
        const auto date = QDate::fromString(dateString, Qt::ISODate);
        if (o.value("base").toString() != "EUR" || !required.contains(fiat) || rates.contains(fiat)
            || !date.isValid() || date.toString(Qt::ISODate) != dateString || date > today || date.daysTo(today) > 7 || !o.value("rate").isString())
            throw Error(QStringLiteral("Missing, stale, future or invalid ECB reference rate"));
        rates.insert(fiat, {decimal(o.value("rate").toString()), date});
    }
    for (const auto& fiat : required) if (!rates.contains(fiat)) throw Error(QStringLiteral("Missing ECB currency: %1").arg(fiat));
    return rates;
}
QVector<Quote> calculate(const QVector<Plan>& plans, const Prices& prices, const FxRates& fx, QDate today)
{
    QVector<Quote> result;
    for (const auto& plan : plans) {
        MyMoneyMoney rate = MyMoneyMoney::ONE;
        QStringList path;
        for (const auto& leg : plan.legs) {
            if (!prices.contains(leg.symbol) || !prices[leg.symbol].isPositive()) throw Error(QStringLiteral("Missing/nonpositive ticker: %1").arg(leg.symbol));
            rate = leg.inverse ? rate / prices[leg.symbol] : rate * prices[leg.symbol];
            path << (leg.inverse ? "1/" : "") + leg.symbol;
        }
        QString source = QStringLiteral("Binance spot [%1]; EUR").arg(path.isEmpty() ? QStringLiteral("EUR identity") : path.join(" * "));
        if (plan.selection.fiat != "EUR") {
            const auto it = fx.constFind(plan.selection.fiat);
            if (it == fx.cend() || !it->rate.isPositive() || !it->date.isValid() || it->date > today || it->date.daysTo(today) > 7)
                throw Error(QStringLiteral("Missing/invalid FX for %1").arg(plan.selection.fiat));
            rate = rate * it->rate;
            source += QStringLiteral("->%1 ECB via Frankfurter FX %2").arg(plan.selection.fiat, it->date.toString(Qt::ISODate));
        } else source += QStringLiteral(" (no FX)");
        result.push_back({plan.selection, rate, today, source});
    }
    return result;
}
}
