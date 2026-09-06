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

## 当前范围：SmartPark 0.5

当前已完成：

- C++17、CMake 与 Qt 6 Widgets 的基础工程配置。
- `Vehicle`、`ParkingSpot`、`ParkingLayout`、`ParkingRecord` 核心模型。
- 独立的 `SpotAllocator`：`ParkingService` 只负责状态机，选位、评分和路径规划从业务层拆出。
- 默认 60 车位、3 个矩形分区的自动分配服务。
- 用户自定义多矩形停车场布局，支持不同车位长宽、通道位置、车位类型和多个出入口。
- 基于 0.5 米栅格的 A* / 一次搜索到多目标 Dijkstra，自动避开车位障碍并生成入口/出口路线。
- 拥堵写入 A* 边权，同时保留附近占用数作为车位评分项。
- `WeightedCost` 默认策略和 `Nearest` 对照策略。
- 预留状态机：`Available -> Reserved(ttl) -> Occupied -> Available`。
- CLI 自动演示与 Qt GUI 实时车位图、路线绘制、布局编辑和策略切换。
- `ParkingService::enter()` / `leave()` / `reserve()` 入离场与预留流程。
- `DatabaseManager` 与 `ParkingRepository`：SQLite 建表、入场/离场/预约持久化和重启恢复。
- `Persistence` 辅助类：统一管理数据库连接，CLI 与 Qt Admin GUI 默认接入 SQLite，跨重启恢复车位状态与停车记录。
- CLI 支持 `--db <路径>` 指定数据库、`--reset` 清空数据库后演示。
- Qt Admin GUI 支持 `--db <路径>`，应用布局时若与数据库签名不一致会提示并可选重置数据库。

下一步将实现收费服务、TCP 服务端与出入口终端。

尚未接入 TCP 通信、OpenCV、HyperLPR3、多线程、用户端或统计图表。调研来源与明确不做的方案见 `docs/research-sources.md`。

## 当前进度

截至 2026-09-07，项目处于 **SmartPark 0.5**：

- 已完成核心模型、60 车位自动分配、自定义多矩形布局、栅格 A* / Dijkstra 路线、拥堵边权、预留 TTL、CLI 与 Qt GUI。
- SQLite 持久化已接入 CLI 与 Admin GUI，支持跨重启恢复车位状态、预约和停车记录。
- 在 `s1` 上验证：CLI 与 Qt 构建通过，`cli-tests` / `qt-tests` 均为 2/2 通过，GUI offscreen 启动正常。
- CLI 默认持久化可重复运行，连续运行至 60/60 满场后仍稳定输出 `RESULT: PASS`。

尚未完成：`BillingService`、TCP Server、Gate Terminal、OpenCV + HyperLPR3、用户端与统计图表。

## 后续发展路线

按调研结论推进，优先级从高到低：

1. **P0 — BillingService**：费率规则、免费时长、日封顶、离场宽限，独立于 UI 与服务端。
2. **P0.5 — TCP 协议与服务端**：先写 `docs/tcp-protocol.md`，再用 `QTcpServer` 实现登录、心跳和入场/离场事件。
3. **P1 — Gate Terminal + Fake LPR + 离线队列**：先以假识别打通出入口，再补齐断线缓存和补报。
4. **P2 — OpenCV + HyperLPR3 + CCPD 评测**：协议稳定后接入真实车牌识别，并用 CCPD 做可复现评测。
5. **P3 — 支付、图表、用户端**：在前述链路稳定后再扩展外围能力。

明确不做：EasyPR、纯云端计费、微信小程序、Go 微服务，以及把 LPR 放进 `ParkingService`。

## 技术栈

- C++17
- Qt 6 Widgets
- CMake
- SQLite 与 Qt SQL
- QTcpServer 与 QTcpSocket（后续）
- OpenCV 4 与 HyperLPR3（后续）
- QThread、std::thread 与 STL
- Qt Charts（后期）

## 目录结构

```text
.
├── apps/
│   ├── admin/       # 管理员端 Qt GUI：布局编辑、车位图、路线显示
│   ├── cli/         # 终端演示程序：自动分配与自定义布局验证
│   ├── server/      # SmartPark 服务端入口（预留）
│   └── gate/        # 出入口终端入口（预留）
├── docs/            # 调研来源与路线说明
├── src/
│   ├── core/
│   │   ├── model/   # Geometry、Vehicle、ParkingSpot、ParkingLayout、ParkingRecord
│   │   ├── persistence/ # DatabaseManager、ParkingRepository、Persistence
│   │   └── service/ # GridPlanner、SpotAllocator、ParkingService
│   ├── database/    # 数据库连接与仓储层
│   ├── network/     # TCP 协议与通信实现
│   └── lpr/         # 车牌识别集成
├── resources/
│   ├── icons/       # 图标资源
│   ├── styles/      # Qt 样式表
│   └── images/      # 图片资源
├── sql/             # 数据库建表与初始化脚本
└── tests/           # 单元测试与集成测试
```

## 开发里程碑

1. ✅ 工程跑起来：完成 CMake、Qt 6、C++17 和主窗口。
2. ✅ 纯 C++ 停车核心：实现停车位、车辆、停车记录和入场/离场流程。
3. ✅ 停车场 GUI：实时展示车位状态、自动分配结果和行驶路线。
4. ✅ 分配器重构：独立选位算法、拥堵边权、预留 TTL、车位类型和多出入口。
5. ✅ SQLite 持久化：核心层已完成，CLI/GUI 已接入并支持重启恢复。
6. ⬜ 收费系统：根据停车时长计算费用。
7. ⬜ 服务端：以 TCP 建立管理员端与服务端架构。
8. ⬜ 出入口终端：手动输入车牌并通过服务端处理业务。
9. ⬜ 车牌识别：接入 OpenCV 与 HyperLPR3。

## 最近工作记录

最近一轮完成“客户端 SQLite 持久化接入”：

- 新增 `Persistence` RAII 封装，统一数据库连接与默认路径。
- CLI 新增 `--db <路径>` 与 `--reset`；默认演示使用唯一车牌，可重复运行并处理满场状态。
- Admin GUI 新增 `--db`，布局签名不一致时可重置数据库或安全降级，避免空指针。
- 修复预约过期未落库、Disabled 状态恢复、active record 唯一性、车位集合校验和毫秒级时间边界。
- 对应提交：`bf0aa55 feat: connect clients to sqlite persistence`、`6615bc3 docs: add smart parking research sources`。

## 文献调研

调研来源、评分和取舍记录在 `docs/research-sources.md`。当前重点参考：

- [DB4403/T 313 智慧停车业务数据与接口规范](https://amr.sz.gov.cn/attachment/1/1566/1566543/9772236.pdf)：TCP 登录、心跳和事件清单。
- [PARCS 五层架构与费率引擎](https://parkingpaymentguide.com/payment-systems/how-parking-payment-systems-work/)：费率引擎、离线队列和宽限期。
- [MDPI SPMS 参考架构](https://www.mdpi.com/2079-8954/13/2/70)：Server / Gate / Admin 分层。
- [佛山禅城 2026 停车收费新规](https://www.163.com/dy/article/L5OVP5IU05129QAF.html)：15 分钟计费、30 分钟免费和日封顶。
- [HyperLPR3](https://github.com/szad670401/HyperLPR)：后续车牌识别后端，只放在 Gate，不进入 core。
- [CCPD](https://github.com/detectRecog/CCPD)：中文车牌识别评测集。
- [北京 DB11/T 3001 ETC 停车场接口](https://jtw.beijing.gov.cn/xxgk/flfg/jthy/201912/P020191231388015746585.pdf)：Gate 作为 TCP 客户端、请求应答与重传。
- [深圳公共智慧停车平台数据接入规范](https://jtys.sz.gov.cn/attachment/1/1596/1596572/12076334.pdf)：心跳、NTP 和断线补报。

以上来源用于提炼接口、计费规则和架构边界，不直接复制外部项目代码。

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
region C 71 40 10 2 1.2 5.5 6 left accessible
```

字段含义：

- `site 宽 高`：场地边界，单位米。
- `entrance x y` 和 `exit x y`：入口与出口坐标，可重复多条。
- `region 名称 x y 行数 列数 车位宽 车位长 通道宽 left|right [类型]`：一个矩形分区；每一列都是“一条通道 + 一排车位”的独立车 bay，每个分区可以使用不同车位尺寸。
- 类型可选 `normal`、`charging`、`accessible`、`vip`，也支持 `type=charging` 写法；省略时默认为 `normal`。
- `#` 开头的行是注释。

## 终端验证（无需 Qt、Conda 或图形桌面）

项目使用 CMake Presets 统一管理构建目录。所有构建产物都放在根目录的 `build/` 下，其中 `build/cli` 是终端版本，`build/qt` 是 Qt 版本。在项目根目录执行，需 CMake 3.21+、Ninja 和支持 C++17 的编译器：

```bash
/usr/bin/cmake --preset cli-debug
/usr/bin/cmake --build --preset cli-debug --parallel
/usr/bin/ctest --preset cli-tests
./build/cli/apps/cli/smartpark_cli
./build/cli/apps/cli/smartpark_cli my-layout.txt
./build/cli/apps/cli/smartpark_cli --db /tmp/smartpark.db --reset
```

如果需要全新构建，可先删除 `build/cli` 或整个 `build/` 目录。CLI 与 Qt 的 CMake 缓存分别保存在 `build/cli` 和 `build/qt`，互不影响。

不带参数时使用内置 60 车位布局；带文本文件参数时加载自定义布局。程序自动执行验证，无需输入。它分配 3 辆车、释放 1 辆车并再次自动分配，同时打印车位编号、类型、入口距离、出口距离、附近占用数和综合评分；成功输出 `RESULT: PASS` 并返回 0。

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
| `src/core/model/Vehicle.h` | 声明车辆类型、车辆数据与只读访问接口。 |
| `src/core/model/Vehicle.cpp` | 实现车辆构造、非空车牌校验和数据访问。 |
| `src/core/persistence/DatabaseManager.h/.cpp` | SQLite 连接与生命周期管理。 |
| `src/core/persistence/ParkingRepository.h/.cpp` | 建表、入场/离场/预约持久化与状态恢复。 |
| `src/core/persistence/Persistence.h/.cpp` | CLI/GUI 共用的数据库封装，提供默认路径与 RAII 生命周期。 |
| `src/core/model/ParkingSpot.h` | 声明车位状态、类型、当前车辆、预留和占用接口。 |
| `src/core/model/ParkingSpot.cpp` | 实现单个车位的状态转换，拒绝重复占用或释放。 |
| `src/core/service/GridPlanner.h/.cpp` | 实现障碍感知栅格 A*、多目标搜索和拥堵边权。 |
| `src/core/service/SpotAllocator.h/.cpp` | 独立选位、策略、评分和路径缓存。 |
| `src/core/service/ParkingService.h/.cpp` | 实现入场、离场、预留 TTL、剩余车位和历史记录查询。 |
| `tests/CMakeLists.txt` | 构建并注册模型单元测试。 |
| `tests/core_model_tests.cpp` | 验证模型、预留、类型匹配、拥堵绕行和多入口选择。 |
| `apps/admin/CMakeLists.txt` | 构建可选 Qt 管理员端，Qt 自动处理只作用于该目标。 |
| `apps/admin/main.cpp` | 独立 GUI 入口，解析 `--db` 参数并启动主窗口。 |
| `apps/admin/MainWindow.h/.cpp` | 实现布局编辑、车位图、自动分配、路线显示与数据库恢复/重置交互。 |

运行流程：系统启动 `smartpark_cli` → 解析布局 → 构建障碍栅格 → `SpotAllocator` 按策略为候选车位计算路线和评分 → 选择最优车位并占用或预留 → 输出路线和状态 → 检查结果并返回退出码。

## Linux 构建与运行

当前 Linux 开发环境使用独立的 Conda 环境 `smartpark-qt68`，其中包含 Qt 6.8.4、CMake、Ninja 和 C++ 编译器。

```bash
source ~/miniforge3/etc/profile.d/conda.sh
conda activate smartpark-qt68

cmake --preset qt-debug
cmake --build --preset qt-debug --parallel
ctest --preset qt-tests
./build/qt/apps/admin/smartpark_admin
```

如果当前终端没有图形显示，可以使用 Qt 的 offscreen 平台插件做启动检查：

```bash
QT_QPA_PLATFORM=offscreen ./build/qt/apps/admin/smartpark_admin
```

该 Conda 环境用于构建 Linux x86_64 版本。Windows 和 Android 版本后续需要分别使用对应平台的 Qt Kit。
