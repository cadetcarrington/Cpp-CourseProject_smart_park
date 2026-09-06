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

## 当前范围：SmartPark 0.1

当前版本只搭建工程结构，后续第一个可运行版本将实现：

- C++17、CMake 与 Qt 6 Widgets 的基础工程配置。
- 可以启动的管理员端 `MainWindow`。
- `Vehicle` 与 `ParkingSpot` 基础模型。
- 一个能够在内存中管理 60 个停车位的停车核心。

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
│   ├── admin/       # 管理员端 Qt GUI
│   ├── server/      # SmartPark 服务端入口
│   └── gate/        # 出入口终端入口
├── src/
│   ├── core/
│   │   ├── model/   # Vehicle、ParkingSpot、ParkingRecord 等领域模型
│   │   └── service/ # ParkingService、BillingService 等业务服务
│   ├── database/    # 数据库连接与仓储层
│   ├── network/     # TCP 协议与通信实现
│   └── lpr/         # 车牌识别集成
├── resources/
│   ├── icons/       # 图标资源
│   ├── styles/      # Qt 样式表
│   └── images/      # 图片资源
├── data/            # 本地运行时 SQLite 数据库，不提交数据库文件
├── sql/             # 数据库建表与初始化脚本
└── tests/           # 单元测试与集成测试
```

## 开发里程碑

1. 工程跑起来：完成 CMake、Qt 6、C++20 和主窗口。
2. 纯 C++ 停车核心：实现停车位、车辆、停车记录和入场/离场流程。
3. 停车场 GUI：实时展示停车位状态。
4. SQLite 持久化：重启后保留停车数据。
5. 收费系统：根据停车时长计算费用。
6. 服务端：以 TCP 建立管理员端与服务端架构。
7. 出入口终端：手动输入车牌并通过服务端处理业务。
8. 车牌识别：接入 OpenCV 与 HyperLPR3。

## Linux 构建与运行

当前 Linux 开发环境使用独立的 Conda 环境 `smartpark-qt68`，其中包含 Qt 6.8.4、CMake、Ninja 和 C++ 编译器。

```bash
source ~/miniforge3/etc/profile.d/conda.sh
conda activate smartpark-qt68

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$CONDA_PREFIX"
cmake --build build --parallel
./build/apps/admin/smartpark_admin
```

如果当前终端没有图形显示，可以使用 Qt 的 offscreen 平台插件做启动检查：

```bash
QT_QPA_PLATFORM=offscreen ./build/apps/admin/smartpark_admin
```

该 Conda 环境用于构建 Linux x86_64 版本。Windows 和 Android 版本后续需要分别使用对应平台的 Qt Kit。
