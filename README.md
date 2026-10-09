# SmartPark

SmartPark 是 C++17 智能停车场管理系统课程项目。当前主分支维护 macOS 原生管理端、服务器、网页用户端和出入口／用户终端，共用停车业务、SQLite 持久化及 TCP／REST 网络层。

## 当前应用

| 应用 | 用途 |
| --- | --- |
| [macOS 管理端](apps/macos/CMakeLists.txt) | AppKit 原生界面；本地模式或连接 s1，管理车位、时段预约、车辆作业和停车记录 |
| [服务器](apps/server/main.cpp) | TCP、REST、WebSocket、网页静态文件与服务端车牌识别 |
| [网页用户端](apps/webclient/index.html) | 扫码注册／登录、查询余位、预约、取消与停车引导 |
| [道闸终端](apps/gate/main.cpp) | 手输车牌模拟出入口、道闸状态机与离线事件补报 |
| [命令行用户端](apps/user/main.cpp) | 查询余位、提交／取消时段预约 |

共享实现位于 [core](src/core/CMakeLists.txt) 和 [network](src/network/CMakeLists.txt)。数据库实现是 [core/persistence](src/core/persistence/DatabaseManager.cpp)，保留供管理端与服务器使用。

业务包括分区均衡车位分配、网格路径规划、停车计费、时段预约冲突检查、定金模拟支付、到场核销及离场抵扣；预约和停车记录持久化到 SQLite。TCP 与 REST 共用服务及事件推送。真实摄像头接入与真实支付尚未实现；TCP v1 用于内网或隧道。

## macOS 构建与启动

依赖 CMake ≥3.21、C++17、Ninja、Qt ≥6.2（Core、Sql、Network、HttpServer、WebSockets）。界面使用 AppKit，不需要 Qt Widgets。

```bash
# 可用 QT_PREFIX 指定 Qt 安装目录；脚本也会尝试自动查找。
QT_PREFIX="$HOME/Qt/6.8.3/macos" scripts/build-macos.sh
scripts/run-macos.sh

# 预填远程登录地址与账号：
scripts/run-macos.sh --remote 10.108.17.55:9527 --user admin
```

构建与启动入口分别是 [build-macos.sh](scripts/build-macos.sh) 和 [run-macos.sh](scripts/run-macos.sh)。默认输出到 `build/macos`；可通过 `BUILD_DIR` 覆盖。直接使用 CMake：

```bash
cmake --preset macos-debug -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/macos"
cmake --build --preset macos-debug --parallel 4
ctest --preset macos-tests
```

## Linux／s1 服务端

```bash
cmake --preset server-release \
    -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/gcc_64"
cmake --build --preset server-release --parallel 4

build/server-release/apps/server/smartpark_server \
    --port 9527 --http-port 18080 --ws-port 18081 \
    --db "$HOME/smartpark-data/smartpark.db" \
    --layout data/garage-6f.txt --web-root apps/webclient \
    --advertise 10.108.17.55
```

s1 当前地址为 `10.108.17.55`，TCP `9527`、HTTP `18080`、WebSocket `18081`。服务器的现有启动脚本与数据库位于 `~/smartpark-data`；已启用真实模型识别。完整配置见 [服务器部署](docs/deploy-server.md)，接口见 [TCP 协议](docs/tcp-protocol.md) 与 [REST API](docs/rest-api.md)。

```bash
build/server-release/apps/gate/smartpark_gate \
    --host 10.108.17.55 --port 9527 --user gate --pass smartpark --mode entrance
build/server-release/apps/user/smartpark_user \
    --host 10.108.17.55 --port 9527 --user user --pass smartpark
```

演示账号仅在新数据库初始化时创建，`admin`、`gate`、`user` 默认口令为 `smartpark`。已有数据库不自动重置。

## 车牌识别

macOS 的“车辆作业 → 识别图片”会先展示候选车牌，人工采用后填入输入框，入场／出场另行操作。远程模式上传图片至服务端，不要求客户端安装模型环境；本地模式通过两套 Python 环境运行 [识别脚本](scripts/recognize_plate.py)。

识别流程为 YOLO pose 四角检测 → 透视矫正裁剪 → PP-OCRv5 字符识别。使用本地权重时设置 `YOLO_OFFLINE=true`，避免离线网络中的 DNS 探测等待。未配置服务端 `--lpr-command` 时返回 `backend=mock`；真实识别返回 `backend=script`。

模型和环境配置、评测限制、训练复现见 [车牌识别说明](docs/lpr.md)，样例见 [样例说明](examples/plates/README.md)。s1 在 2026-10-09 的单张 TCP 验证为“皖AMJ570”、约 13.66 秒；该结果不代表全量准确率。

## 测试

```bash
# 无 AppKit 的服务器／终端构建与测试，也可在 macOS 上执行。
cmake --preset server-debug -DCMAKE_PREFIX_PATH="/path/to/Qt"
cmake --build --preset server-debug --parallel 4
ctest --preset server-tests

# 对现有 s1 只读验证 macOS 远程连接；默认不提交停车业务写入。
build/macos/tests/smartpark_macos_remote_tests \
    --remote 10.108.17.55 9527 admin smartpark
```

CTest 包含核心业务、服务端自测、Gate 自测与断线补报、车牌脚本／几何／数据集测试、REST 端到端测试；macOS 构建另含地图与原生界面远程测试。端到端测试使用独立临时数据库。

## 归档与目录约定

旧 Qt 管理端、演示 CLI、专属测试和 Windows 启动脚本已保存在 `archive/legacy-apps-20261009` 分支，完整旧版文档也在该分支。需要查看时，可在另一个工作目录打开归档：

```bash
git worktree add ../smartpark-legacy archive/legacy-apps-20261009
```

主分支不再保留只有占位文件的 SQL、资源、数据库和 LPR 目录，也移除了已有源码目录中的多余占位文件。本地构建产物、训练框架／输出和会话记录由 [.gitignore](.gitignore) 排除；二维码 C++ 依赖仍随仓库提供。当前维护要点见 [交接说明](handoff.md)。
