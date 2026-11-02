// SPDX-License-Identifier: GPL-2.0-or-later
#include "ledgerwriter.h"
#include <mymoneyfile.h>
#include <mymoneyprice.h>
#include <QSet>

namespace Binance {
void writePricesAtomically(MyMoneyFile* file, const QVector<Quote>& quotes,
    const std::function<void(const MyMoneyPrice&)>& writer)
{
    if (!file) throw Error(QStringLiteral("Missing ledger"));
    // MyMoneyFileTransaction silently joins an existing transaction and its
    // commit() then does nothing. PriceModel changes cannot be rolled back by
    // that outer transaction: only allow an independently owned transaction.
    if (file->hasTransaction()) throw Error(QStringLiteral("账本正在处理其他事务，未写入价格；请稍后重试。"));
    if (quotes.isEmpty()) return;
    QVector<MyMoneyPrice> prices, previous;
    QSet<QString> seen;
    for (const auto& quote : quotes) {
        const auto& s = quote.selection;
        if (s.securityId.isEmpty() || s.fiat.isEmpty() || seen.contains(s.securityId)
            || !quote.spotDate.isValid() || !quote.rate.isPositive()) throw Error(QStringLiteral("Invalid price batch"));
        seen.insert(s.securityId);
        prices.push_back(MyMoneyPrice(s.securityId, s.fiat, quote.spotDate, quote.rate, quote.source));
        auto old = file->price(s.securityId, s.fiat, quote.spotDate, true);
        // price() can return a reverse pair. Never remove or overwrite that pair.
        if (old.from() != s.securityId || old.to() != s.fiat || old.date() != quote.spotDate) old = MyMoneyPrice();
        previous.push_back(old);
    }
    MyMoneyFileTransaction transaction(QStringLiteral("更新币安证券报价"), false);
    qsizetype attempted = 0;
    try {
        for (const auto& price : prices) {
            ++attempted; // Writer may throw after inserting the price.
            if (writer) writer(price);
            else file->addPrice(price);
        }
    } catch (...) {
        // KMyMoney 5.2 PriceModel::addPrice bypasses its undo stack: a plain
        // MyMoneyFileTransaction rollback does NOT restore prices. Compensate
        // inside the still-open transaction before RAII rolls it back.
        for (qsizetype i = attempted; i > 0; --i) {
            if (previous[i - 1].isValid()) file->addPrice(previous[i - 1]);
            else file->removePrice(prices[i - 1]);
        }
        throw;
    }
    bool notified = false;
    QObject notificationContext;
    QObject::connect(file, &MyMoneyFile::dataChanged, &notificationContext, [&] { notified = true; });
    transaction.commit();
    // With no associated stock account, 5.2's price-only commit leaves dirty()
    // true but emits no dataChanged(), which the host needs to enable Save.
    // Notify only after our independent commit, and only if it did not already
    // notify (e.g. because an account valuation changed). Failure never reaches
    // this code, so compensation/rollback does not announce a successful write.
    if (!notified) file->forceDataChanged();
}
}
