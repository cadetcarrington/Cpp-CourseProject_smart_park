# SmartPark 课程设计报告 · 写作总纲与分工（Lead 维护，唯一权威）

> 本文件是本次报告写作的唯一权威约定。所有协同者动手前必须先完整读一遍。
> 最后更新：2026-10-18。

## 0. 任务与硬约束

- 目标：参照 `/Users/Zhuanz/Downloads/程序设计综合实践-报告模板.doc` 的章节骨架，**新建**一份 Word 报告，填入本组 SmartPark 项目的内容。
- **绝对禁止修改模板文件** `/Users/Zhuanz/Downloads/程序设计综合实践-报告模板.doc`；也不要修改任何源码、数据、构建产物和 git 历史。报告取材只读。
- 报告内容**只写 macOS 管理端、服务端、网页用户端三部分**（以及三者之间的接口与协同），**不写 Windows 相关内容**。已归档的 Qt Widgets 管理端、旧版 CLI 等不写；如需交代演进最多一句带过。
- 工作目录（仓库根）：`/Users/Zhuanz/Documents/ChatGPT/c++课设/smartpark`
- 最终文档（Lead 生成，协同者不生成 docx）：
  `docs/SmartPark课程设计报告-macOS服务端网页部分.docx`
- 报告日期、学校/学院/组号/成员/指导教师/成绩评语等固定信息由 Lead 按模板原样迁移；协同者不要写封面和任务书。

## 1. 最终文档结构（模板骨架 + 分工）

| 编号 | 章节标题 | 负责人 | 素材/说明 |
| --- | --- | --- | --- |
| — | 封面、组员页、设计任务书、目录 | Lead | 按模板原样迁移 |
| 第一章 | 绪论（1.1 目的 / 1.2 背景 / 1.3 要求 / 1.4 环境） | Lead | 项目背景、技术栈、运行环境 |
| 第二章 | 需求分析（2.1 问题描述 / 2.2 系统功能分析 / 2.3 程序难点与解决） | Lead 写 2.1、2.3；2.2.1=macos-writer，2.2.2=server-writer，2.2.3=web-writer | 2.2 按三端拆成三个三级标题 |
| 3.1.1 | 核心数据模型与类设计 | server-writer | 领域类 + 核心服务类 + 12 张 SQLite 表 + 协议对象 |
| 3.1.2 | macOS 管理端界面设计 | macos-writer | 主窗口/7 页面/模态框/图表/车位图绘制 |
| 3.1.3 | 网页用户端界面设计 | web-writer | SPA 页面结构、风格、响应式、扫码 |
| 3.2.1 | 系统流程图 | server-writer | 总流程 + 入场/离场/预约/支付/补报，配 `[[FIG:fig-flow-main]]` |
| 3.2.2 | 功能模块图 | server-writer | 三层模块树，配 `[[FIG:fig-modules]]` |
| 4.1.1 | macOS 管理端操作演示 | macos-writer | 按真实操作步骤写 |
| 4.1.2 | 网页用户端操作演示 | web-writer | 按扫码→登录→…真实步骤写 |
| 4.1.3 | 服务端部署与运行验证 | server-writer | 构建预设、启动参数、端口、账号、静态资源 |
| 4.2.1 | 车位分配与路径规划算法实现 | server-writer | Dijkstra/A*、权重、六项评分、分区水位、应急让位 |
| 4.2.2 | 时段预约与定金状态机 | server-writer | 冲突校验、锁位、核销、取消/爽约、抵扣、一人一单 |
| 4.2.3 | 计费与模拟支付实现 | web-writer | 计费规则 + payment_orders 状态机 + 二维码 + 超时关闭 |
| 4.2.4 | 图片识牌集成 | server-writer | YOLO+PP-OCRv5、常驻推理进程、四角矫正、REST/TCP 入口、人工核对 |
| 4.2.5 | 多端同步、断网补报与数据恢复 | server-writer | EventHub、TCP/WS、SQLite 恢复、gate 离线队列、重连补报 |
| 4.2.6 | 自动化测试与性能验证 | figures-evidence | CTest 12 项、Python 测试、e2e 用例、LPR 性能数据 |
| — | 设计总结（含“课程设计过程中遇到的问题”） | Lead（整合三端附B） | 5–8 条问题与解决 |
| — | 参考文献 | Lead | 10–15 条，真实可查 |
| — | 附录（源程序代码） | Lead（用各端附C建议） | 3–4 段代表性代码 |
| — | 指导教师评语页 | Lead | 按模板原样 |

## 2. 四个部分的任务拆分

### Part A｜macOS 管理端（负责人：macos-writer）
产出：`docs/report-build/parts/02-macos.md`。只写下列章节（标题字符串照抄，编号已定）：

- `## 2.2.1 macOS 管理端功能需求`（400–600 字）
- `## 3.1.2 macOS 管理端界面设计`（2400–3200 字）：
  - `### 3.1.2.1 界面总体结构与导航`（主窗口、侧边栏、原生 AppKit 组件、浅色/深色？如不确定不写）
  - `### 3.1.2.2 登录与注册窗口`
  - `### 3.1.2.3 仪表盘页面`
  - `### 3.1.2.4 停车地图页面`
  - `### 3.1.2.5 车辆作业页面`
  - `### 3.1.2.6 车位占用、预约管理与停车记录页面`
  - `### 3.1.2.7 设置页面`
  - `### 3.1.2.8 远程模式与数据源切换`（ParkingDataSource/RemoteDataSource、bridge 层、实时刷新）
- `## 4.1.1 macOS 管理端操作演示`（1500–2000 字）：
  - `### 4.1.1.1 启动、登录与本地/远程模式选择`
  - `### 4.1.1.2 仪表盘与实时车位图`
  - `### 4.1.1.3 车辆入场与车位分配`
  - `### 4.1.1.4 车辆离场与计费`
  - `### 4.1.1.5 预约管理与到场核销`
  - `### 4.1.1.6 图片识牌与人工核对`
  - `### 4.1.1.7 停车记录查询与运营图表`
- `## 附A macOS 端测试与验证`（300–500 字，列测试文件名与覆盖点：`tests/macos_map_tests.mm`、`tests/macos_remote_tests.mm` 等）
- `## 附B macOS 端问题与经验`（4–6 条，每条 1–2 句；供设计总结使用）
- `## 附C 建议附录代码片段`（3–5 条，格式 `文件:起始行-结束行 — 说明`）
- `## 附D 事实清单`（10 条以内，格式 `事实 — 出处文件:行号`）

### Part B｜服务端（负责人：server-writer）
产出：`docs/report-build/parts/03-server.md`。只写：

- `## 2.2.2 服务端功能需求`（400–600 字）
- `## 3.1.1 核心数据模型与类设计`（2500–3200 字）：
  - `### 3.1.1.1 领域模型类`（Vehicle/ParkingSpot/ParkingRecord/Reservation/ParkingLayout/Geometry 等）
  - `### 3.1.1.2 核心业务类`（ParkingService/SpotAllocator/GridPlanner/Billing/ReservationService/FakePaymentGateway/AnalyticsEngine/AuditLogService/UserStore 等）
  - `### 3.1.1.3 数据库表结构设计`（12 张表：名称、用途、关键字段、关系；可配 `[[FIG:fig-db-er]]`）
  - `### 3.1.1.4 通信协议与数据对象`（TCP 帧格式/JSON 字段约定、REST DTO 与错误码）
- `## 3.2.1 系统流程图`（600–800 字，先文字描述主流程与分支，再 `[[FIG:fig-flow-main]]`；远程模式时序可配 `[[FIG:fig-remote-seq]]`）
- `## 3.2.2 功能模块图`（400–600 字，功能模块树文字版，配 `[[FIG:fig-modules]]`）
- `## 4.1.3 服务端部署与运行验证`（700–1000 字：构建三预设、启动参数表、端口、数据库位置、演示账号、网页静态资源、启动横幅/二维码）
- `## 4.2.1 车位分配与路径规划算法实现`（900–1200 字；Dijkstra 一次全图搜索、A* 单目标、代价权重、六项评分、分区水位、应急让位；引用真实文件:行号）
- `## 4.2.2 时段预约与定金状态机`（800–1000 字）
- `## 4.2.4 图片识牌集成`（700–900 字；含性能/精度数字须注明口径，引 `docs/lpr.md`、`docs/lpr-performance.md`）
- `## 4.2.5 多端同步、断网补报与数据恢复`（700–900 字）
- `## 附A 服务端测试与验证`（300–500 字）
- `## 附B 服务端问题与经验`（4–6 条）
- `## 附C 建议附录代码片段`（3–5 条）
- `## 附D 事实清单`（10 条以内）

### Part C｜网页用户端（负责人：web-writer）
产出：`docs/report-build/parts/04-web.md`。只写：

- `## 2.2.3 网页用户端功能需求`（400–600 字）
- `## 3.1.3 网页用户端界面设计`（2000–2600 字）：
  - `### 3.1.3.1 页面总体结构与视觉风格`
  - `### 3.1.3.2 扫码进入、注册与登录`
  - `### 3.1.3.3 余位查询与分区展示`
  - `### 3.1.3.4 时段预约与取消`
  - `### 3.1.3.5 缴费收银台`
  - `### 3.1.3.6 停车记录、到场引导与反向寻车`
  - `### 3.1.3.7 响应式布局与浏览器兼容`
- `## 4.1.2 网页用户端操作演示`（1200–1600 字，按手机扫码→注册/登录→绑车牌→查余位→预约→缴费→寻车真实流程）
- `## 4.2.3 计费与模拟支付实现`（900–1200 字：计费规则、订单状态机、二维码、超时关闭、支付成功结算离场、防重复支付；配 `[[FIG:fig-payment-flow]]`）
- `## 附A 网页端测试与验证`（300–500 字）
- `## 附B 网页端问题与经验`（4–6 条）
- `## 附C 建议附录代码片段`（3–5 条）
- `## 附D 事实清单`（10 条以内）

### Part D｜图表与测试证据（负责人：figures-evidence）
产出：`docs/report-build/05-figures.md` + `docs/report-build/figures/*.png`。只写：

- `## D1 图表清单与成图`：用 Python 3 + Pillow（无 matplotlib）绘制以下 PNG，白底、宽约 2000px、中文字体用 `/System/Library/Fonts/STHeiti Medium.ttc`（每张图另附一行元数据：`FIG|fig-xxx|建议图题|一句话说明`）：
  - `fig-architecture` — 系统总体架构（macOS 管理端 / 服务端 / 网页用户端 / 道闸终端 / 命令行用户端 + 核心业务 + SQLite + Python 识牌）
  - `fig-flow-main` — 系统主业务流程（启动→入场→分配→在场→预约/缴费→离场→结算；异常：满场、断网补报、爽约）
  - `fig-modules` — 功能模块图（管理端 / 服务端 / 网页用户端三列 + 核心公共模块）
  - `fig-db-er` — 12 张 SQLite 表关系图（按 布局/停车/预约/支付/账号/审计 分组，标主外键）
  - `fig-remote-seq` — 远程模式时序图（macOS 端 ↔ REST/TCP ↔ ParkingService ↔ SQLite ↔ WebSocket 推送）
  - `fig-payment-flow` — 支付订单状态机（created→paid→closed/refunded，含超时关闭、支付成功离场）
- `## D2 测试证据表`：CTest 12 项（名称/类型/覆盖点/是否默认启用）、Python 单测、e2e REST 用例清单（从 `tests/e2e_rest.sh` 提取）、LPR 性能/精度数据表（引 `docs/lpr-performance.md`、`docs/lpr-benchmark-*.json`，注明口径）。
- `## D3 运行截图（可选，尽力而为）`：尝试真实截图并保存到 `docs/report-build/figures/screenshots/`：
  - 网页端：启动 `build/server-release/apps/server/smartpark_server`（临时库放 `/tmp`，端口用 19080/19081/19527，`--web-root apps/webclient`），用 Chrome headless `--screenshot` 截首页；能截到有效页面再交付，否则放弃。
  - macOS 端：仅当能通过 `screencapture`/Swift CGWindow 截到 App 窗口时交付；不要强行操作。失败就写“未取得截图”，不要伪造。
  - 结束时必须 kill 自己启动的进程，并确认没有往仓库写数据库/日志。截图成功后另附 `FIG|fig-web-shot|...` / `FIG|fig-macos-shot|...` 元数据行。
- `## D4 事实核对清单`：对 OUTLINE 第 4 节数字与表格逐项核对，列出差异（如发现旧文档口径过期，以源码/最新文档为准）。

### Lead
- `01-overall.md`：第一章全部、2.1、2.3、4.1/4.2 导语、设计总结、参考文献、附录代码选摘。
- 模板封面/任务书/目录/评语页、Markdown→DOCX 构建脚本、图表插入与编号、全文一致性审校、最终交付。

## 3. 写作规范（所有协同者必须遵守）

1. **语言**：中文课程设计报告书面语，使用“本系统／本组／本设计”，不要口语、不要营销腔、不要“我们团队非常优秀”之类空话。
2. **事实必须真实可核验**：类名、文件名、函数名、接口、端口、表名、数值都要能在仓库中找到。引用源码时写 `文件路径:行号`；不确定的内容标 `【待核】`，宁缺毋造。禁止编造测试数据、截图和性能数字。
3. **结构**：只使用 `## `（二级）与 `### `（三级）标题，标题文字与编号必须与第 2 节给出的一字不差；不要在文件开头写 `# ` 总标题。正文每段 2–5 句，避免超长段。
4. **表格**：Markdown 管道表，前后空行；列数 ≤ 6；表头简洁。
5. **列表**：`- ` 或 `1. `，前留空行。每节最多 1 个列表。
6. **代码**：正文中仅在必要时贴 ≤20 行代码（```cpp / ```bash / ```json 围栏），大段源码放附录建议。
7. **图**：**不要插入图片**。在需要图的位置单独一行放占位符 `[[FIG:fig-xxx]]`，只允许第 2 节出现的 id；图题、编号、来源由 Lead 统一加。
8. **交叉引用**：引用其他章节写“见 x.x 节”，不要重复展开别人负责的内容。
9. **术语统一**：管理端=macOS 管理端；服务端=统一服务端（进程名 `smartpark_server`）；网页端=网页用户端；道闸终端 `smartpark_gate`；命令行用户端 `smartpark_user`；识牌=图片车牌识别。
10. **数字口径**：引用性能/精度数据必须写明数据集与统计口径；同一数字全文只用一种口径。
11. **篇幅**：按第 2 节区间写足；不要用空话凑字数，也不要把整篇 repo 文档粘贴过来。

## 4. 已验证事实清单（直接引用；如需更多数字请自行统计并注明命令）

**技术栈与构建**
- C++17；CMake ≥ 3.21 + Ninja；Qt 6.8.3，仅用 Core / Sql / Network / HttpServer / WebSockets 五个模块（无 Qt Widgets、无 QML）。
- macOS 管理端为原生 AppKit + Objective-C++，`apps/macos` 下 23 个 `.mm` 文件；核心与网络代码在 `src/`。
- Python 3.12（uv 管理两套环境）：YOLO 检测 + PP-OCRv5 识别，仅用于识牌推理侧。
- 构建预设：`macos-debug`（build/macos，含管理端/服务端/终端/测试）、`server-debug`（build/server）、`server-release`（build/server-release，Release 服务端+终端，无测试）。

**规模**
- `src/`（核心+网络）共 12634 行；`apps/webclient/app.js` 1276 行；项目 C++/ObjC++ 合计约 2.5 万行（不含 third_party）。
- 测试：CTest 注册 12 项——`smartpark_core_tests`、`smartpark_background_planning_tests`、`smartpark_gate_replay_tests`、`smartpark_lpr_concurrency_tests`、`smartpark_macos_map_tests`、`smartpark_macos_remote_tests`、`smartpark_rest_e2e`（e2e 标签）、`smartpark_lpr_script_tests`、`smartpark_lpr_resident_tests`、`smartpark_plate_geometry_tests`、`smartpark_plate_dataset_tests`、`smartpark_plate_color_tests`；`tests/*.cpp|*.mm` 中 `expect(` 断言 784 处；`tests/test_*.py` 中 `def test_` 75 个。
- SQLite 共 12 张表：`parking_spots`、`parking_records`、`layout_snapshot`、`bookings`、`reservations`、`deposit_payments`、`users`、`auth_tokens`、`audit_logs`、`payment_orders`、`site_tickets`、`user_plates`。

**运行参数**
- 端口：TCP 9527、HTTP 18080、WebSocket 18081（程序默认 8080/8081，部署示例显式用 18080/18081）。
- 演示账号：`admin`、`gate`、`user`，初始口令均 `smartpark`（仅新库初始化）。
- 场地：三塔楼地库图纸 166.5 m × 85 m、330 个车位、7 个分区、2 进 2 出、9 处核心筒/楼梯井障碍、栅格 0.5 m；s1 部署用六层 75 车位布局（`--layout` 决定）。
- REST 端点共 34 个（见 `docs/rest-api.md` 第 4 节），Bearer token，access token 2 小时，登录连续失败 5 次锁定 10 分钟；`/api/v1/lpr/recognize` 上传图片识牌。
- TCP：4 字节大端长度前缀 + UTF-8 JSON；协议版本 1；单帧上限 9 MiB；单图上限 6 MiB。
- s1 部署：`10.108.17.55`，TCP/HTTP/WS = 9527/18080/18081，数据库 `~/smartpark-data/smartpark.db`，构建目录 `~/sp-headless/build-server`。

**macOS 管理端**
- 七个页面：仪表盘、停车地图、车辆作业、车位占用、预约管理、停车记录、设置；另有登录/注册窗口、侧边栏导航。
- 本地模式直接调用同一套 C++ 核心与本机数据库；远程模式经 `ParkingDataSource`/`RemoteDataSource` 走 TCP 连接服务端，事件推送刷新界面。
- 能力差异（README 表格）：远程模式下布局编辑、分配策略切换、应急入场、在场车型更正不可用；图片识牌在远程模式上传服务端执行。

**网页用户端**
- 原生 HTML/CSS/JavaScript 单页应用（`apps/webclient/index.html` + `app.js` + `style.css`），无需 npm 构建。
- 功能：扫码进入、注册/登录、车牌绑定、余位查询、时段预约/取消、模拟缴费（含二维码与超时关闭）、停车记录、到场引导、反向寻车。
- 服务端同一进程伺服静态资源，`--web-root apps/webclient`。

**识牌与部署素材**
- 识牌流程/训练/评测：`docs/lpr.md`、`docs/lpr-performance.md`、`docs/lpr-benchmark-*.json|md`、`scripts/recognize_plate.py`。
- 部署：`docs/deploy-server.md`；协议：`docs/tcp-protocol.md`、`docs/rest-api.md`。
- 全项目总体介绍（最重要素材）：`docs/系统总体介绍.md`；答辩文字稿（可参考表述）：`答辩PPT文字稿.md`；总览：`README.md`。

## 5. 协作与产出规范

- 每个协同者只写自己任务范围内的文件；**不要改别人的文件**，不要 commit，不要动模板/源码/数据库/build 产物。
- 工作完成后：先在共享任务板把自己的任务标为完成，再 `send_message` 给 lead，内容包括：产出文件路径、行数/字数、覆盖章节清单、事实核对结果、未决问题（1–5 条）。
- 若发现 OUTLINE 与仓库事实冲突，以仓库为准，并在消息中说明；不要静默改口径。
- 报告中所有标题编号由 Lead 最终统一核对；协同者按本文件编号写即可。

## 6. 文档生成约定（Lead 用，供协同者了解排版结果）

- 页边距 2.54 cm（上下）/ 3.18 cm（左右）；A4。
- 正文：宋体小四（12 pt），1.5 倍行距，首行缩进 2 字符；西文 Times New Roman。
- 章标题：黑体 18 pt 居中；节标题：黑体 14 pt；小节标题：黑体 12 pt。
- 表格：小五/五号宋体，带浅灰表头；图：居中，图题 10.5 pt 宋体“图 x-y 标题”。
- 目录使用 Word 域，打开文档后按 F9/右键“更新域”即可生成页码。
