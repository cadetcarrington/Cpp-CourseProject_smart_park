## 2.2.2 服务端功能需求

本系统服务端进程为 `smartpark_server`，是整个停车业务的唯一权威。它向上为 macOS 管理端、网页用户端、道闸终端 `smartpark_gate` 和命令行用户端 `smartpark_user` 提供统一接口：TCP 长连接承载终端与管理端的实时业务动作，HTTP/REST 承载网页端的注册登录、查询、预约与缴费，WebSocket 承载状态变化推送。三套入口在同一个进程中运行并共享同一个 `ParkingService` 实例（`apps/server/main.cpp:1032-1048`），任何一端写入的变化都会经事件总线同步给其余在线端，避免多份状态各自为政。

按职责划分，服务端需求可归为六类。第一，接入与认证：TCP 协议 v1 支持登录、心跳与各类业务动作，非登录动作必须携带与登录一致的有效令牌；REST 接口采用 Bearer token 和 user/admin/gate 三级角色，并对连续登录失败实施账号级锁定。第二，核心停车业务：入场时完成预约核销或车位分配，返回进场路线与出场路线；离场时按统一规则计费，并支持预约定金抵扣；全程维护车位占用状态与停车记录。第三，时段预约与模拟支付：校验预约时间、时长、提前量、车牌冲突与一人一单，创建即锁位并收取定金，支持到场核销、取消退款、超时爽约没收与离场抵扣，同时提供网页端可闭环的模拟支付订单。第四，数据持久化与恢复：以单个 SQLite 数据库保存布局、车位、记录、预约、定金流水、账号、审计与支付订单，进程重启时从库中恢复并做一致性校验。第五，多端同步与断网补报：通过事件广播把入场、离场、预约变化推送给所有在线端；道闸断网期间事件落盘，恢复后按原始时间戳补报并按幂等规则去重。第六，识牌与运维：服务端可调用本机识牌脚本或使用内置确定性 mock 后端，把识别结果交给管理端人工核对，并提供启动参数、部署方式、`--selftest` 进程内自检和审计日志，满足无图形界面服务器的运行要求。

## 3.1.1 核心数据模型与类设计

本系统采用“领域模型—业务服务—持久化”三层结构：模型层只表达业务对象的状态与不变量，业务服务层编排一次完整操作并处理失败回滚，持久化层封装 SQL、事务与重启恢复；网络层只做协议编解码、认证与权限判断，不直接修改模型状态。以下类名、文件名与行号均取自当前仓库源码。

### 3.1.1.1 领域模型类

| 类与类型 | 定义位置 | 职责 | 关键不变量 |
| --- | --- | --- | --- |
| Point、Rectangle | `src/core/model/Geometry.h:5-31` | 平面点与轴对齐矩形，提供距离、包含、中心与矩形重叠判定 | 重叠面积按 1e-9 精度判定，正好相切不算重叠 |
| Vehicle、VehicleType | `src/core/model/Vehicle.h:4-19` | 车辆由车牌与车型组成，车型分轿车、摩托车、卡车、电车 | 车牌不可为空 |
| ParkingSpot | `src/core/model/ParkingSpot.h:9-89` | 车位实体：号码、分区、行列、矩形范围、可达点、类型与状态 | 状态迁移只能沿固定路径进行 |
| ParkingRecord | `src/core/model/ParkingRecord.h:7-30` | 一次在场记录：车牌、车位、进出场时间、车型与费用 | 只能关闭一次，离场不早于入场，费用非负 |
| Reservation | `src/core/model/Reservation.h:12-214` | 时段预约订单：预约状态、定金状态、规则与预期路线 | 同一车牌同时最多一个未结束订单 |
| ParkingLayout | `src/core/model/ParkingLayout.h:12-54` | 场地布局：尺寸、入口出口、区域、障碍与通道朝向 | 尺寸为正，至少含一个可生成车位的区域 |

上述值对象中，`Geometry.h` 的 `Point`、`Rectangle` 是全系统统一的米制几何基础，车位矩形、障碍矩形、路径折线都复用它，避免在算法层与界面层之间传递像素坐标。`ParkingSpot` 是最重要的状态载体，其状态集合为 Available、Occupied、Reserved、Disabled 四种；`occupy` 只接受空闲车位或同一车牌预订的车位，`reserve` 只接受空闲车位，`release` 只接受占用或预订状态，非法迁移一律返回 false 而不抛异常，使上层可以把失败翻译为“车位已被占用”“预约车位当前不可用”等业务错误。车位同时保存矩形范围与可达点，路径规划把车位中心所在格视为不可通行，以可达点作为通道侧的最后连接点，从而保证车辆先行驶到通道再进入车位。

`ParkingRecord` 以“构造即在场、关闭即离场”的方式约束生命周期：`close(exitTime, fee)` 会拒绝重复关闭、离场时间早于入场时间以及负费用，时长按秒计算，为计费提供统一时间口径。`Reservation` 的预约状态为 PendingPayment、Confirmed、CheckedIn、Completed、Cancelled、NoShow、Expired 七种，对应持久化整数 0 至 6（`Reservation.h:12-38`）；定金状态为 Pending、Refunded、Forfeited、Applied 四种；`DepositPayment` 记录 Charge、Refund、Forfeit、Apply 四类定金流水及其成功或失败结果（`Reservation.h:104-135`）。`ExpectedRoute` 保存下单时的入口索引、出口索引与进场、离场两条折线，`encode` 以空格分隔文本写入 `reservations.route` 列，重启后 `decode` 还原，使管理端的到场引导可以复用下单时的路线快照而不必重新规划。`ParkingLayout` 从文本文件解析 site、entrance、exit、region、obstacle 五类指令，`ParkingLayout.cpp:31-45` 对尺寸做正数校验，`ParkingLayout.cpp:99-106` 给出内置演示布局，布局文件是车位生成、栅格地图与网页端底图的共同数据源。

### 3.1.1.2 核心业务类

| 类 | 定义位置 | 主要职责 | 主要协作对象 |
| --- | --- | --- | --- |
| ParkingService | `src/core/service/ParkingService.h:52` | 聚合根：编排入场、离场、预约核销、寻车、应急让位与恢复 | SpotAllocator、GridPlanner、BillingService、ReservationService、ParkingRepository |
| GridPlanner | `src/core/service/GridPlanner.h:17` | 在 0.5 米栅格上做路径搜索，输出折线、距离、代价与转弯数 | ParkingLayout、ParkingSpot |
| SpotAllocator | `src/core/service/SpotAllocator.h:49` | 在候选车位中按代价模型选位，支持加权与最近两种策略 | GridPlanner、ParkingLayout |
| BillingService | `src/core/service/Billing.h:15` | 按免费时长、计费单元、最低费、单元费与日封顶计算停车费 | ParkingService |
| ReservationService | `src/core/service/ReservationService.h:20` | 时段预约的规则校验、锁位、核销、取消、爽约扫描与定金结算 | ParkingService、SpotAllocator、FakePaymentGateway、ParkingRepository |
| FakePaymentGateway | `src/core/service/FakePaymentGateway.h:7` | 模拟收取、退回、没收与结算定金，返回交易号 | ReservationService |
| AnalyticsEngine、ParkingInsightEngine | `src/core/service/AnalyticsEngine.h:85`、`ParkingInsightEngine.h:71` | 汇总运营快照、线性外推与分区风险提示 | ParkingService |
| UserStore、AuditLogService | `src/core/service/UserStore.h:12`、`AuditLogService.h:12` | 账号口令摘要、令牌签发与校验；哈希链审计 | RestGateway、SmartParkTcpServer |
| Persistence、ParkingRepository、DatabaseManager | `src/core/persistence/Persistence.h:9`、`ParkingRepository.h:28`、`DatabaseManager.h:5` | 组合数据库连接，提供全部保存、加载与事务接口 | ParkingService、ReservationService、RestGateway |

`ParkingService` 是整套业务的中枢，其构造函数注入布局、分配策略、计费规则以及可选的仓储与审计服务（`src/core/service/ParkingService.cpp:34-60`）。构造时先调用 `ensureReachable`（`:866`）用四向行人规划器检查每个车位可达点到出口的可达性，任何车位不可达即拒绝构造；若注入了仓储，则先保存布局签名再调用 `restore`（`:899`）恢复记录、车位与预约。`enterImpl`（`:243-281`）先尝试把该车牌的预约核销为到场，命中则直接占用预约车位；否则检查是否已在场、是否存在短时锁，再经 `SpotAllocator` 选位、占用车位、生成记录并写库，写库失败时依次回滚记录与车位。`leave` 与 `closeActiveRecord`（`:283-335`）先调用计费得到基础费用，再由 `ReservationService` 计算定金抵扣并推进预约状态，最后在一个数据库事务内完成“关闭记录、释放车位、更新预约、写入抵扣流水”。

`GridPlanner` 与 `SpotAllocator` 分工明确：前者回答“怎么走”，后者回答“停哪里”。`SpotAllocator::propose`（`src/core/service/SpotAllocator.cpp:76-176`）先过滤候选车位，在加权策略下按分区选择目标分区，再对分区内候选逐一调用两次规划得到进场与离场代价（`:179-317`）。`BillingService::calculateFee`（`src/core/service/Billing.cpp:17-46`）与 `ReservationService::prepareExitSettlement`（`src/core/service/ReservationService.cpp:483-503`）都只做纯计算并返回数据，不直接写库，因而可以脱离数据库做单元测试。账号与审计方面，`UserStore` 为每个账号生成 16 字节随机盐，经 12000 轮 SHA-256 迭代得到口令摘要（`src/core/service/UserStore.cpp:19,132-165`），登录令牌只存摘要；`AuditLogService` 将上一条记录的哈希作为 `prev_hash` 参与本条哈希（`src/core/service/AuditLogService.cpp:41-52,82-105`），从而让登录、入场、离场、预约与补报等动作形成可校验的哈希链。`BackgroundEntryPlanner`（`src/core/service/BackgroundEntryPlanner.h:11-33`）把耗时分配放到线程池，工作线程只持有布局与车辆的值快照，回到主线程后再提交状态修改，避免事件循环被长搜索阻塞。

### 3.1.1.3 数据库表结构设计

服务端使用单个 SQLite 数据库文件，由 `Persistence` 负责连接与生命周期，按“布局与车位、停车业务、预约与定金、账号与审计、支付与扫码”五组共设计 12 张表。时间统一存储为 epoch 毫秒整数，金额以元为单位存 REAL，枚举以整数存储并在模型层提供显式映射，布局签名与路线等复合数据以文本存储，既便于 SQL 查询，也便于跨版本迁移。

| 表名 | 用途 | 关键字段 | 约束或索引 | 建表位置 |
| --- | --- | --- | --- | --- |
| parking_spots | 车位主数据与当前状态 | identifier、zone、type、row_index、column_index、x、y、width、height、access_x、access_y、status、plate_number、vehicle_type、reserved_until_ms | identifier 主键 | `ParkingRepository.cpp:57-72` |
| layout_snapshot | 布局签名快照 | name、value | name 主键，启动时比对布局防止库与图纸错配 | `ParkingRepository.cpp:81-83` |
| parking_records | 在场与历史停车记录 | id、plate_number、spot_id、entry_time_ms、exit_time_ms、vehicle_type、fee | 自增主键；唯一部分索引 idx_active_parking_record 保证一车最多一条未离场记录 | `ParkingRepository.cpp:73-80` |
| bookings | 旧版短时预约（兼容保留） | id、plate_number、spot_id、created_at_ms、arrival_time_ms、arrival_deadline_ms、deposit、status | id 主键；idx_bookings_plate | `ParkingRepository.cpp:84-92` |
| reservations | 时段预约订单 | id、plate_number、vehicle_type、spot_id、start_time_ms、end_time_ms、grace_deadline_ms、deposit、status、deposit_state、charge_tx_id、route、accessible | 主键；唯一部分索引 idx_reservations_open_plate 限制同一车牌仅一个未结束订单；idx_reservations_spot_status | `ParkingRepository.cpp:99-113` |
| deposit_payments | 定金流水 | id、reservation_id、plate_number、kind、amount、status、created_at_ms | id 主键 | `ParkingRepository.cpp:122-129` |
| users | 账号、口令摘要与锁定 | username、salt、password_hash、created_at_ms、last_login_ms、role、failed_attempts、locked_until_ms | username 主键 | `UserStore.cpp:66` |
| auth_tokens | 登录令牌摘要 | token_hash、username、role、created_at_ms、expires_at_ms、revoked | token_hash 主键 | `UserStore.cpp:78` |
| audit_logs | 审计哈希链 | id、ts_ms、actor、action、detail、prev_hash、hash | id 自增主键 | `AuditLogService.cpp:29` |
| payment_orders | 模拟支付订单 | order_id、out_trade_no、kind、username、plate、reservation_id、amount、status、created_at_ms、expire_at_ms、paid_at_ms、refund_at_ms、payload | order_id 主键，out_trade_no 唯一，kind 与 status 带 CHECK 约束 | `RestGateway.cpp:497` |
| site_tickets | 扫码进入票据 | token、site_name、created_at_ms、expires_at_ms、revoked | token 主键，过期时间 0 表示不过期 | `RestGateway.cpp:1765` |
| user_plates | 用户绑定车牌 | username、plate、site_token、created_at_ms | 复合主键（username, plate） | `RestGateway.cpp:1777` |

表结构设计有三条一致性防线。第一是唯一部分索引：`parking_records` 上按车牌、仅对未离场记录建唯一索引，`reservations` 上按车牌、仅对状态 0 至 2 的未结束订单建唯一索引，即使上层并发校验出现竞态，数据库也会拒绝“一车两条在场记录”或“一人两个未结束预约”（`ParkingRepository.cpp:93-96,115-118`）。第二是事务边界：离场结算把关闭记录、释放车位、更新预约与写入抵扣流水放进同一事务，任一步失败整体回滚（`ParkingRepository.cpp:716-745`）。第三是布局签名：`layoutSignature` 以 `max_digits10` 精度序列化布局并写入 `layout_snapshot`（`ParkingRepository.cpp:156,188-230`），进程启动时比对当前 `--layout` 与库中签名，不一致直接拒绝启动，防止在旧库上套用新图纸造成车位编号错位。结构演进采用惰性迁移：既通过 `PRAGMA table_info(parking_records)` 检查并补齐 `vehicle_type` 列（`ParkingRepository.cpp:139-150`），也对 users 表逐列执行可重复的 `ALTER TABLE ADD COLUMN`，重复执行时忽略“列已存在”错误。

数据库各表之间的关系如图3-1所示：`parking_spots` 是车位维度表，被 `parking_records`、`bookings`、`reservations` 以车位号引用；`reservations` 与 `deposit_payments` 以预约号关联，形成一单一组定金流水；`payment_orders` 通过 `reservation_id` 与车牌分别关联预约与停车记录；`users` 与 `auth_tokens`、`user_plates` 通过用户名关联；`audit_logs` 独立成链，不设外键，以保证审计记录不受业务数据删除影响。

[[FIG:fig-db-er]]

### 3.1.1.4 通信协议与数据对象

TCP 协议用于终端、管理端与命令行用户端的长连接通信，协议版本 1，默认端口 9527。帧格式为“4 字节大端长度前缀 + UTF-8 紧凑 JSON”，长度字段只统计 JSON 字节数；单帧上限 9 MiB、单张图片上限 6 MiB（`src/network/Protocol.h:13,17,19`），编码与解码分别由 `encodeFrame` 和 `tryDecodeFrame` 完成（`src/network/Protocol.cpp:20-51`），后者返回 NeedMore、Ok、Invalid 三种状态，以支持 TCP 粘包与半包处理。协议时间为 epoch 毫秒整数，金额为元，车型枚举为 car、motorcycle、truck、electric。

```json
{"v":1,"type":"request","id":"7","action":"parking.enter",
 "token":"…","payload":{"plate":"晋A12345","vehicleType":"car"}}
{"v":1,"type":"response","id":"7","ok":true,"payload":{"spotId":"A-012"}}
{"v":1,"type":"event","event":"parking.entered","payload":{"plate":"晋A12345"}}
```

报文信封统一包含版本、类型、编号、动作或事件名、令牌与负载：请求由 `makeRequest` 生成，响应由 `makeResponse` 生成并携带布尔成功标志与中文错误说明，事件由 `makeEvent` 生成并由服务端主动推送。业务动作在 `SmartParkTcpServer::dispatch` 中分发（`src/network/SmartParkTcpServer.cpp:203-238`），包括登录、心跳、停车状态、车位列表、入场、离场、预约创建/取消/到场、补报、识牌、分析报表与管理员快照；登录是唯一免令牌动作，其余动作都要求会话已认证且令牌与登录时一致，补报仅限道闸令牌、管理员快照仅限管理员令牌。同一时刻一个会话只处理一条连接，服务端定时扫描会话，超过 60 秒无帧即断开（`:320-326`），连续 5 次登录失败同样断开并写审计。

REST 网关与 TCP 服务端同进程运行，默认对外提供 34 个 `/api/v1` 端点（`src/network/RestGateway.cpp:225-355`），覆盖元信息、注册登录、令牌刷新、停车状态、车位、布局、记录、在场记录、入场、离场、预约、支付订单、二维码、识牌、车位指引、站点票据与用户车牌绑定，以及独立的 WebSocket 升级端点。认证采用 Bearer token，登录返回 2 小时有效的访问令牌，刷新接口换发新令牌并吊销旧令牌；角色分为 user、admin、gate，注册账号固定为 user。统一错误响应为 `{code,message}` 两个字段，message 为中文可读文本，主要错误码如下表所示。

| 错误码 | HTTP 状态 | 含义 |
| --- | --- | --- |
| VALIDATION | 400 | 字段缺失或格式非法，message 指明具体字段 |
| AUTH_FAILED | 401 | 用户名或口令错误 |
| AUTH_REQUIRED | 401 | 缺少或无效的访问令牌 |
| AUTH_EXPIRED | 401 | 访问令牌已过期，需要刷新或重新登录 |
| FORBIDDEN | 403 | 当前角色无权访问该端点 |
| AUTH_LOCKED | 423 | 账号因连续失败被锁定，message 含剩余时间 |
| NOT_FOUND | 404 | 资源不存在 |
| CONFLICT | 409 | 业务状态冲突，如车辆已在场、时段重叠、一人一单 |
| PAYMENT_STATE | 409 | 订单状态不允许当前操作 |
| VERSION | 400 | 客户端协议版本不匹配 |
| INTERNAL | 500 | 服务端内部错误 |

主要数据对象沿用领域模型的可序列化投影：停车状态对象返回总容量、已占用、可用、已预订及各分区负载；车位对象返回车位号、分区、类型、状态、车牌、车型与矩形范围；预约对象返回预约号、车位号、起止时间、到场截止时间、定金、状态以及入口、出口与路线折线；支付订单对象返回订单号、商户单号、类型、金额、状态与超时时间。管理端通过 `admin.snapshot` 一次取回布局、车位、记录、预约、计费与预约规则以及最近 7 日收入，用于远程模式的全量刷新；网页端则通过 REST 分页查询，并在 WebSocket 上接收事件增量更新。

## 3.2.1 系统流程图

服务端启动先解析命令行参数（`apps/server/main.cpp:1088-1142`），由 `Persistence` 打开或创建 SQLite 数据库，构造 `ParkingService` 并检查布局可达性，再从库中恢复记录、车位、预约与定金流水并校验“记录—车位—预约”一致性，不一致即拒绝启动。恢复完成后依次在 9527 端口监听 TCP、启动 REST 与 WebSocket 推送，最后打印监听横幅与扫码入口（`:1044-1062`）。

主业务线程围绕入场与离场两条链路展开。入场链路为：终端或管理端发起 `parking.enter`，服务端校验会话与令牌后调用 `ParkingService::enterImpl`；若该车牌存在已确认的时段预约且未过到场截止时间，则把预约推进为已到场并直接占用预约车位；否则检查是否已在场和是否存在短时锁，由 `SpotAllocator` 在候选车位中按代价评分选位，`GridPlanner` 计算进场与出场路线，随后占用车位、生成停车记录并写库，最后经事件总线广播 `parking.entered`。离场链路为：收到 `parking.leave` 后定位该车牌未关闭的记录，按统一计费规则计算基础费用，若存在已到场预约则计算定金抵扣并推进预约到已完成，随后在单个事务中关闭记录、释放车位、更新预约并写入抵扣流水，广播 `parking.exited`。

预约链路为：网页端或管理端提交预约后，服务端先执行一次爽约扫描，再依次校验时间、时长、提前量、最远预约天数、车辆是否在场、一人一单与车位锁冲突；通过后在可用车位中选位，创建即锁位、收取定金、写入订单与定金流水并记录预期路线，广播 `reservation.created`。支付链路为：网页端为预约定金或离场停车费创建模拟支付订单，确认支付后由网关推进订单状态；停车费订单确认时直接触发离场结算，未预缴而直接离场时相关订单被置为已取消。

异常分支同样在流程中显式处理：满场时管理员可发起应急入场，系统按“到出口距离最近”选择一辆在场车，先正常结算离场腾出车位，再把车位分配给应急车辆并记录审计；布局签名与数据库不符时拒绝启动；识牌失败时返回可读错误；道闸断网时事件先写入本地 JSONL 队列，恢复连接后批量补报，服务端按车牌、事件类型与毫秒时间戳去重，只确认已处理成功的连续前缀。完整流程如图3-2所示。

[[FIG:fig-flow-main]]

远程模式下，macOS 管理端通过 TCP 登录并获得会话令牌，页面刷新采用快照拉取加事件推送：业务动作响应携带最新结果，入场、离场、预约与支付事件经事件总线转发到 TCP 与 WebSocket，管理端收到事件后触发快照重拉；网页端与道闸的写入也会出现在管理端视野中，时序关系如图3-3所示。

[[FIG:fig-remote-seq]]

## 3.2.2 功能模块图

服务端在结构上分为接入层、业务核心层与支撑层三层，模块之间通过 `ParkingService` 与 `EventHub` 解耦，接入层不直接访问数据库，支撑层不感知具体协议。

- 接入层：TCP 会话管理与心跳、帧编解码、动作分发、令牌认证与角色校验、断网补报、事件广播；REST 路由与静态资源、注册登录与令牌刷新、停车查询、预约与支付订单、二维码、识牌接口、WebSocket 会话与推送。
- 业务核心层：停车服务聚合根（入场、离场、应急让位、寻车）、车位分配模块、路径规划模块、计费模块、时段预约与定金模块、模拟支付网关、运营分析模块、分区洞察模块、后台预分配模块。
- 支撑层：持久化与恢复模块（数据库连接、12 张表、事务、布局签名、一致性校验）、账号模块（口令摘要、令牌、角色、锁定）、审计模块（哈希链）、识牌集成模块（脚本、mock、异步队列）、配置与自检模块（启动参数、`--selftest`）。

其中接入层三个入口共享同一业务核心，业务核心层的写操作都以 `ParkingService` 为唯一入口，因此 TCP、REST 与 WebSocket 看到的是同一份状态。支撑层的持久化模块对业务核心只暴露保存、加载与事务接口，识牌模块通过回调把结果送回事件循环，二者都不反向依赖接入层。各层包含的具体类见 3.1.1.2 节。流程与模块互为补充：3.2.1 节按时间维度描述动作先后，本节按空间维度描述静态依赖，模块图同时给出三端与公共模块的边界。整体功能模块划分如图3-4所示。

[[FIG:fig-modules]]

## 4.1.3 服务端部署与运行验证

服务端按无图形界面程序构建，只依赖 Qt 的 Core、Sql、Network、HttpServer、WebSockets 五个模块，不含 Qt Widgets 与 QML。项目提供三种构建预设：`macos-debug` 生成含管理端、服务端、终端与测试的调试构建；`server-debug` 只构建服务端相关目标；`server-release` 以 Release 方式构建服务端与终端且不启用测试，用于部署。文档给出的部署构建命令通过 `-DSMARTPARK_BUILD_MACOS_ADMIN=OFF -DBUILD_TESTING=OFF` 关闭管理端与测试，产物位于构建目录的 `apps/server/smartpark_server`、`apps/gate/smartpark_gate` 与 `apps/user/smartpark_user`（`docs/deploy-server.md:99-128`）。

| 构建预设 | 构建目录 | 内容 | 典型用途 |
| --- | --- | --- | --- |
| macos-debug | build/macos | 管理端、服务端、终端、测试 | 本机开发与调试 |
| server-debug | build/server | 服务端与终端（调试） | 协议联调 |
| server-release | build/server-release | 服务端与终端（Release，无测试） | 服务器部署 |

服务端的启动参数集中在 `apps/server/main.cpp` 中注册（`:1088-1142`），常用参数如下表所示。程序默认端口为 TCP 9527、HTTP 8080、WebSocket 8081；实际部署示例显式使用 HTTP 18080 与 WebSocket 18081，以避开开发机上常见的 8080 占用（`docs/deploy-server.md:123-151`）。

| 参数 | 默认值 | 作用 |
| --- | --- | --- |
| --port | 9527 | TCP 协议端口，供管理端、道闸终端与命令行用户端连接 |
| --http-port | 8080 | REST 网关端口，0 表示由系统随机分配 |
| --ws-port | 8081 | WebSocket 推送端口，0 表示关闭推送 |
| --db | ~/.local/share/smartpark/smartpark.db | SQLite 数据库路径 |
| --layout | 内置 60 车位布局 | 布局文件路径；给定文件后车位由图纸生成 |
| --web-root | 空 | 网页端静态文件目录；不传则不伺服网页 |
| --advertise | 自动探测 | 对外通告的地址，用于横幅、二维码与 meta |
| --ws-public-url | 空 | 对外暴露的 WebSocket 地址 |
| --site-name | SmartPark 停车场 | 站点名称，显示在网页端与票据中 |
| --lpr-command | 空 | 识牌脚本命令模板，空则使用 mock 后端 |
| --selftest | 关闭 | 进程内自检模式，跑通主要链路后退出 |

布局数据有三种来源：不传 `--layout` 时使用内置的 60 车位基础布局；`data/garage-3tower.txt` 对应第 1.2 节所述的三塔楼图纸，为 165 m × 85 m、10 个分区、330 个车位、1 个入口与 2 个出口；`data/garage-6f.txt` 则是 58 m × 42.4 m、75 个车位的车库布局，端到端测试与网页演示均以它为演示布局（`tests/e2e_rest.sh:137`）。三套布局共用同一套解析、分配、计费与绘制代码，替换布局文件即可切换场地，无需修改业务逻辑；布局文件参与布局签名计算，换用不同布局后旧数据会按签名校验规则处理。

启动成功后，服务端在标准输出打印监听信息，包括 TCP 端口、REST 元信息地址、WebSocket 端口、数据库路径与车位数，并在启用网页静态资源时打印扫码入口地址与 ASCII 二维码（`apps/server/main.cpp:1044-1062`）。示例横幅为：

```bash
SmartPark server listening on port 9527 | REST http://10.0.0.5:8080/api/v1/meta \
  | ws 8081 | db: /opt/smartpark/data/smartpark.db | spots: 75
Scan to open SmartPark H5: http://10.0.0.5:8080/#/claim?t=<票据>
```

新数据库首次启动会初始化演示账号 admin、gate、user，初始口令均为 smartpark；登录后分别对应管理员、道闸与普通用户角色，演示账号只在空库初始化时创建。数据库存放位置由 `--db` 决定，默认在用户主目录下；服务端完成启动后，可用 `--selftest` 在进程内依次验证登录、状态查询、入场、建预约、到场、离场、分析报表与事件广播，也可通过 REST 冒烟脚本检查 `/api/v1/meta` 与登录接口。生产部署建议以 systemd 托管进程，防火墙仅对可信网段开放 9527、8080/18080 与 8081/18081，其中 TCP 协议为明文传输，应仅限内网或经 SSH 隧道访问；由于 SQLite 为单写者模型，同一数据库文件不应当被本地管理端与服务端同时打开，升级时先停服务、替换二进制、再启动，备份需连同 `-wal`、`-shm` 文件一并复制（`docs/deploy-server.md:215,417`）。

## 4.2.1 车位分配与路径规划算法实现

本系统的车位分配由“路径规划 + 代价评分”两级组成：`GridPlanner` 在栅格地图上计算真实可行驶路径，`SpotAllocator` 用这些路径距离、转弯数、邻近占用与分区负载对候选车位打分，选取得分最优者。各项代价统一以米为量纲，权重因此可以互相比较。

栅格与路网建模。`GridPlanner` 以 0.5 米为默认栅格边长（`src/core/service/GridPlanner.h:22-26`），按场地尺寸向上取整得到行列数，格子中心坐标为 `(col+0.5)×cellSize`（`GridPlanner.cpp:449-468`）。障碍矩形与车位中心格标记为不可通行，起终点落在不可通行格时在半径两米内寻找最近可通行格，既避免车辆穿墙穿位，又不要求入口点精确落在格心。占用场 `OccupancyField` 由 `buildOccupancy` 生成（`GridPlanner.cpp:130-148`）：对每个车位以 12 米为半径、默认 0.35 的权重统计邻近占用车位数，存取时按 `1 + weight × 占用数` 作为通行代价倍率（`GridPlanner.cpp:189-196`），从而让规划器倾向避开拥堵通道。

搜索算法与状态表示。规划器把“格子 + 到达方向”编码为一个状态，状态号为 `格子索引 × 8 + 方向`，用 `thread_local` 的搜索缓冲区与代际标记复用内存、避免每次搜索重新分配（`GridPlanner.cpp:204-236`）。单目标规划使用 A*：启发值取当前格到最近目标格中心的 octile 距离，忽略障碍与转弯惩罚，是可采纳下界，故能得到最优路径；目标集合为空时启发值退化为 0，算法即等价于 Dijkstra 全图搜索（`GridPlanner.cpp:161-186,269-280`）。`planFromToTargets` 用一次多目标搜索同时求出多个候选车位的路线，`plan` 则是单目标特例。车辆默认扩展八邻域，行人寻车退化为四邻域（`GridPlanner.cpp:341-344`）。斜向移动前额外检查两个正交邻居是否可通行，防止车辆从两个障碍物之间的缝隙斜穿过去（`:346-353`）。

后台执行。普通车辆入场通过 `ParkingService::enterAsync` 提交给专用线程池，工作线程只读取布局、车位、策略和权重快照；回到业务线程后再校验配置版本与车位状态，变化时最多重试两次，通过后才占位、写记录和提交 SQLite。队列最多容纳 8 个请求，满载时返回“路径规划繁忙”；构建环境支持 OpenMP 时并行计算不同出入口的路线，否则采用后台串行回退。该设计保持协议返回结构不变，同时避免路径搜索阻塞 AppKit 或 Qt 网络事件循环（`src/core/service/BackgroundEntryPlanner.cpp:88-166`）。

代价函数。每步代价由三部分组成：正交步长为 0.5 米，对角步长为 0.5×√2 米；转弯惩罚为 `2.0 × cellSize`（`GridPlanner.cpp:241-247`），即每次改方向相当于多走 1 米；再加上占用场倍率（`:364-367`）。最终代价同时反映路程、转弯与拥堵，`Route` 同时输出折线点、总距离、总代价与转弯数（`GridPlanner.h:7-12`），供评分与界面使用；堆中过期节点按当前最优代价惰性删除（`GridPlanner.cpp:317`）。

车位评分模型。`SpotAllocator` 默认采用加权代价策略，候选为所有可用车位，车辆已有短时锁时只保留锁定车位，调用方指定车位时只评估该车位（`src/core/service/SpotAllocator.cpp:76-102`）。加权策略先做分区筛选：按类型折算各分区车辆数（已预订按 0.7 辆、停用按 0.5 辆计），综合类型惩罚、车辆数、入口距离与负载率排序，只在负载最低的合适分区内评估（`:109-176`）。每个候选再计算六项代价并求和：

| 评分项 | 默认权重 | 含义 |
| --- | --- | --- |
| 进场路径 | 1.0 | 从最优入口到车位的路线距离（米） |
| 出场路径 | 0.35 | 从车位到各出口按系数加权的距离（米） |
| 通道拥堵 | 2.5 | 邻近 12 米内被占用或预订的车位数 |
| 转弯代价 | 0.4 | 进场与出场转弯数之和 |
| 类型惩罚 | 1.0 | 车型与车位类型不匹配的惩罚 |
| 分区压力 | 0.8 | 场地对角线与该区停车后负载率平方的乘积 |

评分公式在 `makeScore` 中实现（`SpotAllocator.cpp:319-341`），六项加权求和，总分越小越优；最近策略直接以进场路线距离排序（`:322-327`）。类型惩罚为：充电车位对电车减 25 分、对非电车加 50 分，无障碍加 25 分，VIP 加 40 分，普通为 0（`:343-354`）。分区压力项只在加权策略且权重为正时生效，跨区负载更低时无条件优先，负载相同时再比较总分（`:179-317`）。邻近占用统计以车位可达点距离不超过 12 米且非本车位为准（`:356-365`），把“停车后走不远就有车”折算为拥堵代价。

多入口多出口时，进场路径在所有入口中取最优，出场距离按各出口系数加权平均（`SpotAllocator.cpp:178-187,179-317`），靠近出口的车位在离场代价上占优。应急入场在常规分配失败时启用：以“从车位到最近出口的路线距离最短”为准则选择一辆在场车，先按正常流程结算并释放其车位，再把该车位分配给应急车辆，并写入 `emergency_evict` 审计（`ParkingService.cpp:337-418`）。反向寻车则使用不把车位中心视为障碍的行人规划器，从出口反向规划到目标车位，其路线只需供人步行，故采用四邻域（`ParkingService.cpp:89`）。按 165 米 × 85 米的场地与 0.5 米栅格计算，地图为 330×170 格，八邻域状态数约为格子数的 8 倍；借助线程本地缓冲区与多目标搜索，单次分配开销仍可交互。

## 4.2.2 时段预约与定金状态机

时段预约解决“提前占位”需求，规则集中在 `ReservationRule`（`src/core/model/Reservation.h:94-103`），默认值如下表。预约采用“创建即锁位”策略：订单一创建就锁定具体物理车位，到场窗口从创建时刻开始至开始后宽限期结束，逾期未到场则由扫描任务没收定金并释放车位；同一车牌只能有一个未结束预约，同一车位只能被一个有效预约锁定。

| 规则项 | 默认值 | 说明 |
| --- | --- | --- |
| 最远可预约 | 7 天 | 开始与结束时间都必须在未来 7 天内 |
| 最短提前量 | 30 分钟 | 开始时间距当前不足 30 分钟则拒绝 |
| 最短时长 | 30 分钟 | 结束与开始之差小于 30 分钟则拒绝 |
| 到场宽限期 | 30 分钟 | 到场窗口为创建时刻至开始后 30 分钟 |
| 定金 | 20 元 | 创建订单时即收取，无障碍关怀订单免收 |

预约状态机含七种状态：PendingPayment 为已建单待支付，Confirmed 为定金已付待到场，CheckedIn 为已到场且定金转为停车预付款，Completed 为已离场完成抵扣，Cancelled 为已取消并退回定金，NoShow 为爽约没收定金，Expired 为支付超时关闭。状态迁移与副作用如下表，模型层对非法迁移返回 false。

| 当前状态 | 事件 | 目标状态 | 副作用 |
| --- | --- | --- | --- |
| PendingPayment | 支付成功 | Confirmed | 写入收取定金流水，锁位持续到宽限期结束 |
| Confirmed | 到场核销 | CheckedIn | 占用预约车位、生成停车记录 |
| Confirmed | 超过宽限期 | NoShow | 写入没收定金流水并释放车位 |
| Confirmed / PendingPayment | 取消 | Cancelled | 释放车位、写入退回定金流水 |
| CheckedIn | 离场结算 | Completed | 按“定金与停车费取小”抵扣，写入抵扣流水 |
| PendingPayment | 支付超时 | Expired | 关闭订单并释放车位 |

预约创建的关键实现在 `ReservationService::create`（`src/core/service/ReservationService.cpp:50-200`）。函数先做一次爽约扫描，再依次校验时间可表示性、时长、最短提前量、最远预约天数、车辆是否在场、一人一单以及旧版生效预约单，失败即返回中文原因。通过后从全部车位中剔除不可用车位与已被其他有效预约锁定的车位；无障碍关怀订单只考虑无障碍车位，并临时把转弯权重提高为原来的 4 倍再加 1，以优先选择少转弯的直达路线（`:112`），规划完成后立即恢复原权重。随后收取定金并生成收取流水，把预约保存为 Confirmed 并锁定车位（`:134-190`），最后写审计并返回结果；若定金支付失败，流程在写入任何持久化数据前即返回失败。

取消、核销与爽约扫描构成生命周期其余环节。取消只允许发生在预约尚未开始时，即 PendingPayment 或 Confirmed 状态（`ReservationService.cpp:201-256`）；取消时先释放预约车位，再对已收取的定金发起退款并写入退回流水，最后把订单持久化为 Cancelled。到场核销以车牌寻址，只有 Confirmed 状态且在宽限期内才可核销；核销时占用预约车位、写入停车记录并更新订单为 CheckedIn（`:263-333`）。`sweep` 是可重复执行的扫描任务（`:335-393`），一方面把超过宽限期的 Confirmed 预约批量推进为 NoShow 并没收定金，另一方面为“因写库失败等原因尚未锁位”的预约补锁车位，扫描本身不依赖外部定时器，因此服务端启动恢复后也能立即补齐状态。离场时 `prepareExitSettlement` 只对 CheckedIn 订单计算抵扣，取定金与停车费较小值（`:483-503`），`applyExitSettlement` 把订单推进为 Completed 并追加一条抵扣流水，`rollbackExitSettlement` 在数据库事务失败时恢复订单快照并撤销流水（`:504-530`）。若同一车位在历史数据中存在多笔有效预约的锁冲突，重启恢复时 `reconcileConflictingReservations` 会依据物理锁当前归属选择保留者，其余订单取消并退回定金（`:576-670`），确保升级旧库后不会出现“两个车主同时认为自己有车位”。

## 4.2.4 图片识牌集成

识牌采用“检测 + 矫正 + 识别”三段式管线：YOLO 四关键点检测器定位车牌并给出姿态关键点，按关键点做四角透视矫正，把倾斜、旋转的车牌拉正后再送入 PP-OCRv5 识别，最后由规则层校验结果合法性。矫正裁剪遵循统一配方：四角外扩 6%、目标高度 64 像素、宽度限制在 160 至 320 像素、JPEG 质量 95（`docs/lpr.md:45-47`）。若输入本身已是裁剪好的车牌小图，程序会以宽高比 1.8 至 8、高不小于 32 像素、检测置信度不低于 0.9 为条件走预裁剪兜底，直接按整图识别。识别脚本输出一行 JSON，包含车牌号、检测与识别置信度、包围框、是否合法、裁剪方式、四角来源、是否做过翻转检查、车牌颜色（蓝、绿、黄、白、黑、未知）以及是否新能源车牌，调用方据此区分“识别成功”“画面无车牌”“识别失败”三种结果。

服务端提供 TCP 与 REST 两个识牌入口。TCP 动作 `lpr.recognize` 接收 base64 图片并异步识别（`src/network/SmartParkTcpServer.cpp:229-231,257-270`），REST 端点 `POST /api/v1/lpr/recognize` 通过 `handleLprRecognize` 处理上传图片（`src/network/RestGateway.cpp:338-341,1817-1865`）。识别并不在请求线程里同步等待，而是把任务交给 `PlateRecognition` 的进程管理器：管理器默认最多同时运行 2 个识别脚本、排队上限 16 个（`src/network/PlateRecognition.cpp:110-154`），超出队列立即异步失败；每次执行创建独立临时图片、设置 60 秒看门狗，并对 `/bin/sh` 调用 `setsid` 形成独立进程组，超时或取消时向整个进程组发送终止信号，防止 OCR 子进程变成孤儿继续占用 CPU（`:187-216,336-348`）。识别失败时 REST 返回 422 并区分 `NO_PLATE`（画面无车牌）与 `LPR_FAILED`（脚本或环境故障），避免把业务性“没识别到”误报成 500 服务端错误（`RestGateway.cpp:1857-1865`）。若启动时未配置 `--lpr-command`，服务端使用内置 mock 后端，以图片内容的 SHA-256 生成确定性演示车牌，结果中标注 `backend=mock`（`PlateRecognition.cpp:27,95`），使无 Python 环境的机器也能完整演示识牌链路。

性能与精度数据均按文档口径引用，并与数据集、日期、环境绑定。常驻推理性能方面，2026-10-09 在 s1 服务器同一 CPU 环境下，原单图命令行方式对同张图片重复 3 次的中位数为 14.284 秒，常驻推理经真实 TCP 服务端 5 次热请求的中位数为 1.427 秒，热请求加速比 10.01 倍；200 张样例的常驻热请求中位数为 1.308 秒、P95 为 1.342 秒（`docs/lpr-performance.md:11-17`）。macOS 本机 2026-10-10 的链路验证为：单图命令行 7.888 秒，常驻模式两次热请求 0.668 秒与 0.586 秒，中位数 0.627 秒（`:30`）。精度方面，200 张省级样例按整牌精确匹配口径，2026-10-09 的 v1 评测集为 191/200 即 95.5%；2026-10-12 用 9 张新样本替换原有失败样本后重测为 199/200 即 99.5%（`:109-118`）。后者属于选择性替换，不能与 95.5% 直接比较，报告中按“替换后评测集”口径单独标注。历史对比数据为 2026-10-05 的 93.5%（pose 四关键点 + 基础识别器）与 96.0%（省份均衡微调），可作为方案演进参考（`docs/lpr.md:55-63`）。

在实际业务中，识牌结果不会被直接采信，管理端会把识别出的车牌、置信度与裁剪图展示给操作员人工核对，确认后才发起入场或预约核销。服务端识牌是为“客户端不安装模型环境”准备的能力：网页端、道闸终端等远程客户端只需上传照片，由服务端统一执行识别。当前版本 macOS 管理端的车辆作业页固定在本机执行同一套脚本（本地与远程模式都一样，`apps/macos/OperationsViewController.mm:714`），远程模式下只有停车业务交给桥接对象；两种执行方式输出的结果字段完全一致，业务层无需区分。

## 4.2.5 多端同步、断网补报与数据恢复

多端同步的基础是事件总线 `EventHub`。任何业务服务完成状态修改后都会发布一条带名称与负载的事件，事件同时携带来源标记，区分本次变化来自 TCP 入口还是 REST 入口（`src/network/EventHub.h:12-35`）。TCP 入口的处理顺序是“先直接回发业务响应与事件给发起会话，再发布到总线”，REST 入口则只发布到总线，由总线统一转发给 WebSocket 订阅者（`src/network/RestGateway.cpp:158`），这样同一变化不会在某个入口被重复投递。WebSocket 在独立端口提供服务，客户端连接后需在 60 秒内通过查询参数或首帧完成令牌认证，认证后只处理心跳与业务事件，服务端广播的事件与 TCP 事件同构，另增支付成功与支付过期两类支付事件（`RestGateway.cpp:1277,1929-1957`）；TCP 侧则对已认证会话广播入场、离场、预约与补报事件。管理端远程模式依赖这些事件触发快照重拉，网页端则用事件做增量刷新。

断网补报由道闸终端与服务端配合完成。道闸在检测到连接断开后先把进出场事件写入本地 JSONL 队列，重连成功后调用 `gate.replay` 批量提交（`apps/gate/OfflineQueue.h:13-49`）。服务端对每批补报强制 1 至 500 条的上限，只接受过去 30 天至未来 5 分钟内的原始时间戳，按事件类型分别调用带时间参数的入场或离场，保证补报后的停车时长与当时真实时长一致（`src/network/SmartParkTcpServer.cpp:595-681`）。去重规则是：若已存在同车牌、同事件类型且时间戳精确到毫秒相同的记录，该条记为重复；成功应用的记为已应用，条件不满足的记为跳过，三类计数之和等于提交条数，逐条结果随响应返回。终端只有在收到逐条结论后，才删除队列中“从队首开始连续成功”的前缀（`apps/gate/OfflineQueue.cpp:76-110`），部分成功、部分失败时保留失败及其后的事件，下次重试不会重复记账。

数据恢复方面，服务端启动时依次完成数据库打开、建表或迁移、布局签名比对、业务数据加载与一致性校验。车位状态、停车记录与预约分别从 `parking_spots`、`parking_records`、`reservations` 加载，加载器会校验时间范围、车型枚举、定金与流水号的对应关系以及路线文本能否解码，任何非法数据都会导致启动失败而不是带着错误数据继续运行（`src/core/persistence/ParkingRepository.cpp` 的加载函数族）。`ParkingService::restore`（`src/core/service/ParkingService.cpp:899`）恢复记录与车位后，会交叉检查“未离场记录对应的车位必须处于占用状态”“车位记录的占用车牌必须与记录一致”“同一车牌不能有两条在场记录”；`ReservationService::restore`（`src/core/service/ReservationService.cpp:532-575`）则检查已到场预约必须与停车记录和车位归属一致，并把历史锁冲突交给 `reconcileConflictingReservations` 对账修复（`:576-670`）。写路径上，服务端统一采用“先改内存、再写数据库、失败回滚内存”的顺序，离场这类跨表操作使用单事务保证原子性（`ParkingRepository.cpp:716-745`），数据库唯一部分索引作为最后防线兜底并发冲突，因而进程被强制终止后重启，仍能回到“数据库为准、内存与数据库一致”的状态。

## 附A 服务端测试与验证

服务端的自动化测试由 CTest 统一注册，共 12 项，其中与服务端直接相关的是：`smartpark_core_tests` 覆盖领域模型、分配、计费与预约生命周期（`tests/CMakeLists.txt:10`）；`smartpark_background_planning_tests` 覆盖后台预分配（`:14`）；`smartpark_gate_replay_tests` 覆盖道闸状态机与断线补报（`:36-37`）；`smartpark_lpr_concurrency_tests` 覆盖识牌并发与队列（`:51-52`）；`smartpark_rest_e2e` 以真实服务端进程做端到端验证并带 e2e 标签，可用 `ctest -LE e2e` 跳过（`:117-118`）；`smartpark_lpr_script_tests`、`smartpark_lpr_resident_tests`、`smartpark_plate_geometry_tests`、`smartpark_plate_dataset_tests`、`smartpark_plate_color_tests` 覆盖识牌脚本、常驻推理、裁剪几何、数据集与车牌颜色（`:131-142`）。此外还有管理端地图与远程模式两项测试，但它们验证的是客户端侧行为。

REST 端到端脚本 `tests/e2e_rest.sh` 会启动真实服务端、临时数据库与识牌替身，按 13 组用例覆盖主要链路：静态资源与安全头（A）、注册登录与令牌刷新（B）、扫码进入与票据（C）、车位与状态查询（D）、入场到离场再到结算且重复离场必须返回冲突（E）、预约创建与到场核销含规则边界（F）、模拟收银台下单与确认支付（G）、二维码与车牌绑定（H）、WebSocket 握手认证与实时推送（I）、REST 动作事件到达 TCP 客户端（I2）、TCP 协议登录与业务动作（J）、数据一致性相对变化（K）、识牌脚本模式含无车牌与识别不阻塞两类断言（L）。识牌替身故意休眠 2 秒，用于验证识别期间服务端仍能即时响应状态查询，从测试层面锁定“识别不得阻塞事件循环”的设计约束（`tests/e2e_rest.sh:132-134,444-472`）。

服务端自身还提供 `--selftest` 进程内自检模式：临时创建数据库与随机端口，依次验证登录、状态查询、入场、创建预约、到场、离场、分析报表、REST 网关、WebSocket 推送、二维码、断网补报与审计哈希链，任一断言失败即以非零码退出，便于部署后在目标机上快速确认环境可用（`apps/server/main.cpp:147-166`）。

## 附B 服务端问题与经验

- 同步识牌阻塞事件循环。早期识牌接口在请求线程内等待脚本结束，实测识别 13.3 秒期间状态查询被阻塞 12.3 秒并错过心跳；改为“临时图片 + QProcess + 看门狗 + 队列”的异步模型后，识牌不再影响任何在线业务。
- 布局与数据库错配导致车位编号错位。引入布局签名并在启动时比对后，换图纸而忘记换库会直接拒绝启动，比运行中才发现编号对不上更容易定位。
- 预约锁位在相邻时段被复用。旧实现只锁“当前时段”，同一车位可能同时存在两笔有效预约；改为创建即锁位、加唯一部分索引并在重启时对账修复，彻底消除双主。
- REST 写入的事件到不了 TCP 客户端。网页端刚建的预约在远程管理端看不到，根因是 TCP 服务端未订阅事件总线；统一由 `EventHub` 双通道广播后，跨入口一致性得到保证。
- 断网补报可能重复记账。仅按“批成功”删队列会在部分失败时丢事件，改为逐条结论加连续前缀确认，并配合服务端按车牌、类型与毫秒时间戳去重，使补报具备幂等性。
- SQLite 单写者与旧库升级。恢复阶段必须容忍历史结构，采用 `PRAGMA` 检查加惰性 `ALTER` 迁移，同时明确同一数据库文件不可被多个进程同时打开，备份要连同预写日志文件一起复制。

## 附C 建议附录代码片段

- `src/network/Protocol.cpp:20-51` — TCP 帧的 4 字节大端长度前缀编解码与半包处理。
- `src/core/service/GridPlanner.cpp:234-380` — 状态扩展 A* 主循环：方向状态、转弯罚、占用场倍率与堆节点惰性删除。
- `src/core/service/SpotAllocator.cpp:319-341` — 六项加权评分与总分合成。
- `src/core/persistence/ParkingRepository.cpp:716-745` — 离场结算四步单事务。
- `src/network/SmartParkTcpServer.cpp:595-681` — 断网补报的批量校验、去重与逐条结果。

## 附D 事实清单

- SQLite 共 12 张表：parking_spots、parking_records、layout_snapshot、bookings、reservations、deposit_payments、users、auth_tokens、audit_logs、payment_orders、site_tickets、user_plates — `src/core/persistence/ParkingRepository.cpp:57-129`；`src/core/service/UserStore.cpp:66,78`；`src/core/service/AuditLogService.cpp:29`；`src/network/RestGateway.cpp:497,1765,1777`。
- TCP 协议版本 1，单帧上限 9 MiB，单图上限 6 MiB — `src/network/Protocol.h:13,17,19`。
- 帧格式为 4 字节大端长度前缀加 UTF-8 JSON，解码返回 NeedMore/Ok/Invalid — `src/network/Protocol.cpp:20-51`。
- 栅格边长默认 0.5 米，八邻域搜索，转弯惩罚 2.0×cellSize — `src/core/service/GridPlanner.h:22-26`；`src/core/service/GridPlanner.cpp:241-247`。
- 六项评分默认权重：进场 1.0、出场 0.35、拥堵 2.5、转弯 0.4、类型 1.0、分区压力 0.8 — `src/core/service/SpotAllocator.h:13-28`；`src/core/service/SpotAllocator.cpp:319-341`。
- 预约默认规则：最远 7 天、最短提前 30 分钟、最短时长 30 分钟、宽限 30 分钟、定金 20 元 — `src/core/model/Reservation.h:94-103`。
- 计费默认规则：免费 30 分钟、单元 30 分钟、最低 2.5 元、每单元 2.5 元、日封顶 100 元 — `src/core/service/Billing.h:7-13`；`src/core/service/Billing.cpp:17-46`。
- TCP 默认端口 9527，会话超 60 秒无帧断开，连续 5 次登录失败断开 — `src/network/SmartParkTcpServer.h:29-36`；`src/network/SmartParkTcpServer.cpp:320-346`。
- REST 共 34 个 `/api/v1` 端点，默认 HTTP 8080/WS 8081，部署示例用 18080/18081 — `src/network/RestGateway.cpp:225-355`；`docs/deploy-server.md:123-151`。
- 识牌常驻推理相对单图命令行加速 10.01 倍（s1，2026-10-09，同 CPU 环境，200 张 v1 评测集）；v1 整牌精确匹配 191/200=95.5%，替换后评测集 199/200=99.5%（选择性替换，不可直接比较）— `docs/lpr-performance.md:11-17,109-118`。
