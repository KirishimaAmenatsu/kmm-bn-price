# KMyMoney 币安证券报价插件

[English](README.md) | 简体中文

面向 **KMyMoney 5.2.2 / Qt 6 / KDE Frameworks 6** 的 C++ 原生插件，不是 Python 脚本或普通网页报价源。将 BTC、ETH、USDT 等作为**股票类型证券**，按每只证券自己的交易法币写入价格。

**估值方式：币安现货价格 + ECB 法币参考汇率。不是 C2C/P2P 可成交报价，不假设 USDT = USD。** 没有交易、下单、API key 或私钥功能。

## 构建与离线验证

依赖：C++17 编译器、CMake、Qt6 Core/Network/Widgets（测试另需 Test）、KF6 CoreAddons/Config/XmlGui、KMyMoney 5.2.2 开发头和 `libkmm_plugin` / `libkmm_mymoney`、Qt6 Alkimia 和 GMP。使用发行版已经安装的依赖；这里不自动安装系统包。

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

产物：`build/kmm_binance_price.so`。测试不访问网络、不打开真实账本；账本测试只使用进程内的临时引擎，GUI 测试使用 offscreen。测试覆盖精确报价换算、多证券/多法币、稳定币不等美元、停牌/缺失市场、错误 JSON/非正数、FX 日期、HTTP 429/418 冷却、取消/超时/响应上限、同 UUID 重开、原生插件实例化和 XMLGUI 工具菜单合并、故障中途写入后的旧价格恢复、拒绝已有事务及成功提交后的保存通知。`plugin_metadata` / `plugin_discovery` 用假 `AppInterface` 和真实内存引擎验证 `plug/unplug`、关闭/重开、配置对话框默认不选和卸载关闭对话框；**这不等于真实 KMyMoney 宿主 UI 或真实账本更新已经验证**。假宿主通过 `tests/upstream/interfaceloader.h` 中 v5.2.2 原接口注册表接入，仅供测试，不更改生产插件 API。

Arch 的 5.2.2 开发文件未提供 KMyMoney CMake package，因此使用 `find_path` / `find_library`。Alkimia 优先使用 `LibAlkimia6` CMake target；直接查找 Qt6 头/库作为 Arch 相对 pkg-config 路径问题的后备。`compat/` 补齐 Arch 漏装的 `kmmset.h`、`qhashseedtype.h` 和一个未使用的 export 头。前两个来自 [KDE 的 5.2 分支](https://invent.kde.org/office/kmymoney/-/tree/5.2/kmymoney/mymoney)，不修改系统头、不伪造插件 ABI。已安装的同名 KMyMoney 头优先。

### 不接触账本的联网 smoke

```sh
./build/quote-smoke BTC:CNY ETH:USD USDT:USD USDC:EUR
```

调用与插件相同的异步客户端，输出精确有理数、便于阅读的小数以及现货路径/FX 日期；不读取或写入账本。请求有 90 秒总截止时间。受地区限制、断网、TLS 或服务限流影响时会非零退出，不使用替代市场或旧报价兜底。

## 原生包与 GitHub Actions

`.github/workflows/packages.yml` 在**每个目标发行版内部从本仓库源码编译**，不是改压缩包后缀或重新包装其他发行版的二进制：

| 目标 | 下载架构 → 原生包架构 | 格式 |
| --- | --- | --- |
| Arch Linux 滚动版 | amd64 → x86_64 | `.pkg.tar.zst` |
| Debian sid | amd64 → amd64；aarch64 → arm64；riscv64 → riscv64 | `.deb` |
| openSUSE Tumbleweed | amd64 → x86_64；aarch64 → aarch64；riscv64 → riscv64 | `.rpm` |

按 Qt6/KF6/KMyMoney 开发依赖可用性选择以上目标。**RPM 指 openSUSE Tumbleweed，不代表通用 Fedora/RHEL 支持；DEB 指 sid，不代表 Debian stable/Ubuntu 支持。** 滚动镜像/仓库没有固定快照。新依赖不兼容时构建会直接失败；KMyMoney 不再是 5.2.2 时，必须先审查兼容性。

- PR 和 `main` 分支 push：Python 打包逻辑检查及三种原生 amd64 包，不发布。匹配 `v*` 的 tag push：**七包全部成功**后才发布同一个 Release。`workflow_dispatch`：始终只生成七个 Actions artifacts，即使手动选择 tag 也不发布。aarch64 用 GitHub 原生 ARM runner；riscv64 在 QEMU 中编译（不是交叉编译），单 job 最长六小时。
- 发布 tag 必须严格为 `vX.Y.Z`，与 CMake `project(VERSION)`、`src/plugin.json`、Arch 配方一致（当前 `v0.1.0`）。先同步修改源码版本再打 tag；流水线不会修改源码来迎合 tag。创建 draft 前和公开发布前，均立即解析 lightweight/annotated tag 并核对 workflow commit；上传期间移动/删除 tag 会中止公开并保留 draft。GitHub 无法原子执行 tag 核对和发布，最后检查后仍有残余竞态，必须使用仓库 ruleset 禁止发布 tag 更新和删除；本 workflow 不会配置这些 ruleset。
- 原生配方位于 `packaging/`。Arch 使用非 root `makepkg`；Debian 使用 debhelper/`dpkg-shlibdeps`；RPM 使用 `rpmbuild` 及自动 ELF 依赖。插件 ABI 不稳定，因此全部包依赖构建时**精确的 KMyMoney 包版本/release**。KMyMoney、Qt、KF ABI 更新后需重建；只锁 KMyMoney 版本无法防止所有 Qt/KF ABI 变化。
- Debian **不存在 `kmymoney-dev`**。CI 根据已安装 runtime 的 `${source:Version}` 下载匹配源码（处理 binNMU），安装构建依赖并按 Qt6/Debian 选项配置。私有 SDK 只复制上游配置后 public-header 安装规则列出的头，并链接已安装的版本化 runtime 库。未用到、需构建生成的 `kmymoneysettings.h` 被排除，不会伪造；SDK 和替代 KMyMoney 库均不进入成品。
- 查询实际 Qt6 `QT_INSTALL_PLUGINS`；必须通过离线 CTest、ELF Machine、原生包架构、精确依赖、文件布局、安装后动态链接及 Qt metadata/`kmymoney_plugins` namespace 发现验证。包**只在一次性 CI 容器中安装**，不执行联网报价 smoke，不接触真实账本。假宿主测试不等于真实 KMyMoney UI 验证。
- 只有 release job 有 `contents: write`。Actions 固定到已核验 commit SHA，checkout 不保留凭据。release job 只接受本次运行精确预期的七个包名，生成 `SHA256SUMS`，先上传 draft，核对八个资产后才公开发布。绝不覆盖已有 Release/资产；上传失败保留 draft，人工检查/删除后再重试，不主动发布不完整 Release。

文件名为 `kmm-binance-price_0.1.0-1_<发行版>_<架构>.<格式>`；Actions artifacts 保留 14 天，成功的 tag Release 保存七包及校验文件。包和校验文件均**未签名**：SHA-256 只能检查损坏，不能证明发布者身份；没有包/仓库签名密钥或自动更新仓库。安装前核对发布源码和信任边界：

```sh
sha256sum --ignore-missing -c SHA256SUMS
# 只执行与你的发行版和架构匹配的一项：
sudo pacman -U ./kmm-binance-price_0.1.0-1_archlinux_amd64.pkg.tar.zst
sudo apt install ./kmm-binance-price_0.1.0-1_debian-sid_amd64.deb
sudo zypper install --allow-unsigned-rpm ./kmm-binance-price_0.1.0-1_opensuse-tumbleweed_amd64.rpm
```

其他架构选择 `aarch64`/`riscv64` 文件。先备份账本，再按下文启用插件。精确 KMyMoney 依赖已不可用时，应针对当前目标仓库重新构建；**不要**绕过依赖检查或安装其他发行版的包。提供 workflow 定义不代表已成功运行 hosted Actions；应查看实际 tag 运行的全部七个 job 后再确认发布验证结果。

本地打包逻辑检查（仅 Python 标准库）：`python3 -m unittest discover -s packaging/tests -v`。容器入口没有 `CI_CONTAINER=1` 会拒绝安装依赖；不要在宿主机执行。

## 安装（由用户执行，本项目不会自动系统安装）

先备份账本；建议先用账本副本测试。插件必须和宿主使用兼容的 KMyMoney/Qt/KF ABI，升级后应重新构建。

可只在当前目录暂存，检查将要安装的文件：

```sh
cmake --install build --prefix "$PWD/stage"
```

默认相对安装目录是 `lib/qt6/plugins/kmymoney_plugins/`，可通过 `KMM_PLUGIN_INSTALL_DIR` 覆盖。KMyMoney 5.2.2 使用 `KPluginMetaData::findPlugins("kmymoney_plugins")` 发现插件；Arch 系统插件在 `/usr/lib/qt6/plugins/kmymoney_plugins/`，无需为测试写入这里。若复用旧构建目录且缓存仍为旧路径，运行 `cmake -S . -B build -U KMM_PLUGIN_INSTALL_DIR` 恢复新默认值；自己指定的自定义路径则应自行核对，不必重置。

用户级安装并仅为本次启动增加搜索路径：

```sh
cmake --install build --prefix "$HOME/.local"
QT_PLUGIN_PATH="$HOME/.local/lib/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}" kmymoney
```

注意 `QT_PLUGIN_PATH` 指向包含 `kmymoney_plugins/` 子目录的目录，不是 `.so` 文件。也可将 `stage/lib/qt6/plugins` 的绝对路径作为该变量来测试暂存产物。不要把 Qt5 插件目录混入 Qt6 路径。`plugin_discovery` 测试会用当前安装规则真正暂存安装到临时 `DESTDIR`，再用宿主相同的 namespace 发现插件，避免绝对路径加载掩盖安装目录错误；同时支持相对与绝对安装布局。

在 KMyMoney **设置 → 配置 KMyMoney → 插件** 中启用 **Binance Securities Prices / 币安证券报价**。插件默认不启用；启用后所有证券仍默认不选中。工具菜单会新增“配置币安证券报价…”、“更新币安证券报价”和“币安报价状态…”。若看不到，检查插件路径、ABI 和终端加载错误。

卸载：先在插件设置禁用并退出 KMyMoney，再删除自己安装的单个 `kmm_binance_price.so` 即可。配置文件独立保留，不影响账本中的既有价格。

## 配置与使用

1. 在 KMyMoney 投资账户中建立/选择**股票类型**证券，例如 Bitcoin，交易符号 `BTC`。不要在货币列表中建立 BTC/ETH 来代替证券。插件不自动建立证券、货币、持仓或交易。
2. 将每只证券的“交易货币”设为账本中已存在的目标法币，例如 CNY、USD、EUR。不同证券可以使用不同法币；插件不强制使用账本本位币。
3. 打开账本后，在工具菜单选择“配置币安证券报价…”。只列出股票类型证券；显式勾选加密资产，切勿勾选普通股票。
4. 检查并编辑 **Binance asset**。默认取证券交易符号，使用币安实际 asset code，不是交易对（例如 `BTC` 而非 `BTCUSDT`）。支持币安官方非拉丁字母代码。不会把 `1000PEPE` 自动缩放为 `PEPE`；必须确认一份证券确实对应所选 asset code 的一个单位。
5. 默认勾选“打开已配置账本时及每 15 分钟自动更新”和“保存配置后立即更新”。可分别关闭。关闭自动更新后仍可通过工具菜单手动更新。
6. “币安报价状态…”显示最后结果及每只证券使用的现货路径、FX 参考日期。自动失败不弹窗打扰，但会更新状态和日志；手动失败会显示原因。
7. 成功后在 KMyMoney 价格列表中查看 **证券 ID → 该证券交易法币** 的价格，保存账本以持久化。一天内再次更新会替换同一证券/法币/日期的价格，不保留日内时间序列。

配置在 `$XDG_CONFIG_HOME/kmm-binance-pricerc`（通常 `~/.config/kmm-binance-pricerc`），按账本 `storageId()` UUID 分组。没有显式选中的证券不会被处理，也不会更改任何证券现有的普通 online quote 设置。账本副本如果保留相同 UUID，会共享配置；这是 KMyMoney 的账本标识语义，不能把文件复制当成新 UUID。异步结果仍受独立会话 generation 隔离。

新建未保存账本若没有 UUID，插件不会调用会创建 UUID 的 getter 去污染账本。请先保存、关闭并重新打开；KMyMoney 的正常保存过程会建立该标识。

## 路径、精度和日期

每次更新先获取币安公开 `exchangeInfo`，只使用 `status=TRADING` 且 `isSpotTradingAllowed=true` 的真实市场。路径固定且可解释：

1. 优先 asset/EUR；其次相反方向 EUR/asset（取倒数）。
2. 否则依次尝试 USDT、USDC、BTC 作一个中间资产；两条边都须存在、在交易中，各自可以正向或反向。不做无限图搜索，也不把美元当成稳定币。
3. EUR → 目标法币使用 Frankfurter v2 的 **ECB provider**；EUR 本身无需 FX。

例如仅存在 BTCUSDT 和 EURUSDT 时：

```text
BTC/CNY = BTCUSDT ÷ EURUSDT × ECB(EUR→CNY)
USDT/USD = 1 ÷ EURUSDT × ECB(EUR→USD)
```

Binance ticker 的价格字符串和 FX JSON 数字的原始十进制字面量都转为 MyMoneyMoney/GMP 有理数；计算过程中不转成 `double`。拒绝零、负数、NaN、无效格式、过大指数/长度和缺失字段，不把错误响应当零。报价精度不是法币金额的两位小数。

证券价格日期是现货成功抓取的 **UTC 日期**。Binance `ticker/price` 本身不提供交易时间戳；这不是历史收盘价服务。如果批次跨越 UTC 午夜，整批拒绝并提示重试。

FX 是**工作日参考汇率，不是实时可成交汇率**。周末/节假日使用最新已公布日期；拒绝未来日期或超过 7 个日历日的参考汇率。来源明确记录 `ECB via Frankfurter FX YYYY-MM-DD`。EUR 报价来源标记 `no FX`。

支持的目标法币：EUR、AUD、BRL、CAD、CHF、CNY、CZK、DKK、GBP、HKD、HUF、IDR、ILS、INR、ISK、JPY、KRW、MXN、MYR、NOK、NZD、PHP、PLN、RON、SEK、SGD、THB、TRY、USD、ZAR。仍以服务实际有可用且不过旧的 ECB 数据为准；不支持 RUB 等没有可用 ECB 参考价的币种。不使用 Frankfurter 的其它提供方补齐缺项。

**只支持币安有上述可用路径的 asset code。** 不保证币安覆盖所有证券代码/代币，不处理合约、杠杆代币单位换算、重命名或代币迁移。请核实代码对应的资产及单位。

## 网络与账本安全边界

- 固定 HTTPS：`data-api.binance.vision` 和 `api.frankfurter.dev`。使用公开行情，不需 API key；不发送账本文件、金额、证券名称或证券 ID，但远端会看到请求的交易对和法币。
- Qt `QNetworkAccessManager` 异步请求，不用 GUI 同步等待。每请求 15 秒墙钟超时、整批 90 秒；单响应上限 16 MiB，不忽略 TLS 错误、不跟随重定向。
- 每批最多 100 只证券，去重交易对，每个 ticker 请求最多 80 对；法币一次批量请求。每次刷新市场信息，不使用可能含停牌市场的长期缓存。默认每 15 分钟，不并发重入、不自动立即重试。
- HTTP 429/418 读取 `Retry-After`（秒数或 HTTP 日期），在该插件实例的对应主机冷却期内禁止新请求。缺失或无效时至少冷却 5 分钟；状态中展示允许重试时间。不要通过反复卸载重载插件规避限流。
- 某只证券缺路径、停牌、缺 ticker/FX，或请求失败，**整批不写入任何价格**；不回退到旧行情。已有其他引擎事务时在任何写价前拒绝本批次，并提示稍后重试，不加入外层事务。正常成功只在一次独立 `MyMoneyFileTransaction` 中写入价格。KMyMoney 5.2 的 PriceModel 不受普通事务 undo 完整保护，因此写入失败时显式恢复之前的精确日期价格/移除新增价格。成功独立提交后，如果引擎尚未发出 `dataChanged`，会补发以通知宿主启用保存；失败恢复不发成功通知。
- 请求记录账本 UUID、会话 generation 和配置快照。关闭/切换/同一本重新打开、模型 reset、配置更改、卸载都会取消并废弃旧结果。完成前重新验证当前账本、证券仍存在且仍为股票、交易法币未变、配置未变，才写入。模型 reset + `modelsReadyToUse` 是 5.2 的主要生命周期信号；同时接入上游 `kmmFilePlugin` 枚举，避免依赖部分构建不会发出的旧信号。
- 不改份额、账户、交易或普通报价配置；不自动保存账本。用户主动更新会替换当天同一证券/法币的旧价格，即使旧来源不是本插件。

仍应先用账本副本试用。网络、系统时钟或远端数据异常会导致拒绝更新，而不是尝试猜测价格。此插件不是投资建议或会计价格真实性保证。

## 源码结构与参考

- `src/quotes.*`：市场路径、严格解析、精确有理数和会话票据。
- `src/quoteclient.*`：固定端点、批量异步网络、超时/取消/限流。
- `src/ledgerwriter.*`：一次事务内写价及失败恢复。
- `src/plugin.cpp`：原生 KPluginFactory、工具菜单、逐账本配置和账本生命周期。
- `tests/`：离线 QtTest、fake QNetworkReply、真实内存引擎及原生插件/UI 资源测试。
- `tools/smoke.cpp`：只读公开行情 CLI。

上游文档：[币安现货 REST API](https://github.com/binance/binance-spot-api-docs/blob/master/rest-api.md)、[币安 market-data-only](https://github.com/binance/binance-spot-api-docs/blob/master/faqs/market_data_only.md)、[Frankfurter v2](https://frankfurter.dev/)、[KMyMoney 5.2 源码](https://invent.kde.org/office/kmymoney/-/tree/5.2)。许可证：[GNU GPL 第 2 版或任何后续版本（GPL-2.0-or-later）](LICENSE)，各文件 SPDX 为准。
