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

当前已完成：

- C++17、CMake 与 Qt 6 Widgets 的基础工程配置。
- 可以启动的管理员端 `MainWindow`。
- `Vehicle` 与 `ParkingSpot` 基础模型及其单元测试。

SmartPark 0.1 下一步将实现 `ParkingRecord`、`ParkingService`，并在内存中初始化和管理 60 个停车位。

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

1. 工程跑起来：完成 CMake、Qt 6、C++17 和主窗口。
2. 纯 C++ 停车核心：实现停车位、车辆、停车记录和入场/离场流程。
3. 停车场 GUI：实时展示停车位状态。
4. SQLite 持久化：重启后保留停车数据。
5. 收费系统：根据停车时长计算费用。
6. 服务端：以 TCP 建立管理员端与服务端架构。
7. 出入口终端：手动输入车牌并通过服务端处理业务。
8. 车牌识别：接入 OpenCV 与 HyperLPR3。

## 终端验证（无需 Qt、Conda 或图形桌面）

在项目根目录执行，需 CMake 3.21+、Ninja 和支持 C++17 的编译器：

```bash
cmake -S . -B build-cli -G Ninja \
  -DSMARTPARK_BUILD_ADMIN=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cli --parallel
./build-cli/apps/cli/smartpark_cli
ctest --test-dir build-cli --output-on-failure
```

当前服务器也可以显式使用 `/usr/bin/cmake`、`/usr/bin/ctest`，并在配置时添加 `-DCMAKE_CXX_COMPILER=/usr/bin/g++`，避免当前激活环境影响工具选择。`build-cli` 与 Qt 版本的 `build` 缓存相互独立。

程序自动执行验证，无需输入。它创建 A001、A002 两个车位和两辆车，依次占用车位、拒绝重复占用、释放车位、拒绝重复释放，最后恢复全部空闲。每个阶段打印状态和剩余车位数；成功输出 `RESULT: PASS` 并返回 0，检查失败输出 `RESULT: FAIL` 并返回 1。

这只是当前模型的演示，不是 60 车位管理服务，也不验证 GUI、数据库或网络。状态和车辆只保存在内存中，程序结束后丢弃。

### 文件职责与运行流程

| 文件 | 用途 |
| --- | --- |
| `CMakeLists.txt` | 设置 C++17、CTest 和各构建目标；关闭 `SMARTPARK_BUILD_ADMIN` 后不查找 Qt。 |
| `apps/cli/CMakeLists.txt` | 构建 `smartpark_cli`，链接核心库并注册终端演示测试。 |
| `apps/cli/main.cpp` | 终端入口，组织演示、打印状态、检查结果并返回退出码。 |
| `src/core/CMakeLists.txt` | 将模型实现编译为 `smartpark_core` 静态库。 |
| `src/core/model/Vehicle.h` | 声明车辆类型、车辆数据与只读访问接口。 |
| `src/core/model/Vehicle.cpp` | 实现车辆构造、非空车牌校验和数据访问。 |
| `src/core/model/ParkingSpot.h` | 声明车位状态、当前车辆及占用/释放接口。 |
| `src/core/model/ParkingSpot.cpp` | 实现单个车位的状态转换，拒绝重复占用或释放。 |
| `tests/CMakeLists.txt` | 构建并注册模型单元测试。 |
| `tests/core_model_tests.cpp` | 验证模型属性、非法空值和占用/释放行为。 |
| `apps/admin/CMakeLists.txt` | 构建可选 Qt 管理员端，Qt 自动处理只作用于该目标。 |
| `apps/admin/main.cpp` | 独立 GUI 入口，不参与终端版本运行。 |
| `apps/admin/MainWindow.h`、`MainWindow.cpp` | 声明与实现 Qt 空主窗口，不参与终端版本运行。 |

运行流程：系统启动 `smartpark_cli` → `main()` 创建模型 → 调用 `occupy()` / `release()` → 读取状态并打印 → 检查结果 → 返回退出码。CMake 只负责构建，CTest 负责执行测试，二者不是业务运行步骤。

## Linux 构建与运行

当前 Linux 开发环境使用独立的 Conda 环境 `smartpark-qt68`，其中包含 Qt 6.8.4、CMake、Ninja 和 C++ 编译器。

```bash
source ~/miniforge3/etc/profile.d/conda.sh
conda activate smartpark-qt68

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$CONDA_PREFIX"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/apps/admin/smartpark_admin
```

如果当前终端没有图形显示，可以使用 Qt 的 offscreen 平台插件做启动检查：

```bash
QT_QPA_PLATFORM=offscreen ./build/apps/admin/smartpark_admin
```

该 Conda 环境用于构建 Linux x86_64 版本。Windows 和 Android 版本后续需要分别使用对应平台的 Qt Kit。
