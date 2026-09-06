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

## 当前范围：SmartPark 0.3

当前已完成：

- C++17、CMake 与 Qt 6 Widgets 的基础工程配置。
- `Vehicle`、`ParkingSpot`、`ParkingLayout` 与 `ParkingService` 核心模型。
- 默认 60 车位、3 个矩形分区的自动分配服务。
- 用户自定义多矩形停车场布局，每个分区可有不同的车位长宽和通道位置。
- 基于 0.5 米栅格的 A* 路径规划，自动避开车位障碍并生成入口/出口路线。
- 自动选位综合入口距离、出口距离和 12 米范围拥堵度，减少局部拥堵。
- CLI 自动演示与 Qt GUI 实时车位图、路线绘制、布局编辑。
- `ParkingRecord` 停车记录，包含车牌、车位、入场/离场时间和停车时长。
- `ParkingService::enter()` / `leave()` 完整入离场流程，自动创建和关闭停车记录。
- 默认车位编号统一为 `A001`～`A060`，并支持查询剩余车位、占用车位和历史记录。

下一步将实现 SQLite 持久化、收费服务、TCP 服务端与出入口终端。

本阶段不接入 SQLite、TCP 通信、OpenCV、HyperLPR3、多线程、用户端或统计图表。

## 技术栈

- C++17
- Qt 6 Widgets
- CMake
- SQLite 与 Qt SQL（后续）
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
├── src/
│   ├── core/
│   │   ├── model/   # Geometry、Vehicle、ParkingSpot、ParkingLayout、ParkingRecord
│   │   └── service/ # GridPlanner、ParkingService
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

1. 工程跑起来：完成 CMake、Qt 6、C++17 和主窗口。
2. 纯 C++ 停车核心：实现停车位、车辆、停车记录和入场/离场流程。
3. 停车场 GUI：实时展示车位状态、自动分配结果和行驶路线。
4. SQLite 持久化：重启后保留停车数据。
5. 收费系统：根据停车时长计算费用。
6. 服务端：以 TCP 建立管理员端与服务端架构。
7. 出入口终端：手动输入车牌并通过服务端处理业务。
8. 车牌识别：接入 OpenCV 与 HyperLPR3。

## 自动分配算法

停车场由多个互不重叠的矩形分区组成。每个分区可独立设置行数、列数、车位宽、车位长、通道宽和通道位于左侧或右侧，因此可组合出长宽不同的车位和异形整体布局。

构建布局时，系统验证：

- 分区必须完全位于场地边界内。
- 分区之间不能重叠。
- 通道宽度至少 2.5 米。
- 所有车位必须能从入口规划出可达路线。

路径规划使用 0.5 米栅格 A* 算法，车位矩形为障碍，通道和空地为可通行区域。A* 支持 8 方向移动，禁止斜穿障碍，并增加转向代价，使路线更平顺。自动分配对每个空闲车位计算：

```text
score = entrance_distance + 0.35 * exit_distance + 2.5 * nearby_occupied_spots
```

选择评分最低的车位。这样可以兼顾入场效率、离场便利性和局部拥堵程度，避免大量车辆集中停在同一区域。

## 自定义布局格式

CLI 和 GUI 共用以下文本布局格式：

```text
site 100 60
entrance 0 30
exit 100 30
region A 5 8 10 2 1.2 5.5 6 left
region B 38 24 10 2 1.4 6.0 6 right
region C 71 40 10 2 1.2 5.5 6 left
```

字段含义：

- `site 宽 高`：场地边界，单位米。
- `entrance x y` 和 `exit x y`：入口与出口坐标。
- `region 名称 x y 行数 列数 车位宽 车位长 通道宽 left|right`：一个矩形分区；每一列都是“一条通道 + 一排车位”的独立车 bay，每个分区可以使用不同车位尺寸。
- `#` 开头的行是注释。

## 终端验证（无需 Qt、Conda 或图形桌面）

项目使用 CMake Presets 统一管理构建目录。所有构建产物都放在根目录的 `build/` 下，其中 `build/cli` 是终端版本，`build/qt` 是 Qt 版本。在项目根目录执行，需 CMake 3.21+、Ninja 和支持 C++17 的编译器：

```bash
/usr/bin/cmake --preset cli-debug
/usr/bin/cmake --build --preset cli-debug --parallel
/usr/bin/ctest --preset cli-tests
./build/cli/apps/cli/smartpark_cli
./build/cli/apps/cli/smartpark_cli my-layout.txt
```

如果需要全新构建，可先删除 `build/cli` 或整个 `build/` 目录。CLI 与 Qt 的 CMake 缓存分别保存在 `build/cli` 和 `build/qt`，互不影响。

不带参数时使用内置 60 车位布局；带文本文件参数时加载自定义布局。程序自动执行验证，无需输入。它分配 3 辆车、释放 1 辆车并再次自动分配，同时打印车位编号、入口距离、出口距离、附近占用数和综合评分；成功输出 `RESULT: PASS` 并返回 0。

当前版本不包含数据库、网络或车牌识别，状态和车辆只保存在内存中，程序结束后丢弃。

### 文件职责与运行流程

| 文件 | 用途 |
| --- | --- |
| `CMakeLists.txt` | 设置 C++17、CTest 和各构建目标；关闭 `SMARTPARK_BUILD_ADMIN` 后不查找 Qt。 |
| `CMakePresets.json` | 定义 CLI 与 Qt 的标准构建目录、构建参数和测试命令。 |
| `apps/cli/CMakeLists.txt` | 构建 `smartpark_cli`，链接核心库并注册终端演示测试。 |
| `apps/cli/main.cpp` | 终端入口，加载默认或自定义布局并演示自动分配。 |
| `src/core/CMakeLists.txt` | 将模型实现编译为 `smartpark_core` 静态库。 |
| `src/core/model/Geometry.h` | 定义坐标、矩形和几何工具。 |
| `src/core/model/ParkingLayout.h/.cpp` | 解析自定义布局并生成车位矩形。 |
| `src/core/model/ParkingRecord.h/.cpp` | 保存一次停车的车牌、车位、时间、时长和费用。 |
| `src/core/model/Vehicle.h` | 声明车辆类型、车辆数据与只读访问接口。 |
| `src/core/model/Vehicle.cpp` | 实现车辆构造、非空车牌校验和数据访问。 |
| `src/core/model/ParkingSpot.h` | 声明车位状态、当前车辆及占用/释放接口。 |
| `src/core/model/ParkingSpot.cpp` | 实现单个车位的状态转换，拒绝重复占用或释放。 |
| `src/core/service/GridPlanner.h/.cpp` | 实现障碍感知栅格 A* 寻路。 |
| `src/core/service/ParkingService.h/.cpp` | 实现自动选位、拥堵评估、车辆入场/离场、剩余车位和历史记录查询。 |
| `tests/CMakeLists.txt` | 构建并注册模型单元测试。 |
| `tests/core_model_tests.cpp` | 验证模型属性、非法空值、自动分配、入离场和记录行为。 |
| `apps/admin/CMakeLists.txt` | 构建可选 Qt 管理员端，Qt 自动处理只作用于该目标。 |
| `apps/admin/main.cpp` | 独立 GUI 入口，不参与终端版本运行。 |
| `apps/admin/MainWindow.h/.cpp` | 实现布局编辑、车位图、自动分配和路线显示。 |

运行流程：系统启动 `smartpark_cli` → 解析布局 → 构建障碍栅格 → 对空闲车位计算路线和评分 → 选择最优车位并占用 → 输出路线和状态 → 检查结果并返回退出码。

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
