# SmartPark 接手说明（2026-09-25）

给下一个 Codex / 终端会话用。先读本文件，再动代码。

## 一句话进度

SmartPark 0.7 远程时间段预约核心（`Reservation`）已完成并全部测试通过；管理端完成登录/注册重做（UserStore + RegisterDialog）与两轮界面迭代——最终主题为**浅色企业风**（白/浅灰底 + 低饱和蓝 #1E5AA8，按 FlashParking/Chase/JustPark 等真实产品调研重做），分区压力算法重写为等效步行米数（连续 30 辆分布 A13/B10/C7）。**改动都在 Mac 本地工作区，未 commit、未 push**（见「仓库状态」）。

## 仓库状态（重要）

- 本地 Mac：`/Users/Zhuanz/Documents/ChatGPT/c++课设/smartpark`，`main` 领先 `origin/main` 两个提交：
  - `ac95d52` 2026-09-23 界面重构、智能决策与分区压力均衡（上一轮，已 commit 未 push）
  - 本轮（2026-09-25 Reservation 核心）**改动还在工作区，未 commit**
- s1：`10.108.17.55` 本轮 ssh 连接超时（可能不在同一网络），未能同步/验证
- GitHub 远程名是 `github`，不是 `origin`；`origin` 指向 s1 的 `~/Cpp-CourseProject_smart_park`
- 用户没明确说就不要 commit / push；push 前记得 s1 上的 `smartpark-my-layout.db` 不能提交

## 本轮做了什么（2026-09-26 第九轮：水位填充分区均衡）

用户实测 4 辆车全堆门口区（二次渐进曲线低占用时代价≈0）。`SpotAllocator::propose()` 选位逻辑改为水位填充：跨分区"放置后负载占比"更低者无条件优先（`zoneBalance = WeightedCost && zonePressure > 0` 才启用；Nearest/权重为 0 保持纯距离），同档内走原加权分。车库 38 辆实测 13 分区负载 25%~62% 均衡分布；testZonePressureBalancing（水位档语义）/SpreadsLoad/ congestion 等回归全过。zonePressureCost 仍在明细中展示但不主导跨区决策。

## 本轮做了什么（2026-09-25 第八轮：主界面毛玻璃）

`Theme.h` 新增 `glassMainWindowStyleSheet()`（毛玻璃版）与 `solidMainWindowStyleSheet()`（原纯色版改名）。MainWindow：`glassMode_`（`SMARTPARK_NO_GLASS=1` 关闭）+ `paintEvent` 绘制整窗 `auroraBackdrop`（按 size 缓存到 glassBackdrop_）；QSS：主窗/壳/内容区/页面全透明，侧边栏 rgba 白 0.59、顶栏/工具栏/状态栏/菜单 0.59-0.65、卡片 0.7-0.8、表格 0.92（可读性优先）。离屏快照已验证效果与可读性。

## 本轮做了什么（2026-09-25 第七轮：总览布局修复）

用户实测反馈总览页裁剪：①总览页包 QScrollArea（dashboardBody，窄窗口可滚动不裁剪）；②折线图 Y 轴刻度自适应小数位（span<2 保留 2 位、<10 保留 1 位，修复全 0 数据时重复"1 元/1 辆"）；③流量条标签改短（1 小时入场/3 小时入场/3 小时离场）；④主题新增 QScrollArea 透明背景规则。

## 本轮做了什么（2026-09-25 第六轮：macOS 原生 API 接入）

子代理调研报告（AppKit/UNUserNotifications/AVFoundation/PDFKit/Network 等）选定 Top-5，全部实现在 `apps/admin/MacSystemBridge.h/.mm`（宏 `SMARTPARK_MACOS_NATIVE`，仅主目标编译；CMake 已链 UserNotifications/AVFoundation/PDFKit）：
1. `macbridge::setMenuBarStatus` — NSStatusItem 菜单栏余位（强引用持静态），refreshDashboard 调用；`SMARTPARK_NO_MENU_BAR=1` 关闭。
2. `macbridge::postNotification` — UNUserNotificationCenter 通知（delegate 静态持有处理前台横幅；授权弹窗 fire-and-forget，勿在主线程等待）。
3. `macbridge::speakChinese` — AVSpeechSynthesizer zh-CN 播报。
4. `macbridge::httpPostJson` — NSURLSession 同步 POST（信号量等待，超时+5s），已填入 RemoteAnalystClient::setTransport——GUI 报告对话框设置 `SMARTPARK_ANALYST_ENDPOINT`/`SMARTPARK_ANALYST_API_KEY` 后可请求远程 LLM 结论（子线程 detached + QTimer 轮询）。
5. `macbridge::exportTextToPdf`（NSTextView→PDFKit，A4+元数据）+ `revealInFinder`。
坑：ObjC 类不能放 C++ namespace；QString::toNSString 在该编译单元不可见（用 stringWithUTF8String）；报告对话框三层 lambda 捕获与 optional 多层解引用易错（state 是裸指针→shared_ptr→optional）。
加固：`--smoke-test` 未传 --db 时强制临时库（默认库有历史数据时恢复弹模态框会卡住自动化——本次冒烟"挂起"的根因，与原生 API 无关）。
调研报告备注：Vision 通用 OCR 不适合中国车牌，LPR 仍走 YOLO+PP-OCRv5 路线。

## 本轮做了什么（2026-09-25 第五轮：创新点 Top-5）

10 个子代理（并发受限后分批）从十角度各提创新点打分，共 40 项；去重合并取前 5 实现：
1. 应急生命通道（9.0）：`ParkingService::emergencyEnter(plate, allowEviction, now)`——应急权重只优化出口距离；满场让位出口最近占用车（正常结算）。CLI `--emergency`；GUI 车辆作业页应急按钮+红色横幅（emergencyBanner_）。
2. 无障碍关怀预约（8.5）：`ReservationService::create(..., accessible)` 免定金（可空 charge_tx，仓储校验已放宽）、宽限×2、仅 Accessible 车位、转向权重×4；`reservations.accessible` 列；CLI `--reserve --accessible`。
3. 哈希链审计（8.5）：`AuditLogService`（audit_logs 表，hash=SHA256(prev+内容)，verifyChain 定位断链）。CLI `--audit`/`--audit-verify`；GUI 经 auditService_ 接线（MainWindow 析构移到 cpp 因 unique_ptr 不完整类型）。
4. 反向寻车（8.5 合并）：`ParkingService::findCar(plate)`——pedestrianPlanner_（空车位障碍集）从最近出入口步行到车位；CLI `--find`。
5. 剧本演示（8.5）：`DemoDirector` 16 步脚本 + 唯一车牌可重复运行（默认布局无无障碍位时自动回退普通预约）；CLI `--demo-script`；GUI 快捷操作"数据分析报告"对话框。
新增 6 个专项测试。注意：Reservation ctor 多了 accessible 尾参；saveReservationOrder 第二参改为 `const DepositPayment *`（可空）。
未实现备选（8.0 档）：有序充电调度、道闸仿真、LED 引导屏、峰谷定价沙盘、预测式选位、异常检测、RBAC、事件总线、潮汐通道、预缴费倒计时；TCP 网关 8.5 分留给 P1。

## 本轮做了什么（2026-09-25 第四轮：数据分析接口）

1. `src/core/service/AnalyticsEngine.h/.cpp`：本地数据分析引擎。从 ParkingService 重建近 72h 逐时占用率序列（由停车记录反推），本地小模型 = OLS 线性回归（`fitLinear`，可静态测试）拟合趋势 + 外推未来 6h 占用率；规则引擎生成 `AnalysisFinding`（预测/高峰/收入/分区/预约/数据质量）与建议，摘要 2-3 句中文。数据不足时如实报 DataQuality，不编造。
2. `src/core/service/RemoteAnalystClient.h/.cpp`：预留远程分析接口（OpenAI 兼容 chat/completions）。输出与本地引擎同构的 AnalysisReport；传输层 `setTransport()` 注入（P1 接 HTTP 后启用），API key 从环境变量 `SMARTPARK_ANALYST_API_KEY` 读，端点 `SMARTPARK_ANALYST_ENDPOINT`。快照只含聚合指标不含车牌。
3. CLI `--analyze`：输出报告 + 远程接口状态；无预约/分析命令时照常跑演示。
4. 测试 +3：OLS 正确性（斜率/截距/R²/样本数下限/常量序列）、报告结论（占用率/高峰/收入/预测边界/空数据诚实提示）、远程接口（未配置禁用、注入假传输验证请求组装与结论解析）。
5. 模型选型建议（写给用户，也在 README）：当前用本地 OLS+规则（已实现）；进阶本地用 LightGBM→ONNX→ONNX Runtime（与 LPR 部署路线统一）；远程 API 推荐 GLM-4-Flash/DeepSeek 等 OpenAI 兼容接口；LSTM/Prophet 数据量不够不建议。

## 本轮做了什么（2026-09-25 第三轮：压力算法重做 + 界面去概念稿化）

1. `SpotAllocator` 分区压力算法重写：旧版 `6.0 × 放置前负载` 上限 6 分，被路径项（米）碾压，车把最近区塞满才外溢。新版 `weights.zonePressure(默认 0.8) × 场地对角线 × 放置后负载占比²`，与路径项同量纲（等效步行米数），二次曲线让满区边际代价递增。`AllocationWeights.zonePressure` 语义已变，注释在 header。
2. 新增测试 `testZonePressureSpreadsLoadAcrossZones`（默认布局 30 辆，断言每区 ≥5 且 ≤15）并重写 `testZonePressureBalancing` 适配新语义；实测首 30 辆 A13/B10/C7。
3. `Theme.h` 按子代理调研报告重写为浅色企业风：白/浅灰底、主蓝 #1E5AA8（主按钮/链接/选中态）、白底侧边栏、表格细分隔线、badge 浅底色块、图表灰网格、圆角 4-6px；样式表用 `@token@` + `applyPalette()` 替换（不要再用 .arg 链——重复占位符只替换第一处，已踩过坑）。
4. 文案去营销味：登录按钮“登录”、注册“注册新账号”，删“实时运营控制台/智能决策/本地运营终端”等措辞；预测行改“预测：60 分钟后占用率 X%（置信度 Y%）”。
5. 注释精简：Reservation*、UserStore、LoginDialog、NativeEffects、SpotAllocator 中教科书式注释收敛为短注释（去 AI 味）。
6. 调研报告要点（子代理产出，已存于会话）：企业系统全部浅色主题、主色低饱和、按钮动词短语、高密度表格 + 完整控件状态、出现对账/复核/月卡等行业词；概念稿的破绽是深色渐变大圆角卡片、单色按钮、假数据。

## 本轮做了什么（2026-09-25 第二轮：界面与登录）

1. `apps/admin/Theme.h`：统一主题——曜石石墨（侧边栏 #1B1D22）+ 香槟金（强调 #C9A45C）+ 暖纸浅色面（#F6F5F2），替换全部 #1D4E89 蓝色系；含主窗口/认证对话框样式表函数与 `auroraBackdrop()`（QGraphicsBlurEffect 真实高斯模糊光斑，非 macOS 回退）。MainWindow、ChartWidgets、Login/Register 对话框全部取值于此。
2. `apps/admin/NativeEffects.h/.mm`：Objective-C++ 调 AppKit `NSVisualEffectView`（HUDWindow 材质 + BehindWindow 混合）给登录/注册窗口做系统级高斯模糊。仅 admin 主目标编译（`SMARTPARK_HAS_NATIVE_VIBRANCY` 宏 + AppKit framework）；测试目标走内联空实现。`SMARTPARK_NO_VIBRANCY=1` 强制回退自绘光斑。
3. `apps/admin/UserStore.h/.cpp`：users 表（与停车数据同库）、盐化 12000 轮 SHA-256 口令摘要 + 常量时间比较、注册/验证/重复拒绝、空库自动播种 admin/smartpark、last_login 记录。
4. `apps/admin/RegisterDialog.h/.cpp`：账号+密码+确认密码注册，实时校验/强度提示/标红。
5. `LoginDialog` 重写：UserStore 认证、空字段分别提示并标红、账号不存在与密码错误分开提示、连续 5 次错误锁定 30 秒倒计时、👁 密码可见切换、记住账号、注册入口回填账号。
6. `apps/admin/main.cpp`：登录循环前打开 UserStore（失败弹窗退出）。
7. CMake：apps/admin 重构为 `ADMIN_SOURCES` 列表 + APPLE 分支 `enable_language(OBJCXX)` + AppKit；测试目标加 UserStore/LoginDialog/RegisterDialog 源。
8. Admin 测试新增 3 项：UserStore 播种/登录（含重启持久化）、注册校验、LoginDialog 交互（空字段→错误密码剩余次数→演示账号通过），并保存离屏渲染快照 /tmp/login-grab.png。

### ⚠️ 截图验证陷阱 + 原生毛玻璃定论（重要教训）

1. 终端没有 macOS「屏幕录制」权限时，`screencapture` 会**省略其他应用的窗口**（只剩桌面壁纸+菜单栏）。offscreen 的 `QWidget::grab()` 快照不受影响。
2. **定论（用户实机截图确认）**：Qt 6.8 半透明窗口（WA_TranslucentBackground）+ NSVisualEffectView 组合在 macOS 15.6 实机上会把 Qt 控件层整体盖住——登录窗口只剩模糊背景、卡片/输入框全部不可见。**默认已改为不透明自绘光斑背景**（`Theme::auroraBackdrop`），原生毛玻璃仅 `SMARTPARK_NATIVE_BLUR=1` 实验启用。除非重验，不要再把原生毛玻璃设为默认。

## 本轮做了什么（2026-09-25 第一轮：Reservation，详见 README 最近工作记录）

1. `src/core/model/Reservation.h/.cpp`：`Reservation`（状态机 `PendingPayment -> Confirmed -> CheckedIn -> Completed` + `Cancelled` / `NoShow` / `Expired`）、`DepositState`（Pending/Refunded/Forfeited/Applied）、`DepositPayment` 流水、`ExpectedRoute` 快照文本序列化、`ReservationRule`（7 天 / 提前 30min / 最短 30min / 宽限 30min / 锁位提前 30min / 定金 20 元）。
2. `src/core/service/FakePaymentGateway.h/.cpp`：确定性模拟支付（PAY/RFD/FFT 交易号），`failNextCharge()` 注入失败。
3. `src/core/service/ReservationService.h/.cpp`：`create`（校验+临时副本选位+冲突检查+收定金）、`cancel`（开始前退定金）、`checkIn`（窗口 `[开始-30min, 宽限截止]`）、`sweep`（延迟锁位+爽约结算）、`prepare/apply/rollbackExitSettlement`（离场抵扣三段式，保证内存与 SQLite 一致）、`restore`（重启恢复校验）。是 `ParkingService` 的 friend，直接访问 spots_/allocator_/records_/repository_。
4. `ParkingService`：构造函数加 `ReservationRule` 参数并持有 `unique_ptr<ReservationService>`（析构函数移到 .cpp）；`enter()` 自动车牌匹配到场；`leave()`/`release()` 走 `closeActiveRecord()` 统一计费+定金抵扣+事务持久化；`expireReservations()` 跳过时段预约锁定的车位；`createBooking()` 拒绝有时段预约的车牌；`reservations()` 访问器（const/非 const）。
5. `ParkingRepository`：新表 `reservations`（含 `route` 列存预期路线快照）与 `deposit_payments`；部分唯一索引 `idx_reservations_open_plate` 兜底同车牌唯一开放订单；事务方法 `saveReservationOrder` / `saveReservationStatus` / `saveReservationPayment` / `saveReservationCheckIn` / `saveExitWithReservation`。
6. CLI：新增时段预约命令与演示（演示含「3h 应收 25 元，定金抵 20 实收 5」与爽约没收）。
7. 测试：`tests/core_model_tests.cpp` 新增 8 个测试（模型状态机/路线序列化、创建校验与冲突、延迟锁位/到场/抵扣、爽约与取消、支付失败、入场自动确认、跨重启恢复、模拟网关）。
8. README 已同步（当前范围、远程预约系统章节、当前进度、里程碑 8 ✅、最近工作记录、文件职责表、CLI 示例）。

## 语义要点（改代码前必读）

- **延迟锁位**：下单不占实体车位；`sweep()` 在 `[开始-30min, ...)` 才把车位 `reserve` 到宽限截止。同一车位可接受互不重叠时段。
- **宽限期从开始时间算**：`graceDeadline = startTime + 30min`，不是结束时间。爽约 = sweep 时超过宽限截止仍未 CheckedIn。
- **到场窗口**：`[startTime - lockLeadTime, graceDeadline]`，可提前 30 分钟到场。
- **定金抵扣**：离场 `fee = max(0, 应收 - min(定金, 应收))`，余额不退；`Applied` 流水金额是实际抵扣额。
- **离场三段式**：`prepareExitSettlement`（生成 Apply 流水）→ `applyExitSettlement`（内存置 Completed/Applied+入账）→ 持久化 `saveExitWithReservation` → 失败则 `rollbackExitSettlement`。
- CLI 默认 60 车位布局不要改；`ParkingLayout::garageDescription()` / `data/garage-6f.txt` / `my-layout.txt` 三处文本同步的规矩不变。
- `ReservationResult.reservation` 是下单时刻快照；测状态要用 `service.reservations().findOpen()/findCheckedIn()/reservations()` 重新取。

## 本轮验证（Mac，Qt 6.8.3，`build/qt`）

- `cmake --build build/qt --parallel` 通过（含 OBJCXX）
- `ctest --test-dir build/qt`：**4/4 通过**（cli_demo、cli_booking、core_tests、admin_tests；admin 含 3 个新账号/登录测试）
- CLI 端到端：`--reserve`（收定金）→ 跨重启 `--arrive`（锁位+占位）→ `--reservations`（状态跨重启正确）→ `--rsv-cancel`（退款）全通
- `data/garage-6f.txt` 75 车位图纸：`Remaining spots: 51/75`，`RESULT: PASS`
- 登录界面 offscreen 快照（/tmp/login-grab.png）：石墨卡片 + 金色按钮 + 注册链接渲染正确
- **毛玻璃与新配色的真机效果未经人眼确认**（终端无屏幕录制权限，截图不可信）；请用户打开 `smartpark_admin` 看一眼，若窗口异常可 `SMARTPARK_NO_VIBRANCY=1` 回退自绘光斑并反馈
- GUI 没有新增预约面板（范围是核心+CLI）；admin 回归测试通过

## 在 s1 上怎么跑（网络恢复后）

```bash
ssh s1
cd ~/Cpp-CourseProject_smart_park
git status -sb          # 应与本地对齐后再验证
cmake --preset cli && cmake --build --preset cli --parallel
ctest --preset cli-tests --output-on-failure
./build/cli/apps/cli/smartpark_cli data/garage-6f.txt --reset --db /tmp/smartpark-garage.db
```

期望：车位数 75，`RESULT: PASS`，新增测试全绿。

## 在 Mac 上看 GUI

```bash
cd "/Users/Zhuanz/Documents/ChatGPT/c++课设/smartpark"
QT_PREFIX=/Users/Zhuanz/Qt/6.8.3/macos scripts/build-admin.sh
QT_PREFIX=/Users/Zhuanz/Qt/6.8.3/macos scripts/run-admin.sh --db /tmp/smartpark-gui-garage.db
```

## 不要做的事

- 不要改 CLI 默认 60 车位
- 不要把 `model/` 或 `smartpark-my-layout.db` 提交进去
- 用户没明确说就不要 commit / push
- 不要为「更像真实支付」接外部服务；支付失败路径只走测试钩子
- 不要跳过 TCP 去做 LPR

## 下一轮建议

1. **先 commit/push 并在 s1 同步验证**（需用户确认；本地领先 2 个提交）。
2. **P1 — TCP 协议与服务端**：先写 `docs/tcp-protocol.md`（登录/心跳/预约查询/创建/取消/入场/离场事件，参考 DB4403/T 313 与北京 DB11/T 3001 的请求应答+重传模式），再用 `QTcpServer` 实现 `apps/server`，把 `ReservationService` 的 API 暴露为协议方法。
3. **P2 — User Client + Gate Terminal**：用户端提交预约并画预期路线；Gate 用假识别（手动输入车牌）核销预约。
4. 预约 GUI 面板（Admin 加时段预约表格）可作为 GUI 侧补齐项。

## 路线回顾

```
① 项目骨架                         完成
② Qt 工程 C++17 + Qt6 + CMake      完成
③ Vehicle + ParkingSpot            完成
④ 内存停车业务                      完成
⑤ 停车场 GUI                        完成，建筑图 75 车位
⑥ SQLite 持久化                     完成
⑦ 收费 BillingService               完成
⑧ 远程时间段预约核心 Reservation     完成（本轮；TCP 接口未开始）
⑨ 出入口终端 Gate                   未开始
⑩ 车牌识别 OpenCV + 方案 B          资料和训练脚本有了，产品未接入
⑪ 用户端 / 可视化统计               图表/洞察已有（09-23），用户端未开始
```

## 给下一个 agent 的第一句

「继续 SmartPark。先读 `smartpark/handoff.md`。Reservation 时段预约核心 + 界面换装（石墨+金、原生毛玻璃、登录/注册重做）都已完成、4/4 测试通过，但改动未 commit（本地领先 origin 两个提交）。先和用户确认界面效果（毛玻璃真机效果没人眼验收过）与提交/推送，然后按 README P1 做 `docs/tcp-protocol.md` + QTcpServer。不要改 CLI 默认 60 车位，不要动 model/。」
