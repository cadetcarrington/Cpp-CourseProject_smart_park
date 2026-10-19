## D1 图表清单与成图

六张图全部用 Python 3 + Pillow 12 绘制（本机无 matplotlib），公共绘图库为 `docs/report-build/figures/_figlib.py`，统一使用 `/System/Library/Fonts/STHeiti Medium.ttc` 字体、白色背景、2000 px 宽度，正文最小字号 15 px；配色按语义固定（蓝＝管理端、橙＝网页端、绿＝道闸终端、靛＝服务端、紫＝核心业务、琥珀＝持久化数据、青＝Python 识牌）。每张图落盘后都用 `read_image` 逐张目视复核过，确认无文字重叠、无箭头穿卡、无内容溢出画布。插入 Word 时建议按版心宽度 15–16 cm 等比缩放；图题与图号由 Lead 统一编排，正文引用请使用下表 id 对应的 `[[FIG:...]]`。

| 图 id | PNG 文件 | 尺寸（像素） | 文件大小 | 生成脚本 |
| --- | --- | --- | --- | --- |
| fig-architecture | `figures/fig-architecture.png` | 2000×1500 | 357 KB | `figures/fig1_architecture.py` |
| fig-flow-main | `figures/fig-flow-main.png` | 2000×2080 | 540 KB | `figures/fig2_flow.py` |
| fig-modules | `figures/fig-modules.png` | 2000×1600 | 382 KB | `figures/fig3_modules.py` |
| fig-db-er | `figures/fig-db-er.png` | 2000×1940 | 322 KB | `figures/fig4_er.py` |
| fig-remote-seq | `figures/fig-remote-seq.png` | 2000×1893 | 268 KB | `figures/fig5_sequence.py` |
| fig-payment-flow | `figures/fig-payment-flow.png` | 2000×1440 | 259 KB | `figures/fig6_payment.py` |

各图建议图题与一句话说明（`FIG|图 id|建议图题|一句话说明`）：

FIG|fig-architecture|SmartPark 系统总体架构|四类客户端经 TCP 9527、HTTP 18080、WebSocket 18081 接入统一服务端，服务端进程内链接核心库并持久化到 SQLite，识牌由独立 Python 进程完成
FIG|fig-flow-main|SmartPark 主业务流程|服务端视角的启动、入场、车位分配、在场服务、离场结算十步主链路，并给出满场、断网补报、爽约与预约取消、支付订单超时四类分支
FIG|fig-modules|SmartPark 功能模块图|macOS 管理端、统一服务端、网页用户端三列功能模块，以及两者共用的核心公共模块（领域模型、业务服务、算法、持久化、审计）
FIG|fig-db-er|SQLite 数据库 ER 关系图|12 张表按账号、支付、预约、布局与停车、审计五组布局，标注主键、外键、唯一约束与跨表业务关联
FIG|fig-remote-seq|远程模式时序图|管理端经 TCP 调用服务端、服务端进程内调用 ParkingService 并落库、事件经 TCP 与 WebSocket 双通道回推的十三步时序
FIG|fig-payment-flow|支付订单状态机|payment_orders 的 pending／paid／expired／cancelled／refunded 五态与六条迁移，含 15 分钟超时关闭、支付成功即结算离场与重复确认幂等

## D2 测试证据表

### D2.1 CTest 注册的 12 项测试

`tests/CMakeLists.txt` 用 12 个 `add_test` 注册下列测试（第 10、14、36、51、80、109、117、131、133、136、138、141 行），断言宏 `expect(` 在 `tests/*.cpp`、`tests/*.mm` 中共 784 处（`core_model_tests.cpp` 596、`macos_remote_tests.mm` 129、`lpr_concurrency_tests.cpp` 38、`background_planning_tests.cpp` 21；`gate_replay_tests.cpp` 与 `macos_map_tests.mm` 用自带断言宏，计 0）。

| 测试名 | 类型 | 覆盖点 | 默认启用条件 | 标签 |
| --- | --- | --- | --- | --- |
| smartpark_core_tests | C++ 单元 | 领域模型、计费、预约、选位与路径、仓库事务、审计哈希链等核心逻辑 | 无条件注册，始终启用 | — |
| smartpark_background_planning_tests | C++ 单元 | 后台入场规划 BackgroundEntryPlanner；`TIMEOUT 90` | 无条件注册，始终启用 | — |
| smartpark_gate_replay_tests | C++ 单元 | 道闸断线补报的"部分确认"语义：真实 OfflineQueue + 真实协议分帧 + 进程内 mock 服务端只回半帧，验证失败安全 | 需存在 `smartpark_gate` 目标（`apps/gate` 默认构建） | — |
| smartpark_lpr_concurrency_tests | C++ 单元 | 识牌并发上限、有界排队、超时杀整棵进程树、context 销毁取消、JSON 解析、同步入口超时清理；`TIMEOUT 120` | 无条件注册，始终启用 | — |
| smartpark_macos_map_tests | ObjC++ 单元 | 管理端地图几何与坐标换算、`--layout` 开库路径（`SMARTPARK_DATA_DIR` 注入样例布局）、PlatePhotoArchive／ParkingBridge | `APPLE` 且存在 `smartpark_admin_macos` 目标 | — |
| smartpark_macos_remote_tests | ObjC++ 集成 | 自带进程内服务端，把真实 MainWindowController 按远程模式接上去，验证快照数据、页面可见性与各页渲染；也可 `--remote <host> <port> <user>` 指向真实服务端 | `APPLE` 且存在 `smartpark_admin_macos` 目标 | — |
| smartpark_rest_e2e | Shell 端到端 | 真实服务端进程跑 REST／WebSocket／TCP 全链路；`RUN_SERIAL TRUE`、`TIMEOUT 240` | 存在 `smartpark_server` 目标且找到 bash | e2e |
| smartpark_lpr_script_tests | Python 单元 | 单图识牌脚本 `scripts/recognize_plate.py` 的参数、JSON 输出与错误码（25 个用例） | 找到 Python 3 解释器 | — |
| smartpark_lpr_resident_tests | Python 单元 | 常驻识牌进程的协议、预热、退出清理（17 个用例） | 找到 Python 3 解释器 | — |
| smartpark_plate_geometry_tests | Python 单元 | 车牌四角几何与裁剪（9 个用例） | 找到 Python 3 解释器；无 numpy／opencv 时视觉用例自动跳过（exit 0） | — |
| smartpark_plate_dataset_tests | Python 单元 | 样例车牌数据集清单与标注（8 个用例） | 找到 Python 3 解释器 | — |
| smartpark_plate_color_tests | Python 单元 | 车牌底色判定（7 个用例） | 找到 Python 3 解释器；缺 numpy 时自动跳过 | — |

口径说明：上表是 `tests/CMakeLists.txt` 定义的项数；`ctest -N` 实测 `build/macos` 注册 14 项 = 上述 12 项 + `smartpark_server_selftest`（`apps/server/CMakeLists.txt:15`）与 `smartpark_gate_selftest`（`apps/gate/CMakeLists.txt:25`）两项可执行文件自检；`build/server` 目录当前只注册 10 项（缺 `smartpark_background_planning_tests` 与 `smartpark_lpr_concurrency_tests`，该构建目录未随测试定义重新 configure）。引用"12 项"时应同时说明是否含 2 项自检。

### D2.2 Python 单元测试统计

`tests/test_*.py` 共 6 个文件、75 个 `def test_`，每个文件由 Python 标准库 unittest 风格函数组成，其中 5 个文件已注册进 CTest，`test_lpr_runtime_cache.py` 为手工运行。

| 文件 | 用例数 | 是否注册进 CTest | 覆盖点 |
| --- | --- | --- | --- |
| `tests/test_recognize_plate.py` | 25 | 是（smartpark_lpr_script_tests） | 单图识牌的参数校验、JSON 字段、错误码、无车牌路径 |
| `tests/test_lpr_resident.py` | 17 | 是（smartpark_lpr_resident_tests） | 常驻服务握手、预热、并发请求、SIGTERM 清理与 socket 回收 |
| `tests/test_plate_geometry.py` | 9 | 是（smartpark_plate_geometry_tests） | 四角点排序、透视裁剪、边界外扩 |
| `tests/test_plate_datasets.py` | 8 | 是（smartpark_plate_dataset_tests） | 数据集清单、省份覆盖、标注一致性 |
| `tests/test_plate_color.py` | 7 | 是（smartpark_plate_color_tests） | 蓝／绿／黄底判定与置信度阈值 |
| `tests/test_lpr_runtime_cache.py` | 9 | 否 | 运行时缓存复用与失效（手工脚本运行） |
| 合计 | 75 | 5 个文件已注册 | — |

### D2.3 e2e REST 用例清单

`tests/e2e_rest.sh` 共 484 行，自带 `chk` 断言函数并统计失败数（41 处 `chk`），默认端口 TCP 19527、HTTP 18080、WebSocket 18081（第 22–24 行），启动真实服务端进程并使用临时数据库与 `--advertise 127.0.0.1`（第 136–137 行）。

| 段 | 起始行 | 覆盖内容 | 代表性断言 |
| --- | --- | --- | --- |
| A | 154 | 静态资源与安全头 | H5 首页、app.js、style.css 均 200；未知路径回退 SPA 首页 |
| B | 167 | 认证 | 错误口令 401；注册 201；重复注册 409；无 token 访问受保护接口 401 |
| C | 190 | 扫码进入流程 | `site/resolve` 免认证且回显点位名；无效票据 404；绑定车牌 200；重复绑定幂等；未登录绑定 401 |
| D | 205 | 车位与状态 | `capacity` 与 `spots` 均为 75（六层布局）；`layout` 200 |
| E | 211 | 入场→离场→结算 | 入场 200；占用数 +1；离场 200；重复离场 409 |
| F | 229 | 预约→到场确认 | 提前量不足 409；按车牌查预约 1 条；非 admin 不指定车牌 400；admin 查全部 200；checkin 200；寻车指引 200 |
| G | 253 | 模拟收银台 | 不在场车牌下单 404；确认支付 200；订单状态变为 paid |
| H | 268 | 二维码 / 车牌 | 服务端生成二维码 200；已移除的无感支付接口 404 |
| I | 273 | WebSocket 实时推送 | 先 auth 后订阅；业务事件能到达 WS 客户端 |
| I2 | 337 | 跨入口事件 | REST 动作产生的 `parking.entered` 等事件能送达 TCP 客户端 |
| J | 402 | TCP 协议（道闸 / 用户端） | 长度前缀分帧、登录、action 路由与错误码 |
| K | 427 | 数据一致性 | 入场使占用数 +1、记录数 +1；离场使占用数 −1、不新增记录且该记录转已结算 |
| L | 444 | 识牌接口（脚本模式） | 无车牌时返回 422 |

### D2.4 LPR 性能与精度数据

数据来自 `docs/lpr-performance.md` 的实测表与评测章节，原始逐图数据在 `docs/lpr-benchmark-20261009.json` / `.csv`、`docs/lpr-benchmark-20261012-s1.json` / `.csv`、`docs/lpr-benchmark-20261012-macos.json` / `.csv`。

| 指标 | 结果 | 数据集与口径（出处） |
| --- | --- | --- |
| 原单图 CLI 延迟 | 14.284 秒 | s1 同机同权重，同一张图片 3 次，取中位数（`docs/lpr-performance.md:11`） |
| 常驻模式热请求延迟 | 1.427 秒 | s1，经真实 TCP 服务端 5 次热请求中位数（`:12`） |
| 热请求加速比 | 10.01 倍 | 同上两组之比（`:13`） |
| 200 张评测热请求 | 中位 1.308 秒／P95 1.342 秒 | 常驻模式，200 张样例逐图请求（`:14`） |
| 启动与首张请求 | 6.063 秒／6.273 秒 | 200 张评测中的常驻进程启动与首张请求（`:15`） |
| TCP 桥接首张请求 | 7.486 秒 | 不含进程启动（`:16`） |
| 完整输出一致性 | 199 张字段与数值完全相同，1 张同样未检出 | 200 张逐图比对，置信度容差 1e-6（`:18-20`） |
| 精度 v1（原始 200 张） | s1 191/200＝95.5%；macOS 192/200＝96.0% | 含 8 张误判 + 1 张未检出（`:109-110`） |
| 精度 v2（替换后 200 张） | s1 与 macOS 均为 199/200＝99.5% | 选择性替换 9 张失败样本，**不是泛化精度**，不能与 95.5% 直接比较（`:111-118`） |
| macOS 本机链路 | 单图 7.888 秒；常驻热请求中位 0.627 秒；预热 6.169 秒后首请求 0.591 秒 | macOS 本机，torch 2.14.0／ultralytics 8.4.164／paddle 3.3.1（`:30`、`:40`） |

## 4.2.6 自动化测试与性能验证

本系统的测试按"单元—集成—端到端"三层组织，入口集中在 CMake/CTest：`tests/CMakeLists.txt` 用 12 个 `add_test` 注册 C++／Objective-C++ 单元测试、Python 单元测试与一项 REST 端到端脚本，另在 `apps/server/CMakeLists.txt:15` 与 `apps/gate/CMakeLists.txt:25` 注册了两个可执行文件自检，`ctest --test-dir build/macos -N` 实测共 14 项。断言密度可以说明覆盖面：`tests/*.cpp`、`tests/*.mm` 中共 784 处 `expect(`，`tests/test_*.py` 中 75 个 `def test_`，端到端脚本另有 41 处 `chk` 断言。

从覆盖点看，12 项测试中前 7 项是核心业务、后台规划、道闸补报、识牌并发、管理端地图、管理端远程界面与 REST 端到端，后 5 项是识牌脚本、常驻服务、车牌几何、数据集与底牌颜色的 Python 评测（`tests/CMakeLists.txt:10-143`）。其中只有端到端用例带标签：`smartpark_rest_e2e` 设置 `LABELS e2e`、`RUN_SERIAL TRUE`、`TIMEOUT 240`（第 117–125 行），因此可以用 `ctest -LE e2e` 跳过慢用例；其余按构建条件启用——道闸回放要求存在 `smartpark_gate` 目标（第 19 行），管理端两项要求 `APPLE` 且存在 `smartpark_admin_macos` 目标（第 55 行），Python 五项要求找到解释器（第 129–130 行）。这种"默认尽量全跑、缺依赖就跳过"的写法，使同一份测试定义能在 macOS 管理端构建与纯服务端构建中复用。

测试设计上有四个值得说明的做法。第一，识牌并发不依赖真实模型：测试用假 shell 脚本充当 `--lpr-command`，在进程内覆盖并发上限、有界排队、超时杀整棵进程树、`context` 销毁取消与 JSON 解析（`tests/CMakeLists.txt:40-43`），因此能在秒级完成且不需要 GPU。第二，道闸补报用真实 `OfflineQueue` 与真实协议分帧，另起进程内 mock 服务端"只回半帧"，专门验证"只确认连续前缀"的失败安全语义（第 17–18 行）。第三，管理端远程模式自带进程内服务端，把真实 `MainWindowController` 按远程方式接上去，验证的是真界面而不是桩；同时保留 `--remote <host> <port> <user>` 以便指向真实服务端（第 82–84 行）。第四，`rest_e2e` 启动真实服务端进程、使用临时数据库并显式 `--advertise 127.0.0.1`（`tests/e2e_rest.sh:136-137`），十二段用例覆盖静态资源、认证、扫码进入、车位状态、入场离场结算、预约到场、模拟收银台、二维码、WebSocket 推送、REST→TCP 跨入口事件、TCP 协议、数据一致性与识牌接口（分别始于第 154、167、190、205、211、229、253、268、273、337、402、427、444 行）。

性能与精度证据集中在识牌侧。在 s1 同机同权重下，原单图 CLI 同一张图片三次中位 14.284 秒，改为常驻模式后 5 次热请求中位 1.427 秒，加速 10.01 倍；200 张评测的常驻热请求中位／P95 为 1.308／1.342 秒，进程启动与首张请求为 6.063／6.273 秒（`docs/lpr-performance.md:11-17`）。精度上，v1 评测集在 s1 为 191/200（95.5%）、macOS 本机为 192/200（96.0%）；v2 替换 9 张失败样本后两处均为 199/200（99.5%），但该组属于选择性替换，不能与 95.5% 直接比较（`:109-118`）。引用这些数字时必须带上口径：200 张样例包含原本失败的图片且存在已记录的训练污染，只用于验证优化没有改变原输出，不是独立泛化测试（`:20-22`）；s1 与 macOS 的依赖版本不同，置信度浮点值可能略有差异（`:32`）。原始逐图数据见 `docs/lpr-benchmark-20261009.json`、`docs/lpr-benchmark-20261012-s1.json` 与 `docs/lpr-benchmark-20261012-macos.json`。

测试暴露的问题主要有两类。第一类是推理链路的性能：单图 CLI 每次冷启动 14.284 秒，无法满足道闸与入场体验，为此引入常驻服务（一个 YOLO 对象加独立 OCR worker，串行处理请求，只缓存四个初始化步骤、不缓存识别结果，`docs/lpr-performance.md:44-48`），并增加启动异步预热——完整预热 6.169 秒后首次真实请求 0.591 秒（`:34-40`），管理端还做了后台预热与父 PID 监控，应用退出时清理子进程（`:36`）。第二类是评测口径：v2 的替换若不加说明会虚高精度，因此在 `docs/lpr-performance.md:118` 与 `docs/lpr-eval-replacements-20261012.md:91` 固定了口径说明；并发回归同时覆盖超时杀进程树与取消排队（`tests/CMakeLists.txt:40-43`），避免关闭识别后残留子进程。

综上，本系统以 CTest 为唯一入口，把 12 项测试（1 项带 e2e 标签）、6 个 Python 测试文件（其中 5 个注册进 CTest）与 2 项自检组合成覆盖"模型—服务—网络—端到端"的回归网；性能数据都标注了机器、数据集与统计口径，99.5% 的精度提升也明确说明是选择性替换后的数值。需要说明的是，本节测试项数量以 `build/macos` 构建目录下 `ctest -N` 实测的 14 项为准（含 2 项可执行文件自检）；不同构建目录的注册项可能略少，正式验收时应以最新一次 `ctest` 输出为准。

## D3 运行截图（可选）

网页用户端截图已真实取得。做法是启动 `build/server-release/apps/server/smartpark_server`，参数为 `--port 19527 --http-port 19080 --ws-port 19081 --db /tmp/sp-shot/shot.db --layout data/garage-6f.txt --web-root apps/webclient`，数据库与 Chrome 用户数据目录都放在 `/tmp`，未写入仓库；随后用 `/Applications/Google Chrome.app/Contents/MacOS/Google Chrome --headless=old --no-sandbox --disable-gpu --disable-breakpad --disable-crash-reporter --hide-scrollbars --user-data-dir=/tmp/... --window-size=430,932 --virtual-time-budget=5000 --screenshot=...` 逐页截图。本机的 `--headless=new` 会报 GPU 进程错误并挂起，退回 `--headless=old` 后正常出图；截图为 430×932（iPhone 14 Pro 逻辑分辨率）。

| 图 id | 文件 | 尺寸 | 页面内容 |
| --- | --- | --- | --- |
| fig-web-shot | `figures/screenshots/web-login.png` | 430×932 | 网页端登录页：SmartPark 抬头、登录/注册切换、账号与密码输入、底部"没有账号？去注册" |
| fig-web-claim | `figures/screenshots/web-claim.png` | 430×932 | 扫码进入页：带站点票据访问时提示"扫码成功"，先设置账户再绑定车牌 |
| fig-web-register | `figures/screenshots/web-register.png` | 430×932 | 注册页：账号 2–24 字符、密码 6–64 字符与注册按钮 |

FIG|fig-web-shot|图4-2 网页用户端运行界面|headless Chrome 访问本机 18080/19080 端口的网页用户端登录页，展示抬头与登录/注册切换
FIG|fig-web-claim|网页用户端扫码进入页|带站点票据打开 #/claim，提示扫码成功并要求先设置账户
FIG|fig-web-register|网页用户端注册页|注册表单的账号与密码长度约束

说明两点：截图右侧约有 10–20 px 被裁切，原因是页面在 430 px 视口下自身存在轻微横向溢出（把视口放宽到 480 px 同样如此），截图未做任何裁剪或修图，保持真实运行结果；用无头浏览器无法先登录再截首页、预约、缴费页，因为登录态存在 `localStorage`，因此这三页没有截图，正文请用文字与交互流程描述。

macOS 管理端截图未取得：该端是 AppKit 原生窗口，需要图形会话与屏幕录制权限，本环境无法稳定截取，未强行操作。整理结束时已 kill 自建的 `smartpark_server` 进程并删除 `/tmp/sp-shot` 临时目录，`git status` 中除新增 `docs/report-build/` 外没有数据库或日志文件，确认未污染仓库。

## D4 事实核对清单

下表逐项核对 OUTLINE 第 4 节的事实清单，差异一律以源码或数据文件为准，并给出可直接复查的依据。

| 核验项 | 第 4 节写法 | 核对结果 | 依据与处理建议 |
| --- | --- | --- | --- |
| CTest 项数 | 注册 12 项 | `tests/CMakeLists.txt` 确有 12 个 `add_test`；但 `ctest -N` 实测含 2 项可执行文件自检共 14 项，`build/server` 目录当前只注册 10 项 | `tests/CMakeLists.txt:10-143`、`apps/server/CMakeLists.txt:15`、`apps/gate/CMakeLists.txt:25`；正文写"12 项测试 + 2 项自检"，不要直接说"ctest 共 12 项" |
| 3tower 场地尺寸 | 166.5 m × 85 m | 数据文件为 `site 165 85`，即 165 m × 85 m | `data/garage-3tower.txt:11`；正文改用 165 m × 85 m |
| 3tower 车位与分区 | 330 车位、7 分区、2 进 2 出 | 车位 330 一致；分区 10 个 region；出入口为 1 个入口 + 2 个出口 | `data/garage-3tower.txt:20` 起 `region` 行合计 330、`entrance`/`exit` 行分别 1 与 2；正文改为"10 个分区、1 进 2 出" |
| README 的 3tower 数据 | —（README 写 190×88 m、335 车位） | README 该行口径过期，与数据文件不一致，数据文件才生效 | `README.md` 布局表；建议 Lead 顺手修正 README |
| 支付订单状态机 | created→paid→closed／refunded | 源码 CHECK 约束为 `pending/paid/expired/refunded/cancelled`，没有 created 与 closed | `src/network/RestGateway.cpp:506`；图 fig-payment-flow 已按代码口径绘制 |
| REST 端点数量 | 34 个 | `docs/rest-api.md` 端点总览表共 34 行端点，一致 | `docs/rest-api.md:80-231` |
| 服务端口 | TCP 9527／HTTP 18080／WS 18081 | 源码默认值为 TCP 9527、HTTP 8080、WS 8081；18080／18081 是部署与演示时显式指定的值 | `apps/server/main.cpp:1094`、`:1104`、`:1106`；`README.md:116`；正文写"默认 8080／8081，演示改用 18080／18081" |
| apps/macos 的 .mm 文件数 | 23 个 | 实测 23 个（含 `bridge/` 子目录） | `find apps/macos -name '*.mm'` 后接 `wc -l` 统计 |
| src 代码行数 | 12634 行 | 实测 12634 行 | `find src` 按 `.cpp/.h/.mm` 过滤后 `xargs wc -l` 统计 |
| expect 断言数 | 784 处 | 实测 784 处（596＋129＋38＋21） | 同上命令；`gate_replay_tests.cpp`、`macos_map_tests.mm` 用其它断言宏，计 0 |
| Python 单测数 | 75 个 `def test_` | 实测 75 个（25＋17＋9＋8＋9＋7） | `tests/test_*.py` |
| SQLite 表数 | 12 张表 | 表名与 12 张一致：parking_spots、parking_records、layout_snapshot、bookings、reservations、deposit_payments、users、auth_tokens、audit_logs、payment_orders、site_tickets、user_plates | `src/core/persistence/ParkingRepository.cpp`、`UserStore.cpp`、`AuditLogService.cpp`、`src/network/RestGateway.cpp:506`、`:1765`、`:1777` |
| 六层布局车位 | 75 车位 | 一致：`data/garage-6f.txt` 的 14 个 region 合计 75 | `data/garage-6f.txt` |
| 识牌性能与精度口径 | 未在第 4 节展开 | 引用时必须注明：v2 的 199/200 属选择性替换，不能与 95.5% 直接比较；200 张样例含失败图片与训练污染 | `docs/lpr-performance.md:109-118`、`:20-22` |

处理建议：3tower 的尺寸、分区与出入口三处请正文按数据文件改写；CTest 数量补充"另含 2 项自检"的口径；支付状态机沿用代码口径（图已按此绘制）；其余数字与表格核对一致，可直接引用。
