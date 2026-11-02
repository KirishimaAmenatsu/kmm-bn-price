# KMyMoney Binance Securities Price Plugin

English | [简体中文](README.zh.md)

A native C++ plugin for **KMyMoney 5.2.2 / Qt 6 / KDE Frameworks 6**, not a Python script or a regular web-based quote source. Treat BTC, ETH, USDT, and similar assets as **stock-type securities**, and write prices in each security's own trading fiat currency.

**Valuation: Binance spot prices + ECB fiat reference exchange rates. These are not executable C2C/P2P quotes, and USDT is not assumed to equal USD.** There are no trading, order placement, API key, or private key features.

## Build and offline verification

Dependencies: a C++17 compiler, CMake, Qt6 Core/Network/Widgets (plus Test for tests), KF6 CoreAddons/Config/XmlGui, KMyMoney 5.2.2 development headers and `libkmm_plugin` / `libkmm_mymoney`, Qt6 Alkimia, and GMP. Use dependencies already installed by your distribution; this project does not automatically install system packages.

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

Output: `build/kmm_binance_price.so`. Tests do not access the network or open real ledger files; ledger tests use only a temporary in-process engine, and GUI tests run offscreen. Coverage includes exact quote conversion, multiple securities/currencies, stablecoins not equaling USD, suspended/missing markets, invalid JSON/non-positive numbers, FX dates, HTTP 429/418 cooldowns, cancellation/timeouts/response limits, reopening the same UUID, native plugin instantiation and XMLGUI Tools menu merging, restoration of old prices after a mid-write failure, rejection of existing transactions, and save notifications after successful commits. `plugin_metadata` / `plugin_discovery` use a fake `AppInterface` and a real in-memory engine to verify `plug/unplug`, closing/reopening, no securities selected by default in the configuration dialog, and dialog closure on unloading; **this does not mean the real KMyMoney host UI or updates to real ledger files have been verified**. The fake host connects through the original v5.2.2 interface registry in `tests/upstream/interfaceloader.h`, for testing only, without changing the production plugin API.

Arch's 5.2.2 development files do not provide a KMyMoney CMake package, so the build uses `find_path` / `find_library`. Its Alkimia pkg-config contains relative include paths, so the build locates Qt6 Alkimia directly; `compat/` supplies `kmmset.h`, `qhashseedtype.h`, and an unused export header omitted from Arch's package. The first two come from [KDE's 5.2 branch](https://invent.kde.org/office/kmymoney/-/tree/5.2/kmymoney/mymoney); no system headers are modified and no plugin ABI is fabricated. Installed KMyMoney headers with the same names take precedence.

### Online smoke test without accessing ledger files

```sh
./build/quote-smoke BTC:CNY ETH:USD USDT:USD USDC:EUR
```

Uses the same asynchronous client as the plugin and prints exact rational numbers, readable decimals, and spot paths/FX dates; it does not read or write ledger files. Requests have a 90-second overall deadline. Regional restrictions, loss of connectivity, TLS problems, or service rate limits cause a nonzero exit, with no fallback to alternative markets or old quotes.

## Installation (performed by the user; no automatic system installation)

Back up your ledger first; testing with a copy is recommended. The plugin must use a KMyMoney/Qt/KF ABI compatible with the host and should be rebuilt after upgrades.

You can stage the installation in the current directory to inspect the files that would be installed:

```sh
cmake --install build --prefix "$PWD/stage"
```

The default relative installation directory is `lib/qt6/plugins/kmymoney_plugins/`, overridable with `KMM_PLUGIN_INSTALL_DIR`. KMyMoney 5.2.2 discovers plugins using `KPluginMetaData::findPlugins("kmymoney_plugins")`; on Arch, system plugins reside in `/usr/lib/qt6/plugins/kmymoney_plugins/`, but testing does not require writing there. If you reuse an old build directory whose cache still contains the old path, run `cmake -S . -B build -U KMM_PLUGIN_INSTALL_DIR` to restore the new default; check any custom path you explicitly set yourself rather than resetting it unnecessarily.

Install for your user and add the search path for this launch only:

```sh
cmake --install build --prefix "$HOME/.local"
QT_PLUGIN_PATH="$HOME/.local/lib/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}" kmymoney
```

`QT_PLUGIN_PATH` must point to the directory containing the `kmymoney_plugins/` subdirectory, not to the `.so` file. You can also use the absolute path of `stage/lib/qt6/plugins` as this variable to test the staged build. Do not mix Qt5 plugin directories into the Qt6 path. The `plugin_discovery` test actually stages an installation into a temporary `DESTDIR` using the current installation rules, then discovers the plugin with the same namespace as the host, so absolute-path loading cannot hide installation-directory errors; this test targets the default relative installation layout.

In KMyMoney, enable **Binance Securities Prices / 币安证券报价** under **Settings → Configure KMyMoney → Plugins** (**设置 → 配置 KMyMoney → 插件** in Chinese). The plugin is disabled by default; even after enabling it, no securities are selected by default. The Tools menu gains “配置币安证券报价…” (Configure Binance Securities Prices), “更新币安证券报价” (Update Binance Securities Prices), and “币安报价状态…” (Binance Quote Status). These plugin menu labels are currently in Chinese; the English explanations here are not localized UI labels. If the entries are missing, check the plugin path, ABI, and loading errors in the terminal.

To uninstall, first disable the plugin in plugin settings and exit KMyMoney, then delete only the `kmm_binance_price.so` file you installed. The separate configuration file is retained, and existing prices in the ledger are unaffected.

## Configuration and usage

1. Create/select a **stock-type** security in a KMyMoney investment account, such as Bitcoin with trading symbol `BTC`. Do not create BTC/ETH entries in the currency list as substitutes for securities. The plugin does not automatically create securities, currencies, holdings, or transactions.
2. Set each security's trading currency (“交易货币” in Chinese) to a target fiat currency already present in the ledger, such as CNY, USD, or EUR. Different securities may use different fiat currencies; the plugin does not require the ledger's base currency.
3. After opening the ledger, choose “配置币安证券报价…” (Configure Binance Securities Prices) in the Tools menu. Only stock-type securities are listed; explicitly select crypto assets and never select ordinary stocks.
4. Check and edit **Binance asset**. It defaults to the security's trading symbol; use the actual Binance asset code, not a trading pair (for example, `BTC`, not `BTCUSDT`). Official non-Latin Binance codes are supported. `1000PEPE` is not automatically scaled to `PEPE`; you must confirm that one unit of the security really corresponds to one unit of the chosen asset code.
5. “打开已配置账本时及每 15 分钟自动更新” (Automatically update when opening a configured ledger and every 15 minutes) and “保存配置后立即更新” (Update immediately after saving configuration) are checked by default. Each can be turned off separately. Manual updates through the Tools menu remain available when automatic updates are disabled.
6. “币安报价状态…” (Binance Quote Status) displays the last result and the spot path and FX reference date used for each security. Automatic failures do not interrupt you with a popup, but update the status and logs; manual failures display the reason.
7. After success, check prices for **security ID → that security's trading fiat currency** in KMyMoney's price list, and save the ledger to persist them. Updating again on the same day replaces the price for the same security/currency/date; no intraday time series is retained.

Configuration is stored in `$XDG_CONFIG_HOME/kmm-binance-pricerc` (usually `~/.config/kmm-binance-pricerc`), grouped by the ledger's `storageId()` UUID. Securities not explicitly selected are not processed, and no security's existing regular online quote settings are changed. Ledger copies retaining the same UUID share configuration; this follows KMyMoney's ledger identity semantics, so copying a file does not create a new UUID. Asynchronous results are still isolated by a separate session generation.

If a new, unsaved ledger has no UUID, the plugin does not call the getter that would create one and alter the ledger. Save, close, and reopen it first; KMyMoney's normal save process creates this identifier.

## Paths, precision, and dates

Each update first retrieves Binance's public `exchangeInfo`, using only actual markets with `status=TRADING` and `isSpotTradingAllowed=true`. Paths are deterministic and explainable:

1. Prefer asset/EUR; otherwise use the reverse EUR/asset market (taking the reciprocal).
2. Otherwise, try USDT, USDC, then BTC as a single intermediate asset; both edges must exist and be trading, and each can be used in either direction. There is no unbounded graph search, and USD is not treated as a stablecoin.
3. EUR → target fiat currency uses the **ECB provider** in Frankfurter v2; EUR itself needs no FX conversion.

For example, when only BTCUSDT and EURUSDT exist:

```text
BTC/CNY = BTCUSDT ÷ EURUSDT × ECB(EUR→CNY)
USDT/USD = 1 ÷ EURUSDT × ECB(EUR→USD)
```

Binance ticker price strings and the original decimal literals of FX JSON numbers are converted to MyMoneyMoney/GMP rational numbers; calculations never convert them to `double`. Zero, negative numbers, NaN, invalid formats, excessive exponents/lengths, and missing fields are rejected; error responses are not treated as zero. Quote precision is not limited to the two decimal places typical of fiat amounts.

The security price date is the **UTC date** of the successful spot fetch. Binance `ticker/price` does not itself provide a trade timestamp; this is not a historical closing-price service. If a batch spans UTC midnight, the entire batch is rejected with a prompt to retry.

FX rates are **business-day reference rates, not real-time executable rates**. Weekends/holidays use the latest published date; future dates and reference rates older than 7 calendar days are rejected. The source explicitly records `ECB via Frankfurter FX YYYY-MM-DD`. EUR quote sources are marked `no FX`.

Supported target fiat currencies: EUR, AUD, BRL, CAD, CHF, CNY, CZK, DKK, GBP, HKD, HUF, IDR, ILS, INR, ISK, JPY, KRW, MXN, MYR, NOK, NZD, PHP, PLN, RON, SEK, SGD, THB, TRY, USD, ZAR. Availability still depends on the service providing sufficiently recent ECB data; currencies without available ECB reference rates, such as RUB, are not supported. Other Frankfurter providers are not used to fill gaps.

**Only Binance asset codes with an available path as described above are supported.** Binance is not guaranteed to cover every security symbol/token, and the plugin does not handle derivatives, leveraged-token unit conversions, renamings, or token migrations. Verify the asset and unit represented by each code.

## Network and ledger safety boundaries

- Fixed HTTPS hosts: `data-api.binance.vision` and `api.frankfurter.dev`. Public market data requires no API key; ledger files, amounts, security names, and security IDs are not sent, but the remote services see the requested trading pairs and fiat currencies.
- Asynchronous Qt `QNetworkAccessManager` requests, with no synchronous GUI waits. Each request has a 15-second wall-clock timeout and the whole batch has 90 seconds; each response is limited to 16 MiB. TLS errors are not ignored and redirects are not followed.
- At most 100 securities per batch, with trading pairs deduplicated and at most 80 pairs per ticker request; fiat currencies are requested in one batch. Market information is refreshed every time rather than using a long-lived cache that might include suspended markets. The default interval is 15 minutes, with no overlapping updates or automatic immediate retries.
- HTTP 429/418 responses read `Retry-After` (seconds or an HTTP date), blocking new requests to the corresponding host during that plugin instance's cooldown. A missing or invalid value results in at least 5 minutes of cooldown; the status shows when retrying is allowed. Do not repeatedly unload/reload the plugin to bypass rate limits.
- If any security lacks a path, is suspended, lacks ticker/FX data, or a request fails, **no prices are written for the entire batch**; there is no fallback to old quotes. If another engine transaction is already active, the batch is rejected before writing any prices, with a prompt to retry later; it does not join the outer transaction. Normal success writes prices in a single independent `MyMoneyFileTransaction`. KMyMoney 5.2's PriceModel is not fully protected by ordinary transaction undo, so write failures explicitly restore the previous prices for the exact dates/remove newly added prices. After a successful independent commit, if the engine has not yet emitted `dataChanged`, the plugin emits it to notify the host to enable saving; failure recovery does not send a success notification.
- Requests record the ledger UUID, session generation, and a configuration snapshot. Closing/switching/reopening the same ledger, model resets, configuration changes, and unloading cancel and discard old results. Before writing, the plugin revalidates the current ledger, that the securities still exist and remain stocks, that their trading currencies have not changed, and that configuration is unchanged. Model reset + `modelsReadyToUse` are the main lifecycle signals in 5.2; the plugin also connects to the upstream `kmmFilePlugin` enum to avoid relying on old signals that some builds do not emit.
- Shares, accounts, transactions, and regular quote settings are not changed; the ledger is not saved automatically. User-initiated updates replace existing prices for the same security/currency on that day, even if the old source was not this plugin.

Still, try it with a copy of your ledger first. Network, system-clock, or remote-data anomalies cause updates to be rejected rather than prices to be guessed. This plugin is neither investment advice nor a guarantee of accounting-price accuracy.

## Source layout and references

- `src/quotes.*`: market paths, strict parsing, exact rational numbers, and session tickets.
- `src/quoteclient.*`: fixed endpoints, batched asynchronous networking, timeouts/cancellation/rate limiting.
- `src/ledgerwriter.*`: price writes in a single transaction and failure recovery.
- `src/plugin.cpp`: native KPluginFactory, Tools menu, per-ledger configuration, and ledger lifecycle.
- `tests/`: offline QtTest, fake QNetworkReply, a real in-memory engine, and native plugin/UI resource tests.
- `tools/smoke.cpp`: read-only public market data CLI.

Upstream documentation: [Binance Spot REST API](https://github.com/binance/binance-spot-api-docs/blob/master/rest-api.md), [Binance market-data-only](https://github.com/binance/binance-spot-api-docs/blob/master/faqs/market_data_only.md), [Frankfurter v2](https://frankfurter.dev/), [KMyMoney 5.2 source](https://invent.kde.org/office/kmymoney/-/tree/5.2). License: [GNU GPL version 2 or any later version (GPL-2.0-or-later)](LICENSE); individual files' SPDX identifiers take precedence.
