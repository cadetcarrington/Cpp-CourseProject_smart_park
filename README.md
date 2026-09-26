# SmartPark

SmartPark 是一个基于 C++ 和 Qt 的智能停车场管理系统课程项目。项目目标是逐步构建一个包含停车业务、可视化管理、数据持久化、客户端与服务端通信，以及车牌识别能力的完整软件工程系统。

## 系统规划

最终系统由一个统一的 SmartPark Server 和多个终端组成：

```text
                   SmartPark Server
                         |
            +------------+------------+
            |            |            |
            v            v            v
      Admin Client   Gate Terminal  User Client
       管理员端        出入口端        用户端
```

服务器将负责停车业务、车位状态、收费、车辆记录、用户与预约管理、数据库访问及网络通信。管理员端用于管理与可视化，出入口端用于车辆入场和离场处理。

## 当前范围：SmartPark 0.7

当前已完成：

- C++17、CMake 与 Qt 6 Widgets 的基础工程配置。
- `Vehicle`、`ParkingSpot`、`ParkingLayout`、`ParkingRecord` 核心模型。
- 独立的 `SpotAllocator`：`ParkingService` 只负责状态机，选位、评分和路径规划从业务层拆出。
- 默认 60 车位、3 个矩形分区的自动分配服务（CLI 内置布局）。
- Admin GUI 默认使用 6 层车库建筑图布局：58m×42.4m、75 个车位、南北/东西向混合停放、双入口单出口；电气室/设备用房/蓄电池室/水箱间已改为停车位，仅两侧楼梯间不可通行。
- 用户自定义多矩形停车场布局，支持不同车位长宽、通道位置（left/right/up/down）、车位类型、障碍物和多个出入口。
- 基于 0.5 米栅格的 A* / 一次搜索到多目标 Dijkstra，自动避开车位障碍并生成入口/出口路线。
- 拥堵写入 A* 边权，同时保留附近占用数作为车位评分项。
- `WeightedCost` 默认策略和 `Nearest` 对照策略。
- 预留状态机：`Available -> Reserved(ttl) -> Occupied -> Available`。
- CLI 自动演示与 Qt GUI 实时车位图、路线绘制、布局编辑和策略切换。
- `ParkingService::enter()` / `leave()` / `reserve()` 入离场与预留流程。
- `DatabaseManager` 与 `ParkingRepository`：SQLite 建表、入场/离场/预约持久化和重启恢复。
- `Persistence` 辅助类：统一管理数据库连接，CLI 与 Qt Admin GUI 默认接入 SQLite，跨重启恢复车位状态与停车记录。
- `BillingRule` 与 `BillingService`：免费时长、计费单元、首单元费用、后续单元费用和单次封顶；`ParkingService` 在离场/释放车位时自动计算费用并写入停车记录。
- CLI 支持 `--db <路径>` 指定数据库、`--reset` 清空数据库后演示。
- Qt Admin GUI 支持 `--db <路径>`，应用布局时若与数据库签名不一致会提示并可选重置数据库。
- `Booking` 模型与预约 API：远程预约、近一周时间窗、预付定金、到场确认、取消、爽约扣定金与预期路线，持久化到 `bookings` 表并跨重启恢复。
- 预约可操作化：CLI 支持 `--book` / `--checkin` / `--cancel` / `--bookings` / `--expire-bookings` 预约命令与 `--at` / `--in` 基准时间参数（指定命令时只执行预约流程，不运行自动演示）；Qt Admin GUI 新增预约面板（到场时间选择、预约 / 到场确认 / 取消按钮、预约记录表格与定金统计），车位图同步显示预约预期路线。
- 远程时间段预约核心（`Reservation`，SmartPark 0.7）：未来 7 天时间校验、时间段冲突检查（同一车位重叠窗口拒绝、同一车牌仅一个未结束订单）、`FakePaymentGateway` 定金模拟支付、延迟锁位（到场窗口前 30 分钟才短时锁定物理车位）、到场转停车并定金抵扣停车费、取消退定金与爽约没收；订单与定金流水持久化到 `reservations` / `deposit_payments` 表并跨重启恢复。

CLI 启动时打印默认计费规则，批量演示入场/离场并汇总累计停车费；同时演示预约流程（远程预约返回预期路线、到场确认、爽约没收定金）与时段预约流程（创建收定金、延迟锁位、到场转预付、离场抵扣、爽约没收），并汇总待结算与爽约没收定金。

下一步优先实现 SmartPark 0.7 的 TCP 协议与服务端（先写 `docs/tcp-protocol.md`，再用 `QTcpServer` 实现登录、心跳、预约查询/创建/取消和入场/离场事件），随后接入用户端与 Gate 终端；之后把 `BillingRule` / `ReservationRule` 扩展为 GUI 可配置、可持久化。

尚未接入 TCP 通信、真实 LPR、多线程、用户端或统计图表。调研来源与明确不做的方案见 `docs/research-sources.md`。

## 计费规则

当前内置默认规则：

| 项目 | 默认值 |
| --- | --- |
| 免费时长 | 30 分钟 |
| 计费单元 | 30 分钟 |
| 首单元费用 | 5 元 |
| 后续每单元费用 | 5 元 |
| 单次封顶 | 100 元 |

计费采用“免费时长后向上取整到计费单元”的方式。`ParkingService::leave()` 与 `ParkingService::release()` 会先调用 `BillingService::calculateFee()`，再通过 `ParkingRepository::saveExit()` 在同一事务中写入费用、关闭停车记录并释放车位。

## 预约系统（Booking，已实现第一版）

预约系统是 SmartPark 的核心业务能力之一，支持用户远程预约车位、预付定金、到场后自动接入停车流程。与入口处的短时预留（`reserve()` + TTL）不同，预约面向“未来一段时间”的确定性车位锁定，并带有定金约束与爽约处理。

### 设计要点

- **远程预约**：用户无需到场，通过用户端（后续 TCP / User Client）提交预约。核心层由 `ParkingService` 提供预约 API，选位与路线复用 `SpotAllocator` / `GridPlanner`。
- **可预约时间范围**：仅允许预约“近一周”（默认 7 天）内的未来时间，超过时间范围的请求被拒绝。
- **预付定金**：预约成功后立即收取定金（默认 `20 元`），计入平台待结算资金。
- **到场确认**：在预约到场时间至宽限期（默认 `30 分钟`）内到场，将预留车位转为占用并生成停车记录；定金退回，停车费用在离场时按正常计费规则收取。
- **爽约扣定金**：超过到场宽限期仍未到场，判定为爽约（NoShow），没收定金并自动释放车位。
- **取消预约**：在预约到场时间之前取消，退回定金并释放车位。
- **预期路线**：预约成功即返回从入口到车位、以及从车位到出口的预期路线（含距离、转弯次数与入口/出口门选择），供预约者提前规划。

### 数据模型与状态机

```text
Booked ──到场(confirmBooking)──> CheckedIn ──> 停车记录 / 离场收费
  │
  ├── 超过宽限期(expireBookings)──> NoShow（扣定金，释放车位）
  └── 取消(cancelBooking)──> Cancelled（退定金，释放车位）

车位角度：Available -> Reserved(arrivalDeadline) -> Occupied -> Available
```

预约记录单独建模为 `Booking`（编号、车牌、车位、创建时间、到场时间、定金、状态），持久化到 `bookings` 表并跨重启恢复；爽约/取消的定金流水可独立统计（`forfeitedDeposits`）。

> 第一版从下单起即把车位锁为 `Reserved` 直到到场截止时间，并以“到场退定金”作为简化语义；完整的远程时间段预约（时间段冲突检查、延迟锁位、定金抵扣停车费、模拟支付）见下节。

## 数据分析接口（本地小模型 + 预留远程 API）

`AnalyticsEngine`（`src/core/service/AnalyticsEngine.h/.cpp`）对运营数据给出中文结论：从 `ParkingService` 重建近 72 小时逐时占用率序列，由本地小模型（普通最小二乘线性回归，`local-ols-v1`）拟合趋势并外推未来 6 小时占用率，再用规则引擎生成结构化发现（预测/高峰/收入/分区/预约/数据质量）与建议。数据不足时如实报告置信度受限，不编造结论。CLI 通过 `--analyze` 输出报告。

`RemoteAnalystClient`（`src/core/service/RemoteAnalystClient.h/.cpp`）是预留的远程分析接口：把聚合运营快照（只含统计指标，不含车牌等隐私数据）组装为 OpenAI 兼容的 `chat/completions` 请求，输出与本地引擎相同的 `AnalysisReport` 结构，两者可互换。传输层通过 `setTransport()` 注入——网络层（P1）落地后接入 HTTP 实现即可启用，核心代码无需改动；测试用注入的假传输验证了请求组装与结论解析。

### 模型选型建议

| 阶段 | 方案 | 说明 |
| --- | --- | --- |
| 当前（已实现） | 本地 OLS 线性回归 + 规则引擎 | 零依赖、可解释、C++ 内置训练；适合当前数据量 |
| 本地进阶 | LightGBM/XGBoost 导出 ONNX，ONNX Runtime 推理 | 特征：星期/小时/预约量/天气；与 LPR 的 ONNX 部署路线统一 |
| 远程 API | OpenAI 兼容接口（GLM-4-Flash、DeepSeek 等） | 只上传聚合指标，不传车牌隐私；接口已预留 |
| 不建议 | LSTM/Prophet 时序模型、真实支付风控模型 | 数据量不足，收益不抵复杂度 |

## 远程预约系统（SmartPark 0.7 完整目标，核心已实现）

预约系统是 SmartPark 0.7 的核心功能。用户端通过 SmartPark Server 远程提交车牌、车辆类型、预计到达时间和预计离开时间，只能预约从当前时刻起 **7 天内** 的时间段。服务端完成时间校验、空闲车位匹配、时间段冲突检查、定金支付确认和路线规划后，才生成有效预约。核心领域逻辑已在 `Reservation` / `ReservationService` 中实现并通过单元测试，TCP 接口在下一阶段开放。

预约主流程：

```text
User Client
    |
    | 选择未来 7 天内的到达/离开时间
    v
SmartPark Server -> 时间段冲突检查 -> SpotAllocator -> 预期入口路线
    |
    | 预付定金成功
    v
Confirmed Reservation
    |
    +-- 按时到达 -> Check-in -> 定金抵扣停车费 -> Completed
    +-- 主动取消 -> 按取消规则退还定金 -> Cancelled
    +-- 超过到场宽限期仍未到达 -> 扣除定金 -> NoShow
```

默认业务规则如下，后续统一做成可配置项：

| 规则 | 默认值 |
| --- | --- |
| 可预约范围 | `[当前时间, 当前时间 + 7 天]` |
| 最短提前时间 | 30 分钟 |
| 最短预约时长 | 30 分钟 |
| 到场宽限期 | 预约到达时间后 30 分钟 |
| 定金 | 20 元 |
| 按时到达 | 定金转为停车费预付款，离场结算时抵扣 |
| 提前取消 | 预约开始前取消，原路退还定金 |
| 爽约 | 宽限期结束仍未入场，预约转为 `NoShow` 并扣除全部定金 |

预约订单使用独立状态机，不直接复用车位的实时状态：

```text
PendingPayment -> Confirmed -> CheckedIn -> Completed
       |              |             |
       v              +-----------> Cancelled
    Expired            |
                      v
                    NoShow
```

计划新增 `Reservation`、`ReservationRule`、`DepositPayment` 和 `ReservationService`。`Reservation` 至少持久化预约编号、车牌、车辆类型、车位、预约起止时间、宽限期截止时间、状态、定金金额、支付状态以及预期路线。数据库对同一车位的重叠有效时间段执行冲突检查，对同一车牌限制只能存在一个未结束预约；创建预约、确认定金和占用预约时段必须使用事务，防止并发重复预订。

以上核心已实现（2026-09-25）：`Reservation` / `ReservationRule` / `DepositPayment` 持久化到 `reservations` 与 `deposit_payments` 表；同一车牌的未结束订单由部分唯一索引 `idx_reservations_open_plate` 兜底；创建、到场与离场结算均在单个 SQLite 事务内完成。预约成功响应包含选定入口、车位编号、预计距离、预计转弯数和路线点集（序列化存入 `route` 列），供用户端绘制预期路线；该路线是预约时基于布局和当时占用情况生成的快照，车辆实际到达后，系统会按照实时拥堵重新规划，并把最新路线发送给用户端或 Gate Terminal。

`FakePaymentGateway` 在本地完成支付成功、失败、退款和爽约扣款的确定性测试，只保存模拟交易号，不接入真实支付平台。失败路径通过测试钩子显式注入；后续真实支付必须由服务端验证支付回调和幂等键，客户端提交的“已支付”状态不能直接采信。

延迟锁位语义：预约从下单起**不**占用实体车位；`ReservationService::sweep()` 在进入到场窗口（开始时间前 `lockLeadTime`，默认 30 分钟）时才把物理车位短时锁定到宽限期截止。同一车位因此可以接受互不重叠的时间段预约，也不会被一周后的订单提前占满。到场确认窗口为 `[开始时间 - 锁位提前量, 宽限期截止]`，车牌匹配即把预约转为 `CheckedIn` 并占用预约车位；`ParkingService::enter()` 对匹配车牌自动完成该转换。离场时定金按 `min(定金, 应收费用)` 抵扣，余额不退。

## 当前进度

截至 2026-09-26，项目处于 **SmartPark 0.7**：预约体系（`Booking` + `Reservation`）、数据分析层与管理端 GUI（登录注册/毛玻璃主题/原生能力）均已落地，TCP 服务端尚未开始：

- 已完成核心模型、60 车位自动分配、自定义多矩形布局（含南北向车位与机房障碍）、栅格 A* / Dijkstra 路线、拥堵边权、预留 TTL、CLI 与 Qt GUI。
- SQLite 持久化已接入 CLI 与 Admin GUI，支持跨重启恢复车位状态、预约和停车记录。
- 收费服务已接入 `ParkingService`、CLI 与 Admin GUI；离场费用随停车记录持久化。
- CLI 默认持久化可重复运行，连续运行至 60/60 满场后仍稳定输出 `RESULT: PASS`；75 车位图纸布局 `data/garage-6f.txt` 验证通过。
- 预约第一版（`Booking`）：远程预约、近一周时间窗、预付定金、到场退回定金、爽约没收定金与预期路线，持久化到 `bookings` 表并跨重启恢复。
- 远程时间段预约核心（`Reservation`）已完成：未来 7 天时间校验、最短提前 30 分钟、最短时长 30 分钟、时间段冲突检查、同一车牌唯一未结束订单、`FakePaymentGateway` 定金模拟支付（含失败注入）、延迟锁位（开始前 30 分钟短时锁）、到场转停车与定金抵扣、取消退定金、爽约没收；订单与流水持久化并跨重启恢复。8 个专项单元测试覆盖模型状态机、冲突、锁位、抵扣、爽约、取消、支付失败与重启恢复。
- 2026-09-23 增量：Admin GUI 登录界面与产品化界面重构、`ParkingInsightEngine` 运营洞察（占用预测/风险提示）、`ChartWidgets` 统计图表、分配算法分区压力均衡项。
- 2026-09-25~26 增量：
  - 界面：浅色企业风主题（低饱和蓝）+ 主界面毛玻璃模式（整窗高斯模糊光斑 + 半透明面板，`SMARTPARK_NO_GLASS=1` 回退）、登录/注册重做（`UserStore` 盐化哈希口令、失败锁定、密码可见切换）。
  - 创新 Top-5（10 角度子代理评分选拔）：应急生命通道（出口最近车位 + 满场让位）、无障碍关怀预约（免定金/宽限翻倍/无障碍车位限定/少转弯路线）、哈希链防篡改审计日志（`AuditLogService` + `verifyChain`）、反向寻车（行人栅格步行路线）、剧本式一键演示（`DemoDirector` 16 步）。
  - 数据分析：`AnalyticsEngine` 本地 OLS 占用率预测 + 规则结论（CLI `--analyze`），`RemoteAnalystClient` 预留 OpenAI 兼容远程分析接口。
  - macOS 原生：菜单栏余位图标、通知中心、中文语音播报、PDF 报告导出、NSURLSession 远程分析传输（`MacSystemBridge`）。
  - 算法：分区均衡升级为负载水位填充（跨分区低负载无条件优先，同档内按距离/拥堵/类型），车库布局 38 辆实测 13 分区负载 25%~62% 均衡。

尚未完成：TCP Server 与协议文档、Gate Terminal、用户端、真实 LPR；`ReservationRule`/计费规则配置化与真实支付在后续阶段接入。

## 后续发展路线

按调研结论推进，优先级从高到低：

1. **P0 — 预约领域核心**：在第一版 `Booking` 基础上补齐完整语义——预约模型、未来 7 天时间校验、时间段冲突、定金模拟支付、取消/到场/爽约状态机、SQLite 事务与单元测试。
2. **P1 — TCP 协议与服务端**：先写 `docs/tcp-protocol.md`，再用 `QTcpServer` 实现登录、心跳、远程预约查询/创建/取消和入场/离场事件。
3. **P2 — User Client + Gate Terminal + Fake LPR**：用户端展示预约与预期路线；Gate 先以假识别打通预约核销和出入口，再补齐断线缓存和补报。
4. **P3 — 双路线真实 LPR**：先接入 HyperLPR3 作为可运行基线，再训练 `YOLO11m + PP-OCRv5` 中国车牌专用模型；使用 CCPD 与 SmartPark 场景数据做统一评测。
5. **P4 — 规则配置化、真实支付与图表**：预约核心只使用模拟支付；真实支付、`BillingRule` / `ReservationRule` 配置化和统计图表在网络链路稳定后实现。

明确不做：EasyPR、纯云端计费、微信小程序、Go 微服务，以及把 LPR 放进 `ParkingService`。

## 技术栈

- C++17
- Qt 6 Widgets
- CMake
- SQLite 与 Qt SQL
- QTcpServer 与 QTcpSocket（后续）
- OpenCV 4
- HyperLPR3（LPR 基线）
- YOLO11m + PP-OCRv5（中国车牌专用训练路线）
- ONNX / ONNX Runtime（自训练模型的 C++ 部署目标）
- QThread、std::thread 与 STL
- Qt Charts（后期）

## 目录结构

```text
.
├── apps/
│   ├── admin/       # 管理员端 Qt GUI
│   │   ├── main.cpp / MainWindow.*      # 入口与主窗口（总览/车位图/作业/预约/记录）
│   │   ├── LoginDialog.* / RegisterDialog.* # 登录与注册（SQLite 账号验证）
│   │   ├── UserStore.*                  # users 表：盐化哈希口令、注册/验证
│   │   ├── Theme.h                      # 视觉主题：浅色/毛玻璃样式表与配色
│   │   ├── ChartWidgets.h               # 环形/折线/条形统计图表
│   │   ├── NativeEffects.*              # macOS 原生毛玻璃（实验路径）
│   │   └── MacSystemBridge.*            # macOS 原生桥接：菜单栏/通知/语音/PDF/HTTP
│   ├── cli/         # 终端程序：自动演示、预约/时段预约/分析/应急/寻车/审计命令
│   ├── server/      # SmartPark 服务端入口（预留，P1 TCP）
│   └── gate/        # 出入口终端入口（预留）
├── docs/            # 调研来源与路线说明
├── data/            # 示例布局（garage-6f.txt 6 层车库 75 位图纸）
├── src/
│   ├── core/
│   │   ├── model/        # Geometry、Vehicle、ParkingSpot、ParkingLayout、
│   │   │                 # ParkingRecord、Booking、Reservation
│   │   ├── persistence/  # DatabaseManager、ParkingRepository、Persistence
│   │   ├── service/      # GridPlanner、SpotAllocator、ParkingService、Billing、
│   │   │                 # ReservationService、FakePaymentGateway、AnalyticsEngine、
│   │   │                 # RemoteAnalystClient、AuditLogService、DemoDirector、
│   │   │                 # ParkingInsightEngine
│   │   └── util/         # TimeUtil 时间范围与安全转换
│   ├── database/    # 数据库连接与仓储层（预留目录）
│   ├── network/     # TCP 协议与通信实现（预留，P1）
│   └── lpr/         # 车牌识别统一接口及两种后端实现（预留）
├── scripts/         # 构建/运行脚本（build-admin、run-admin 等）
├── resources/       # 图标/样式/图片资源
├── sql/             # 数据库建表与初始化脚本
├── tests/           # core_model_tests、admin_main_window_tests + CTest 配置
├── training/        # LPR 训练脚本（检测/识别/评测）
├── CMakeLists.txt   # 顶层构建：C++17、CTest、可选 Qt 组件
├── CMakePresets.json# cli / qt 构建预设
├── README.md        # 项目说明（本文档）
└── handoff.md       # 会话交接说明
```

## 开发里程碑

1. ✅ 工程跑起来：完成 CMake、Qt 6、C++17 和主窗口。
2. ✅ 纯 C++ 停车核心：实现停车位、车辆、停车记录和入场/离场流程。
3. ✅ 停车场 GUI：实时展示车位状态、自动分配结果和行驶路线。
4. ✅ 分配器重构：独立选位算法、拥堵边权、预留 TTL、车位类型和多出入口。
5. ✅ SQLite 持久化：核心层已完成，CLI/GUI 已接入并支持重启恢复。
6. ✅ 收费系统：根据停车时长、免费时长、计费单元和单次封顶计算并持久化费用，CLI/GUI 已展示。
7. ✅ 预约系统（第一版 `Booking`）：远程预约、近一周时间窗、预付定金、爽约扣定金与预期路线。
8. ✅ 预约完整版核心（`Reservation`）：未来 7 天时间段预约、时间段冲突检查、定金模拟支付、延迟锁位与定金抵扣（GUI 面板与 TCP 接口随后续里程碑接入）。
9. ⬜ 服务端：以 TCP 建立管理员端、用户端与服务端架构，开放预约和停车事件接口。
10. ⬜ 用户端与出入口终端：用户端提交预约并查看路线；Gate 手动输入车牌并通过服务端核销预约。
11. ⬜ 车牌识别：实现 HyperLPR3 基线，并完成 YOLO11m + PP-OCRv5 中国车牌专用模型训练、评测与 C++ 部署。

## 最近工作记录

2026-09-26 分区均衡改为水位填充（用户实测反馈低占用时不均衡）：

- 问题：二次渐进曲线（负载²×等效步行米数）在低占用时压力项极小（第 2 辆车仅约 4 米代价），前几辆车必然堆满最近分区——实测 4 辆车时 A2 区 50%、F 区 25%、其余 0%。
- 修正：`SpotAllocator::propose()` 改为**负载水位优先**——跨分区时"计入本车后负载占比"更低的分区无条件优先，同水位档内才由距离/拥堵/类型评分决定。仅在 WeightedCost 且 `zonePressure` 权重 > 0 时启用（Nearest 策略与显式关闭均衡时保持纯距离行为）。
- 效果：车库布局（14 分区 75 位）连续入场 38 辆，分布 A1/A2 3/B 4/C 5/D 5/E 7/F 2/G 2/H 3/I 2/J 2/L 1/M 1，各分区负载比 25%~62%，无空置分区；首辆车仍就近入场。
- `zonePressureCost` 保留在评分明细中用于展示，跨分区决策由水位规则主导。

2026-09-25 主界面毛玻璃模式：

- `Theme.h` 新增 `glassMainWindowStyleSheet()`：主窗口整体毛玻璃化——`MainWindow::paintEvent` 绘制整窗高斯模糊光斑背景（复用 `auroraBackdrop`，按尺寸缓存），侧边栏/顶栏/菜单栏/工具栏/状态栏/卡片全部改为半透明玻璃材质（rgba 白 0.59~0.9），光斑从玻璃下透出；表格保持 0.92 不透明度保证可读性。
- 回退开关：`SMARTPARK_NO_GLASS=1` 使用 `solidMainWindowStyleSheet()` 纯色主题（原样式保留为 `solidMainWindowStyleSheet`）。
- 离屏快照验证：光斑从侧边栏与卡片间隙透出、图例/表格/图表完整可读；此前修复的滚动容器与刻度自适应保持生效。

2026-09-25 macOS 原生 API 深度接入（按子代理调研报告 Top-5）：

- 新增统一 ObjC++ 桥接层 `apps/admin/MacSystemBridge.h/.mm`（仅主目标编译，宏 `SMARTPARK_MACOS_NATIVE`；非 macOS/测试目标为内联空实现），链接 UserNotifications、AVFoundation、PDFKit、AppKit：
  1. **NSStatusItem 菜单栏余位图标**：常驻菜单栏显示"SmartPark 余位 N"，随 `refreshDashboard` 实时刷新（`SMARTPARK_NO_MENU_BAR=1` 可关闭）。
  2. **UNUserNotificationCenter 本地通知**：应急车辆入场即推送通知中心横幅；首次调用触发系统授权弹窗，前台展示需 delegate 回调（已实现）。
  3. **AVSpeechSynthesizer 中文语音播报**：应急入场时朗读事件文本（zh-CN，婷婷语音，缺失时回退默认语音）。
  4. **NSURLSession 同步 POST**：填入 `RemoteAnalystClient` 预留的 Transport 接缝——设置 `SMARTPARK_ANALYST_ENDPOINT`（+ `SMARTPARK_ANALYST_API_KEY`）后，"数据分析报告"对话框可请求远程 LLM 生成自然语言结论（子线程 + QTimer 轮询回传，20 秒超时）。
  5. **PDFKit 报告导出 + NSWorkspace**：分析对话框"导出 PDF"生成 A4 报告（含标题/作者元数据）到桌面并自动在 Finder 中定位。
- 加固：`--smoke-test` 未显式指定 `--db` 时强制使用临时数据库，杜绝恢复失败模态框卡住自动化（曾使冒烟测试挂起的根因）。
- 报告确认 Vision.framework 通用 OCR 对中国车牌（小尺寸/斜角/字符集约束）不可靠，LPR 仍按 README 的 YOLO+PP-OCRv5 专用模型路线推进。

2026-09-25 十角度创新点评分与 Top-5 实现：

- 由 10 个独立顾问从运营效率、用户体验、预测分析、安全隐私、架构工程、商业模式、IoT 硬件模拟、绿色能源、答辩演示、社会价值十个角度各提 3-4 个创新点并按「创新性 40% + 可实现性 30% + 价值 30%」打 1-10 分，共 40 个创新点。汇总去重后选前 5 实现：
  1. **应急生命通道模式**（9.0）：`ParkingService::emergencyEnter()`——应急车优先分配出口距离最近的车位（专用应急权重），满场时出口最近的占用车自动结算让位；CLI `--emergency`、GUI「车辆作业」页红色横幅 + 一键入场按钮；应急/让位动作写入审计链。
  2. **无障碍关怀预约**（8.5）：`ReservationService::create(..., accessible)`——免定金、到场宽限翻倍（60 分钟）、仅限无障碍车位、转向权重 ×4 的少转弯路线；CLI `--reserve --accessible`；`reservations` 表新增 `accessible` 列跨重启保留。
  3. **哈希链防篡改审计日志**（8.5）：`AuditLogService`——append-only `audit_logs` 表，每条 hash = SHA-256(prev_hash + 内容)，`verifyChain()` 检测删改并定位断链行；入场/离场/预约创建/取消/到场/爽约/应急全量埋点；CLI `--audit` / `--audit-verify`。
  4. **反向寻车**（8.5，两角度重复提出合并）：`ParkingService::findCar()`——行人栅格规划器（车位可穿越、机房楼梯仍阻挡）从最近出入口到车位步行路线；CLI `--find`。
  5. **剧本式一键演示**（8.5）：`DemoDirector`——16 步虚拟时钟脚本（入场→分区再平衡→无障碍预约→爽约→到场→离场计费→分析结论），车牌按次唯一化可重复运行；CLI `--demo-script`；GUI「快捷操作」新增数据分析报告对话框。
- 其余 35 个创新点（有序充电调度 8.0、道闸仿真 8.0、LED 引导屏 8.0、峰谷动态定价沙盘 8.0、预测式选位 8.0、异常检测 8.0、RBAC 8.0、事件总线 8.0、潮汐通道仿真 8.0、预缴费倒计时 8.0 等）已记录评分备选。
- 新增 6 个专项测试：审计链校验/篡改检测/埋点集成、应急让位、无障碍权益与重启保留、寻车路线、演示脚本；全量测试通过。

2026-09-25 分区压力算法重做与界面去概念稿化：

- 分区压力算法重写（`SpotAllocator`）：旧实现 `6.0 × 放置前负载`（满分 6 分）相对路径项（米）几乎不起作用，车辆把最近分区塞满才外溢。新算法把压力换算成与路径同量纲的等效步行米数——`zonePressure(默认 0.8) × 场地对角线 × 计入本车后负载占比²`，负载越接近满区边际代价越高。默认布局连续入场 30 辆的分布从「A 区塞满 20、B 区 10、C 区 0」变为 **A 13 / B 10 / C 7**，近门优先的商业逻辑保留、无分区被塞满或空置。新增分布回归测试 `testZonePressureSpreadsLoadAcrossZones`。
- 界面按真实企业软件调研重做（调研对象：FlashParking、SKIDATA、捷顺/ETCP、SpotHero、JustPark、Q-Park、Chase、Bank of America 等，共性结论：浅色主题、低饱和主色只用于主按钮/链接/选中态、高密度表格、动词短语按钮、无渐变撞色）：主题从深色「石墨+金」改为**白/浅灰底 + 低饱和企业蓝 #1E5AA8**，白底侧边栏蓝色选中态，表格细分隔线，状态 badge 浅底色块，图表灰网格；登录/注册页同步改浅色，原生毛玻璃切换为浅色材质（`UnderWindowBackground`）。
- 文案去营销味：按钮改为动词短语（“登录”、“注册新账号”），移除“实时运营控制台”“智能决策”“本地运营终端”等自我宣传式措辞，预测行改为“预测：60 分钟后占用率 X%（置信度 Y%）”。
- 代码注释同步精简：移除教科书式自我解释与变更历史式注释，只保留业务约束说明。

2026-09-25 管理端界面换装与登录/注册重做：

- 视觉主题从蓝色系（#1D4E89）整体更换为**曜石石墨 + 香槟金**：集中到 `apps/admin/Theme.h`（主窗口与认证对话框样式表、图表用色常量、自绘高斯光斑背景），`MainWindow`、`LoginDialog`、`RegisterDialog` 与 `ChartWidgets` 全部取值于该主题，不再散落色值。
- macOS 原生毛玻璃：新增 `apps/admin/NativeEffects.h/.mm`（Objective-C++ 调用 AppKit `NSVisualEffectView`，`BehindWindow` 混合模式对窗口背后内容做系统级高斯模糊），登录/注册窗口半透明透出毛玻璃；仅 `smartpark_admin` 主目标编译（`SMARTPARK_HAS_NATIVE_VIBRANCY` 宏 + AppKit framework），其他平台与测试目标为空实现。`SMARTPARK_NO_VIBRANCY=1` 可强制关闭。
- 非 macOS / 原生效果关闭时的回退：`Theme::auroraBackdrop()` 用 `QGraphicsBlurEffect` 对金色/青色/绛色光斑做真实高斯模糊后铺底，视觉对应毛玻璃。
- 登录逻辑完善（`LoginDialog` 重写）：认证改接 `UserStore`（SQLite `users` 表）；空账号/空密码分别提示并标红输入框；账号不存在与密码错误分开提示；连续 5 次密码错误锁定 30 秒（按钮倒计时）；密码可见切换（👁 动作）；记住账号；账号框回车跳密码框。
- 注册功能：新增 `RegisterDialog`（账号 + 密码 + 确认密码，实时校验、密码强度提示、重复账号检测），注册成功回填登录账号。新增 `UserStore`（`apps/admin/UserStore.h/.cpp`）：盐化 12000 轮迭代 SHA-256 口令摘要、常量时间比较、空库自动播种演示账号 admin/smartpark、登录时间戳记录。
- Admin 测试新增 3 项：`UserStore` 播种与登录验证（含重启持久化）、注册校验与重复拒绝、`LoginDialog` 交互（空字段提示 → 错误密码剩余次数 → 演示账号通过验证），并保存对话框离屏渲染快照。
- 注意：终端若未授予 macOS「屏幕录制」权限，`screencapture` 会省略其他应用的窗口（呈现为"窗口不可见"假象）；GUI 视觉验收请在真实屏幕确认。

2026-09-25 完成 SmartPark 0.7 远程时间段预约核心（`Reservation`）：

- 新增 `Reservation` / `ReservationRule` / `DepositPayment` 模型（`src/core/model/Reservation.h/.cpp`）：状态机 `PendingPayment -> Confirmed -> CheckedIn -> Completed`，另有 `Cancelled` / `NoShow` / `Expired`；定金结算状态 `Pending / Refunded / Forfeited / Applied`；预期路线快照可文本序列化并随订单持久化。
- 新增 `FakePaymentGateway`（`src/core/service/FakePaymentGateway.h/.cpp`）：确定性模拟定金收取/退回/没收，支持注入支付失败。
- 新增 `ReservationService`（`src/core/service/ReservationService.h/.cpp`）：未来 7 天校验、最短提前/最短时长校验、同车位时间段冲突检查、同车牌唯一未结束订单、延迟锁位 `sweep()`、到场确认（窗口 `[开始-30min, 宽限截止]`，`ParkingService::enter()` 自动车牌匹配）、取消退定金、爽约没收、离场定金抵扣（`prepare/apply/rollback` 三段式保证与 SQLite 事务一致）。
- `ParkingRepository` 新增 `reservations` / `deposit_payments` 表：订单、状态与定金流水持久化；部分唯一索引兜底同车牌唯一开放订单；`saveReservationOrder` / `saveReservationCheckIn` / `saveExitWithReservation` 等事务方法。
- CLI 可操作化：`--reserve`（`--duration` 时长）、`--arrive`、`--rsv-cancel`、`--reservations`、`--settle-reservations`；自动演示新增时段预约全流程（预约收定金 -> 延迟锁位 -> 到场转预付 -> 离场抵扣：3 小时应收 25 元实收 5 元 -> 爽约没收）。
- 新增 8 个单元测试：模型状态机与路线序列化、创建校验与冲突、延迟锁位/到场/抵扣、爽约与取消、支付失败、入场自动确认、跨重启恢复、模拟网关。
- 本轮验证（Mac，Qt 6.8.3）：`ctest` 4/4 通过（CLI 演示、CLI 预约、核心测试、Admin GUI 测试）；CLI 端到端验证预约->到场->取消->列表跨重启正常；`data/garage-6f.txt` 75 车位图纸 `RESULT: PASS`。
- s1 本轮不可达（10.108.17.55 连接超时），改动未提交未推送；`origin/main` 落后本地 2 个提交（`ac95d52` + 本轮）。

2026-09-14 把 6-1 电气室/设备用房/蓄电池室和 6-7/6-8 水箱间改成停车位：

- 图纸仍是 58.0m×42.4m、北墙双入口单出口；只保留东西两座楼梯间作为障碍。
- 新增 J/J2（原电气室，4 充电 + 2 无障碍）、把 H 扩成两列 6 个 VIP、L（原蓄电池室，4 无障碍）、M（原水箱间，3 充电）。
- 当前共 75 个车位：8 无障碍、15 充电、10 VIP、42 普通。三处文本保持同步：`ParkingLayout::garageDescription()`、`data/garage-6f.txt`、`my-layout.txt`。
- CLI 默认仍是 60 车位，不要改；验证图纸用 `./build/qt/apps/cli/smartpark_cli data/garage-6f.txt`。
- 接手说明见仓库根目录 `handoff.md`。

2026-09-14 Admin GUI 按 6 层车库建筑图落地车位规划：

- 布局引擎新增 `up` / `down`（`top` / `bottom`）通道方向：南北向车位沿 Y 停放，入口点在车位上/下方；原有 `left` / `right` 东西向车位保持不变。
- 新增 `obstacle x y 宽 高 [名称]`：机房、楼梯间等不可通行，路径规划与 GUI 同步避开。
- 按图纸尺寸编码 `ParkingLayout::garageLayout()` / `data/garage-6f.txt`：场地 58.0m×42.4m，北墙两入口一出口，中央岛式车位带 + 南侧 MQ2940 车位带 + 西侧零散车位。
- Admin GUI 默认加载该图纸，并按建筑图绘制轴线（6-1～6-8 / 6-E～6-A）、外墙出入口开口、机房斜线填充、按车位类型着色（普通/无障碍/充电/VIP）。自定义布局对话框支持 `up|down` 与 `obstacle`。CLI 仍默认 60 车位，可用 `./build/qt/apps/cli/smartpark_cli data/garage-6f.txt` 验证图纸布局。

2026-09-12 预约系统 CLI / GUI 可操作化：

- CLI 新增预约命令：`--book <车牌>`（`--in` / `--at` 指定到场时间，`--type` 指定车辆类型）、`--checkin <车牌>`、`--cancel <车牌>`、`--bookings`、`--expire-bookings`；带预约命令时跳过自动演示，并新增 `smartpark_cli_booking` CTest 用例（CMake 脚本驱动完整生命周期：预约 -> 到场确认 -> 重复确认应失败 -> 再预约 -> 取消 -> 列表）。
- Admin GUI 新增“车位预约”面板：到场时间 `QDateTimeEdit`（默认 60 分钟后，限当前时间至最多提前 7 天）、预约 / 到场确认 / 取消按钮、8 列预约记录表格（编号、车牌、车位、创建 / 到场 / 宽限截止时间、定金、状态）与定金统计（待结算 / 爽约没收）；预约与到场确认会把预期路线画到车位图。
- 本轮验证（`/opt/mamba/envs/smartpark`，`MAMBA_ROOT_PREFIX=/opt/mamba`）：CLI 与 Qt 构建通过，`cli-tests` / `qt-tests` 均为 3/3 通过；GUI offscreen 冒烟与预约面板交互测试（预约、到场确认退定金、取消退定金）全部通过。

2026-09-12 已同步 SmartPark 0.7 预约需求：

- 明确未来 7 天时间段预约、30 分钟到场宽限期、20 元模拟定金、到场抵扣和爽约扣款规则。
- 明确未来预约订单与实时 `Reserved(ttl)` 车位锁分层，避免长期预约提前占用实体车位。
- 明确预约成功返回预期路线快照，车辆到场后按照实时拥堵重新规划。

随后完成预约系统第一版（`Booking`）：

- 新增 `Booking` 模型与 `BookingPolicy`（定金、最多提前天数、到场宽限期），预约状态机为 `Booked -> CheckedIn / NoShow / Cancelled`。
- `ParkingRepository` 新增 `bookings` 表与 `saveBooking` / `saveBookingStatus` / `saveBookingCheckIn` / `loadBookings`，预约记录跨重启恢复。
- `ParkingService` 新增 `createBooking` / `confirmBooking` / `cancelBooking` / `expireBookings`：远程预约、近一周时间窗、预付定金、到场退回定金、爽约没收定金，并复用分配器返回预期路线。
- CLI 演示预约流程并汇总待结算/爽约没收定金；新增预约生命周期、爽约扣定金与重启恢复单元测试。

上一轮完成“BillingService 接入 CLI/GUI”：

- 新增 `BillingRule` / `BillingService`，离场费用在 `ParkingService` 中统一计算并写入 `ParkingRecord`。
- `ParkingRepository::saveExit()` 保留费用参数，费用与停车记录在同一 SQLite 事务中持久化。
- CLI 打印默认计费规则、批量入场/离场与累计停车费；Admin GUI 增加计费规则说明，并在释放车位时显示本次费用和累计收费。
- 对应提交：`9416bf9 feat: integrate parking billing and layout improvements`。

## 文献调研

调研来源、评分和取舍记录在 `docs/research-sources.md`。当前重点参考：

- [DB4403/T 313 智慧停车业务数据与接口规范](https://amr.sz.gov.cn/attachment/1/1566/1566543/9772236.pdf)：TCP 登录、心跳和事件清单。
- [PARCS 五层架构与费率引擎](https://parkingpaymentguide.com/payment-systems/how-parking-payment-systems-work/)：费率引擎、离线队列和宽限期。
- [MDPI SPMS 参考架构](https://www.mdpi.com/2079-8954/13/2/70)：Server / Gate / Admin 分层。
- [佛山禅城 2026 停车收费新规](https://www.163.com/dy/article/L5OVP5IU05129QAF.html)：15 分钟计费、30 分钟免费和日封顶。
- [HyperLPR3](https://github.com/szad670401/HyperLPR)：中文车牌识别基线，只放在 Gate，不进入 core。
- [CCPD](https://github.com/detectRecog/CCPD)：中国城市停车场车牌数据集，作为检测、识别与统一评测的核心数据来源。
- [北京 DB11/T 3001 ETC 停车场接口](https://jtw.beijing.gov.cn/xxgk/flfg/jthy/201912/P020191231388015746585.pdf)：Gate 作为 TCP 客户端、请求应答与重传。
- [深圳公共智慧停车平台数据接入规范](https://jtys.sz.gov.cn/attachment/1/1596/1596572/12076334.pdf)：心跳、NTP 和断线补报。

以上来源用于提炼接口、计费规则和架构边界，不直接复制外部项目代码。

## 车牌识别（LPR）方案

SmartPark 的 LPR 只部署在 `Gate Terminal`，不进入 `ParkingService`。Gate 从摄像头或测试图片获取画面，完成车牌识别后，仅把标准化后的车牌号、置信度、时间戳等结构化结果发送给 SmartPark Server。

为了兼顾课程项目的可实现性和模型训练研究价值，项目保留两条路线：

- **路线 A：HyperLPR3** —— 快速建立稳定、可运行的中文车牌识别基线。
- **路线 B：YOLO11m + PP-OCRv5** —— 项目的主要训练路线，针对中国停车场车牌进行检测和字符识别微调。

### 路线 A：HyperLPR3 基线

```text
Camera / Image
      |
      v
    OpenCV
      |
      v
  HyperLPR3
      |
      v
 Plate Result
      |
      v
Gate Terminal -> SmartPark Server
```

该路线主要承担以下作用：

- 尽快打通真实摄像头到 Gate、TCP Server、停车业务的完整链路。
- 提供中国车牌识别的 baseline，避免自训练模型尚未完成时阻塞系统开发。
- 与自训练路线在完全相同的测试集上比较整牌准确率、困难场景准确率和推理延迟。
- 保持 LPR 与停车核心解耦，后续可以无侵入切换识别引擎。

HyperLPR3 对常见中国蓝牌、黄牌、新能源车牌等已有专门支持，并提供 C/C++ 推理接口，因此适合用作 Gate 端基线。

### 路线 B：YOLO11m + PP-OCRv5 中国车牌专用模型

最终主路线采用“检测 + 序列识别”的两阶段结构：

```text
Camera / Image
      |
      v
    OpenCV
      |
      v
YOLO11m Plate Detector
  CCPD Fine-tuning
      |
      v
 Bounding Box
      |
      v
Crop / Perspective Rectification
      |
      v
PP-OCRv5 Server Recognizer
Chinese Plate Fine-tuning
      |
      v
 PlateValidator
      |
      v
   晋A12345
      |
      v
Gate Terminal -> SmartPark Server
```

#### 车牌检测模型

初始权重：

```text
yolo11m.pt
```

选择 `YOLO11m` 的原因：

- 属于中量级检测模型，复杂度明显高于 `n/s`，又没有 `l/x` 的部署成本。
- 车牌通常只占 1080p 入口画面中的较小区域，中量模型在困难、小目标场景中有更大的精度空间。
- 训练生态成熟，便于从 PyTorch 导出 ONNX，并在 C++ Gate 中部署。
- 检测阶段只设置一个类别：`license_plate`，不让 YOLO 承担中文字符分类。

建议首轮训练配置以 `imgsz=960` 为起点，并额外对 `640 / 960 / 1280` 做分辨率对比实验。

最终检测权重命名建议：

```text
smartpark_plate_yolo11m_best.pt
smartpark_plate_yolo11m.onnx
```

#### 字符识别模型

初始模型采用：

```text
PP-OCRv5_server_rec
```

检测得到车牌区域后，先完成裁剪和必要的透视矫正，再由 OCR 直接输出完整字符序列。项目不采用“第二个 YOLO 逐字符检测”的方案，避免字符漏检、字符排序、粘连字符和新能源 8 位车牌带来的额外复杂度。

OCR 将使用中国车牌专用字典缩小识别空间，核心字符包括：

```text
省级简称：京 沪 津 渝 冀 豫 云 辽 黑 湘 皖 鲁 新 苏 浙 赣 鄂 桂 甘 晋 蒙 陕 吉 闽 贵 粤 青 藏 川 宁 琼
数字：0-9
字母：A-Z
按需求扩展：警 学 港 澳 使 领 挂 等特殊字符
```

最终识别权重命名建议：

```text
smartpark_plate_rec.onnx
plate_dict.txt
```

### 两条路线对比

| 项目 | 路线 A：HyperLPR3 | 路线 B：YOLO11m + PP-OCRv5 |
| --- | --- | --- |
| 项目定位 | 可运行 baseline / 保底方案 | 最终主路线 / 训练研究方案 |
| 中国车牌针对性 | 已针对中国车牌设计 | 使用中国车牌数据进一步专门微调 |
| 是否需要自行训练 | 否 | 是 |
| 检测 | 框架内部完成 | YOLO11m 独立检测 |
| 字符识别 | 框架内部完成 | PP-OCRv5 序列识别 |
| 模型可控性 | 中 | 高，可控制训练集、增强、字典与阈值 |
| 训练难度 | 低 | 中高 |
| C++ 集成难度 | 低到中 | 中，计划统一导出 ONNX |
| 可解释/可做实验内容 | 中 | 高，可进行检测、OCR、分辨率、数据集消融实验 |
| 对 SmartPark 的价值 | 快速打通 Gate 实机链路 | 形成项目自身的中国车牌识别能力 |

最终不把两条路线设计成互斥方案，而是通过统一接口并存：

```text
ILicensePlateRecognizer
          |
     +----+----+
     |         |
     v         v
HyperLPR3   SmartParkLPR
 Baseline    YOLO11m
                +
             PP-OCRv5
```

这样可以在相同 Gate 输入与相同测试集上直接切换识别引擎并完成公平对比。

### 数据集方案

训练和评测以中国停车场场景为核心，不使用只包含欧美车牌的数据作为主训练集。

#### 1. CCPD2019

核心数据集使用 [CCPD — Chinese City Parking Dataset](https://github.com/detectRecog/CCPD)。该数据集来自中国城市停车场场景，并包含大量带车牌位置和车牌字符标注的图片，非常适合本项目。

计划使用的主要子集包括：

| 子集 | 用途 |
| --- | --- |
| `CCPD-Base` | 主训练集和基础评测 |
| `CCPD-DB` | 不同亮度、曝光场景 |
| `CCPD-Blur` | 模糊、运动模糊场景 |
| `CCPD-Rotate` | 车牌旋转场景 |
| `CCPD-Tilt` | 倾斜和透视形变场景 |
| `CCPD-FN` | 困难检测场景 |
| `CCPD-Challenge` | 综合困难场景评测 |

CCPD 文件名中的标注信息可以转换为：

- YOLO 所需的车牌检测框标签；
- OCR 所需的车牌裁剪图与完整字符序列标签。

因此同一份数据能够分别支持 Detector 和 Recognizer 的训练。

#### 2. CCPD2020 Green

新能源车牌使用 `CCPD2020 / CCPD-Green` 补充训练和独立评测，重点解决中国新能源 8 位绿色车牌。

目标至少覆盖：

```text
普通蓝牌：晋A12345
新能源牌：晋AD12345
```

新能源数据不能只混入总测试集，还应保留单独的 Green 指标，以避免整体准确率掩盖新能源车牌效果。

#### 3. 黄牌与特殊车牌扩展数据

如果后期基础模型已经稳定，可进一步加入包含黄牌以及更多车牌类型的公开数据，或选择合适的 `CCPD-Plus` 类扩展数据作为补充。

这部分作为增强项，不阻塞第一版 LPR：

```text
第一阶段：CCPD2019 + CCPD Green
第二阶段：黄牌 / 特殊牌照扩展
```

#### 4. SmartPark 自建停车场场景集

公共数据负责获得通用中国车牌能力，但最终微调应加入少量与实际 Gate 摄像头分布一致的数据。

建议后期采集或合规制作约 `2,000 ~ 5,000` 张 SmartPark 场景图片，覆盖：

- 入口与出口不同摄像机视角；
- 白天、夜间、逆光和车灯干扰；
- 雨天、反光和轻度污损；
- 不同车辆距离和不同车牌占画面比例；
- 水平、倾斜和透视角度；
- 蓝牌与新能源牌。

自建数据不作为公开车牌隐私数据直接提交 GitHub；仓库只保存数据格式说明、脱敏示例和训练脚本。

### 数据处理与目录规划

训练代码和 C++ 应用代码分离。Python 可以用于模型训练和数据转换，但最终 Gate 不依赖 Python 运行环境。

计划目录：

```text
training/
├── detector/
│   ├── prepare_ccpd.py
│   ├── ccpd.yaml
│   └── train.py
├── recognizer/
│   ├── prepare_recognition.py
│   ├── plate_dict.txt
│   └── configs/
└── evaluation/
    ├── evaluate_detector.py
    └── evaluate_lpr.py

models/
├── detector/
│   ├── smartpark_plate_yolo11m.onnx
│   └── metadata.json
└── recognizer/
    ├── smartpark_plate_rec.onnx
    └── plate_dict.txt
```

其中 Detector 数据转换为典型 YOLO 结构：

```text
datasets/ccpd_yolo/
├── images/
│   ├── train/
│   ├── val/
│   └── test/
├── labels/
│   ├── train/
│   ├── val/
│   └── test/
└── ccpd.yaml
```

Recognizer 将车牌区域裁剪出来并生成“图片路径 + 完整车牌字符串”的标签：

```text
train/plate_000001.jpg    晋A12345
train/plate_000002.jpg    京B88888
train/plate_000003.jpg    粤AD12345
```

### C++ 部署方案

自训练模型计划统一导出 ONNX：

```text
Training
PyTorch / Paddle
      |
      v
     ONNX
      |
      v
OpenCV + ONNX Runtime
      |
      v
C++ Qt Gate Terminal
```

Gate 只对上层暴露稳定的识别结果结构，不让业务层感知 YOLO、OCR 或 HyperLPR 的实现细节。

建议接口：

```cpp
struct PlateRecognitionResult {
    std::string plateNumber;
    float detectionConfidence;
    float recognitionConfidence;
    cv::Rect boundingBox;
};

class ILicensePlateRecognizer {
public:
    virtual ~ILicensePlateRecognizer() = default;

    virtual std::optional<PlateRecognitionResult>
    recognize(const cv::Mat& frame) = 0;
};
```

后续分别实现：

```text
HyperLPRRecognizer
SmartParkLprRecognizer
```

### 车牌规则后处理

OCR 结果后增加独立的 `PlateValidator`，用于格式校验、标准化和低置信度拒绝，但不允许用规则“伪造”模型未识别出的字符。

主要校验包括：

- 第一位是否为合法省级简称；
- 第二位是否符合车牌字母规则；
- 普通车牌与新能源车牌长度是否合法；
- 是否包含不允许出现的字符；
- OCR 置信度是否低于 Gate 的人工确认阈值。

低置信度时 Gate 应进入人工确认流程，而不是自动放行。

### LPR 评测设计

两条路线必须在相同测试划分上比较，至少记录：

| 指标 | 说明 |
| --- | --- |
| Detection Precision / Recall | 车牌检测精确率和召回率 |
| `mAP@0.5` / `mAP@0.5:0.95` | Detector 检测质量 |
| Character Accuracy | 单字符识别准确率 |
| Full Plate Accuracy | 整块车牌完全正确的比例，作为核心业务指标 |
| Green Plate Accuracy | 新能源车牌整牌准确率 |
| Blur / Tilt / Challenge Accuracy | 困难子集整牌准确率 |
| End-to-End Latency | 从输入帧到最终车牌字符串的耗时 |
| Model Size / Memory | Gate 部署资源占用 |

最终课程报告计划至少比较三个实验组：

```text
A. HyperLPR3

B. YOLO11m + 原始 PP-OCRv5

C. SmartPark LPR
   YOLO11m CCPD fine-tune
   + PP-OCRv5 中国车牌 fine-tune
   + 专用字符字典
   + SmartPark 场景数据
   + PlateValidator
```

其中 C 为最终模型。通过 B → C 的变化可以量化“中国车牌专用微调”带来的收益，而 A 则提供成熟专用框架的工程基线。

> 注意：训练数据、预训练模型和第三方框架均应遵循各自许可证与数据使用要求。特别是自建真实车牌数据应考虑隐私与脱敏；模型权重如体积较大，优先使用 Release、Git LFS 或独立下载说明，不直接放入普通 Git 历史。

## 自动分配算法

停车场由多个互不重叠的矩形分区组成。每个分区可独立设置行数、列数、车位宽、车位长、通道宽、通道位置和车位类型，因此可组合出长宽不同的车位和异形整体布局。场地可以有多个入口和出口。

构建布局时，系统验证：

- 分区必须完全位于场地边界内。
- 分区之间不能重叠。
- 通道宽度至少 2.5 米。
- 每个车位必须能从至少一个入口到达，并通往至少一个出口。

路径规划使用 0.5 米栅格。单目标查询使用 A*，分配器对每个入口/出口做一次到全部候选车位的搜索，避免每辆车对 60 个车位分别跑两次 A*。车位矩形为障碍，通道和空地为可通行区域。搜索支持 8 方向移动，禁止斜穿障碍，并增加转向代价。拥堵区域会提高边权：

```text
edgeCost = baseCost * (1 + k * localOccupancy)
```

默认 `k=0.35`，即局部每多一辆占用车，经过该格子的边权提高 35%。拥堵在两个层面生效：路径搜索把局部占用写入边权（`route.cost`），让规划出的路线自动绕开拥堵通道；车位评分再用 `nearbyOccupiedSpots`（`w3`）对局部占用密集的车位单独加罚。

`SpotAllocator` 默认使用 `WeightedCost` 策略：

```text
score =
    w1 * entryPathDistance
  + w2 * exitPathDistance
  + w3 * nearbyOccupiedSpots
  + w4 * turnCount
  + typePenalty
```

默认权重为 `w1=1.0`、`w2=0.35`、`w3=2.5`、`w4=0.4`。`Nearest` 只比较入口路径长度，作为对照策略。充电车位对电动车有加分，对普通车有惩罚；无障碍和 VIP 车位不是预留车位，而是对普通车附加类型惩罚（无障碍 `+25`、VIP `+40`），有替代空位时车辆会优先避开，只有绕行或距离代价高于类型惩罚时才会选择它们。

车位状态机：

```text
Available -> Reserved(ttl) -> Occupied -> Available
Available -> Occupied -> Available
```

预留未在 TTL 内确认占用时自动释放，避免后续 TCP 双端把同一车位分给两辆车。

## 自定义布局格式

CLI 和 GUI 共用以下文本布局格式：

```text
site 100 60
entrance 0 30
exit 100 30
region A 5 8 10 2 1.2 5.5 6 left normal
region B 38 24 10 2 1.4 6.0 6 right charging
region C 50 8 1 6 2.5 5.5 3.0 up accessible
obstacle 80 5 12 20 设备用房
```

字段含义：

- `site 宽 高`：场地边界，单位米。
- `entrance x y` 和 `exit x y`：入口与出口坐标，可重复多条。
- `region 名称 x y 行数 列数 车位宽 车位长 通道宽 left|right|up|down [类型]`：一个矩形分区；`left` / `right` 为东西向停车（车位长沿 X），`up` / `down` 为南北向停车（车位长沿 Y），也可用 `top` / `bottom`。
- `obstacle x y 宽 高 [名称]`：机房、楼梯等不可停车、不可通行区域。
- 类型可选 `normal`、`charging`、`accessible`、`vip`，也支持 `type=charging` 写法；省略时默认为 `normal`。
- `#` 开头的行是注释。
- 图纸布局示例：`data/garage-6f.txt`（6 层车库平面图，75 车位；电气室/设备用房/蓄电池室/水箱间已改为停车位）。

## 终端验证（无需 Qt、Conda 或图形桌面）

项目使用 CMake Presets 统一管理构建目录。所有构建产物都放在根目录的 `build/` 下，其中 `build/cli` 是终端版本，`build/qt` 是 Qt 版本。在项目根目录执行，需 CMake 3.21+、Ninja 和支持 C++17 的编译器：

```bash
/usr/bin/cmake --preset cli-debug
/usr/bin/cmake --build --preset cli-debug --parallel
/usr/bin/ctest --preset cli-tests
./build/cli/apps/cli/smartpark_cli
./build/cli/apps/cli/smartpark_cli my-layout.txt
./build/cli/apps/cli/smartpark_cli data/garage-6f.txt
./build/cli/apps/cli/smartpark_cli --db /tmp/smartpark.db --reset
```

如果需要全新构建，可先删除 `build/cli` 或整个 `build/` 目录。CLI 与 Qt 的 CMake 缓存分别保存在 `build/cli` 和 `build/qt`，互不影响。

不带参数时使用内置 60 车位布局；带文本文件参数时加载自定义布局。程序自动执行验证，无需输入。它打印默认计费规则，默认批量入场 30 辆、离场 15 辆、二次入场 8 辆，并输出每条车位的类型、入口距离、出口距离、综合评分、离场时长和费用，最后汇总累计停车费；成功输出 `RESULT: PASS` 并返回 0。

CLI 默认把车位与停车记录持久化到 SQLite：未指定 `--db` 时使用用户数据目录 `smartpark/smartpark.db`，重启后可恢复占用/预留状态与停车记录；`--reset` 在启动前删除数据库文件，适合反复演示。测试中的演示用例固定使用构建目录下的临时数据库并带 `--reset`，保证结果确定。

当前版本不包含网络或车牌识别。

### 文件职责与运行流程

| 文件 | 用途 |
| --- | --- |
| `CMakeLists.txt` | 设置 C++17、CTest 和各构建目标；关闭 `SMARTPARK_BUILD_ADMIN` 后不查找 Qt。 |
| `CMakePresets.json` | 定义 CLI 与 Qt 的标准构建目录、构建参数和测试命令。 |
| `docs/research-sources.md` | 记录仓库/论文评分，以及对本阶段路线的取舍。 |
| `apps/cli/CMakeLists.txt` | 构建 `smartpark_cli`，链接核心库并注册终端演示测试。 |
| `apps/cli/main.cpp` | 终端入口，加载默认或自定义布局、SQLite 数据库参数并演示自动分配。 |
| `src/core/CMakeLists.txt` | 将模型、服务与持久化实现编译为 `smartpark_core` 静态库。 |
| `src/core/model/Geometry.h` | 定义坐标、矩形和几何工具。 |
| `src/core/model/ParkingLayout.h/.cpp` | 解析自定义布局并生成车位矩形、类型和出入口。 |
| `src/core/model/ParkingRecord.h/.cpp` | 保存一次停车的车牌、车位、时间、时长和费用。 |
| `src/core/model/Booking.h/.cpp` | 定义预约记录（编号、车牌、车位、时间、定金、状态）与 `BookingPolicy`。 |
| `src/core/model/Reservation.h/.cpp` | 定义远程时间段预约（状态机、定金结算状态、预期路线快照序列化）、`ReservationRule` 与 `DepositPayment`。 |
| `src/core/model/Vehicle.h` | 声明车辆类型、车辆数据与只读访问接口。 |
| `src/core/model/Vehicle.cpp` | 实现车辆构造、非空车牌校验和数据访问。 |
| `src/core/persistence/DatabaseManager.h/.cpp` | SQLite 连接与生命周期管理。 |
| `src/core/persistence/ParkingRepository.h/.cpp` | 建表、入场/离场/预约/预约记录持久化与状态恢复。 |
| `src/core/persistence/Persistence.h/.cpp` | CLI/GUI 共用的数据库封装，提供默认路径与 RAII 生命周期。 |
| `src/core/model/ParkingSpot.h` | 声明车位状态、类型、当前车辆、预留和占用接口。 |
| `src/core/model/ParkingSpot.cpp` | 实现单个车位的状态转换，拒绝重复占用或释放。 |
| `src/core/service/GridPlanner.h/.cpp` | 实现障碍感知栅格 A*、多目标搜索和拥堵边权。 |
| `src/core/service/SpotAllocator.h/.cpp` | 独立选位、策略、评分和路径缓存。 |
| `src/core/service/Billing.h/.cpp` | 定义计费规则并计算离场费用。 |
| `src/core/service/ParkingService.h/.cpp` | 实现入场、离场、计费、预留 TTL、预约（创建/确认/取消/爽约）、时间段预约接入（自动到场、定金抵扣、延迟锁位扫描）、剩余车位和历史记录查询。 |
| `src/core/service/ReservationService.h/.cpp` | 实现远程时间段预约：冲突检查、延迟锁位、定金模拟支付、取消/到场/爽约状态机与持久化协作。 |
| `src/core/service/AnalyticsEngine.h/.cpp` | 本地数据分析：OLS 小模型拟合占用率趋势、生成中文结论与建议。 |
| `src/core/service/RemoteAnalystClient.h/.cpp` | 预留的远程分析接口：OpenAI 兼容请求组装与响应解析，传输层可注入。 |
| `src/core/service/AuditLogService.h/.cpp` | 哈希链防篡改审计日志：append-only 记录与链完整性校验。 |
| `src/core/service/DemoDirector.h/.cpp` | 剧本式一键演示：虚拟时钟驱动入场/预约/爽约/离场/分析的 16 步脚本。 |
| `src/core/service/FakePaymentGateway.h/.cpp` | 确定性模拟定金支付网关（收取/退回/没收/失败注入）。 |
| `tests/CMakeLists.txt` | 构建并注册模型单元测试。 |
| `tests/core_model_tests.cpp` | 验证模型、预留、计费边界、类型匹配、拥堵绕行、多入口选择与预约生命周期。 |
| `apps/admin/CMakeLists.txt` | 构建可选 Qt 管理员端，Qt 自动处理只作用于该目标。 |
| `apps/admin/main.cpp` | 独立 GUI 入口，解析 `--db` 参数、打开账号库并启动登录/主窗口。 |
| `apps/admin/Theme.h` | 统一视觉主题：曜石石墨 + 香槟金配色、主窗口/认证对话框样式表、自绘高斯光斑背景。 |
| `apps/admin/NativeEffects.h/.mm` | macOS 原生 NSVisualEffectView 毛玻璃封装（非 macOS 为空实现）。 |
| `apps/admin/UserStore.h/.cpp` | 登录/注册账号存储：users 表、盐化迭代 SHA-256 口令摘要、演示账号播种。 |
| `apps/admin/LoginDialog.h/.cpp` | 登录对话框：字段校验、失败锁定、密码可见切换、记住账号、注册入口。 |
| `apps/admin/RegisterDialog.h/.cpp` | 注册对话框：账号/密码/确认密码校验与强度提示。 |
| `apps/admin/MacSystemBridge.h/.mm` | macOS 原生桥接：菜单栏余位、通知中心、中文语音、PDF 导出、NSURLSession 同步 POST。 |
| `apps/admin/MainWindow.h/.cpp` | 实现布局编辑、车位图、自动分配、路线显示与数据库恢复/重置交互。 |

运行流程：系统启动 `smartpark_cli` → 解析布局 → 构建障碍栅格 → `SpotAllocator` 按策略为候选车位计算路线和评分 → 选择最优车位并占用或预留 → 输出路线和状态 → 检查结果并返回退出码。

## Admin GUI 跨平台构建与运行

`smartpark_admin` 现在支持 Windows、macOS 与 Linux。跨平台差异被封装在 CMake 和构建脚本中：

- Windows 使用 Qt 6 MSVC Kit，并把目标设置为 `WIN32` GUI 程序。
- macOS 使用 Homebrew 或官方 Qt，输出 `smartpark_admin.app`。
- Linux 使用 conda/mamba 环境或系统 Qt，输出可执行文件。
- `apps/admin/main.cpp` 提供 `--smoke-test`：窗口启动约 1.5 秒后自动退出，适合 CI 或无人工交互时做启动检查。
- `.github/workflows/admin-cross-platform.yml` 会在 Ubuntu、Windows 与 macOS 三个 GitHub Actions 运行器上构建并执行 Admin GUI 冒烟测试。

### Linux

当前 Linux 开发环境使用 conda/mamba 环境。如果 `CONDA_PREFIX` 已指向含 Qt 6 的环境，可直接运行：

```bash
scripts/build-admin.sh
scripts/run-admin.sh
```

手动构建时：

```bash
export MAMBA_ROOT_PREFIX=/opt/mamba
export PATH=/opt/mamba/envs/smartpark/bin:$PATH
export CONDA_PREFIX=/opt/mamba/envs/smartpark
export CMAKE_PREFIX_PATH=/opt/mamba/envs/smartpark

cmake --preset qt-debug
cmake --build --preset qt-debug --parallel
ctest --preset qt-tests
./build/qt/apps/admin/smartpark_admin

# 预约命令示例（Booking 第一版）
./build/cli/apps/cli/smartpark_cli --db /tmp/p.db --book 晋A12345 --in 90
./build/cli/apps/cli/smartpark_cli --db /tmp/p.db --checkin 晋A12345 --in 90
./build/cli/apps/cli/smartpark_cli --db /tmp/p.db --bookings

# 时段预约命令示例（Reservation，含定金支付与冲突检查）
./build/cli/apps/cli/smartpark_cli --db /tmp/p.db --reserve 晋B12345 --in 120 --duration 180
./build/cli/apps/cli/smartpark_cli --db /tmp/p.db --arrive 晋B12345 --in 120
./build/cli/apps/cli/smartpark_cli --db /tmp/p.db --rsv-cancel 晋B12345
./build/cli/apps/cli/smartpark_cli --db /tmp/p.db --reservations
./build/cli/apps/cli/smartpark_cli --db /tmp/p.db --settle-reservations

# 数据分析：本地模型输出结论与建议
./build/cli/apps/cli/smartpark_cli --db /tmp/p.db --analyze
```

如果当前终端没有图形显示，可以使用 Qt 的 offscreen 平台插件做启动检查：

```bash
QT_QPA_PLATFORM=offscreen ./build/qt/apps/admin/smartpark_admin
```

无图形桌面时的启动自检：

```bash
QT_QPA_PLATFORM=offscreen ./build/qt/apps/admin/smartpark_admin --smoke-test
```

### macOS

需要 Xcode Command Line Tools、CMake、Ninja 与 Qt 6：

```bash
brew install cmake ninja qt
scripts/build-admin.sh
scripts/run-admin.sh
```

脚本会自动通过 `brew --prefix qt` 找到 Homebrew Qt；也可以显式指定：

```bash
QT_PREFIX="$(brew --prefix qt)" scripts/build-admin.sh
```

如果使用官方 Qt 安装包或 `aqtinstall` 安装到自定义目录，可直接把前缀传给脚本：

```bash
QT_PREFIX="$HOME/Qt/6.8.3/macos" scripts/build-admin.sh
```

macOS 15 已移除 `AGL.framework` 的实际二进制，脚本会自动在 `build/qt/macos-agl-stub` 生成一个兼容 stub，无需手工处理。

构建结果位于 `build/qt/apps/admin/smartpark_admin.app`。如需从命令行直接运行 GUI：

```bash
scripts/run-admin.sh
```

### Windows

推荐安装 Qt Online Installer 的 Qt 6.8+ MSVC 2022 64-bit Kit，以及 Visual Studio 2022（含 C++ 桌面开发工具）。安装后先执行：

```bat
scripts\build-admin.bat
scripts\run-admin.bat
```

如果 `qmake` 已加入 `PATH`，脚本会自动推导 Qt 前缀；也可以手动指定：

```bat
set QT_PREFIX=C:\Qt\6.8.3\msvc2022_64
scripts\build-admin.bat
```

脚本优先使用 Ninja；未找到 Ninja 时回退到 Visual Studio 2022 生成器。构建后的 `smartpark_admin.exe` 位于 `build\qt\apps\admin\smartpark_admin.exe` 或 `build\qt\apps\admin\Debug\smartpark_admin.exe`。

如果需要在 CI 中验证，可直接运行 GitHub Actions 的 `Admin GUI cross-platform` workflow，它会在三平台构建并执行 `--smoke-test`。
