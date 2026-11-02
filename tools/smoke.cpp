// SPDX-License-Identifier: GPL-2.0-or-later
#include "quoteclient.h"
#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 2) {
        QTextStream(stderr) << "Usage: quote-smoke BTC:CNY ETH:USD USDT:EUR ...\nRead-only live public API check; never opens a ledger.\n";
        return 2;
    }
    QVector<Binance::Selection> selections;
    for (int i = 1; i < args.size(); ++i) {
        const auto parts = args[i].toUpper().split(':');
        if (parts.size() != 2 || !Binance::validAsset(parts[0]) || !Binance::supportedFiat(parts[1])) return 2;
        selections.push_back({QString::number(i), parts[0], parts[1]});
    }
    Binance::QuoteClient client;
    QObject::connect(&client, &Binance::QuoteClient::succeeded, &app, [&](const QVector<Binance::Quote>& quotes) {
        QTextStream out(stdout);
        for (const auto& q : quotes) out << q.selection.asset << '/' << q.selection.fiat << ' ' << q.rate.toString() << " (exact rational), " << q.rate.formatMoney(QString(), 10, false) << '\n' << q.source << '\n';
        app.exit(0);
    });
    QObject::connect(&client, &Binance::QuoteClient::failed, &app, [&](const QString& error) { QTextStream(stderr) << error << '\n'; app.exit(1); });
    QTimer::singleShot(0, &app, [&] { client.start(selections); });
    return app.exec();
}
