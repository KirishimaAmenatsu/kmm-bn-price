// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "quotes.h"
#include <functional>
class MyMoneyFile;
class MyMoneyPrice;
namespace Binance {
// A narrow writer seam permits offline fault injection against the real engine.
// Empty writer means MyMoneyFile::addPrice. Caller must check ledger session and
// validate security identities/target currencies before calling (GUI thread).
// Refuses an existing engine transaction; success notifies the host after an
// independent commit, without duplicating its normal dataChanged signal.
void writePricesAtomically(MyMoneyFile* file, const QVector<Quote>& quotes,
    const std::function<void(const MyMoneyPrice&)>& writer = {});
}
