# SmartPark 接手说明（2026-10-09）

当前主分支维护 macOS 原生管理端、server、webclient、gate 和 user。旧 Qt admin、演示 CLI、专属测试／启动脚本、未使用的通知组件和空占位目录已保存在 `archive/legacy-apps-20261009` 分支。完整旧版 README 和交接记录也在归档分支。

## 构建与归档

```bash
git status --short
git branch -vv
git worktree list
QT_PREFIX="$HOME/Qt/6.8.3/macos" scripts/build-macos.sh
ctest --test-dir build/macos --output-on-failure
scripts/run-macos.sh --remote 10.108.17.55:9527 --user admin

cmake --preset server-debug -DCMAKE_PREFIX_PATH="/path/to/Qt"
cmake --build --preset server-debug --parallel 4
ctest --preset server-tests

# 在独立目录查看旧版代码。
git worktree add ../smartpark-legacy archive/legacy-apps-20261009
```

Git 的 `origin` 是 s1 训练仓库，`github` 是 GitHub。同步与发布时先检查分支差异，本地 main 更新不等于远端已推送。s1 部署构建位于 `~/sp-headless`，与训练仓库和本地工作目录独立。

会话记录仍在本地磁盘上，但不再由 Git 跟踪。构建产物、训练输出和第三方 PaddleOCR 框架由 [.gitignore](.gitignore) 排除；二维码 C++ 依赖是构建必需内容，仍随仓库提供。

## s1 当前部署

| 项目 | 配置 |
| --- | --- |
| 地址 | `10.108.17.55`，集群内网 |
| 端口 | TCP `9527` / HTTP `18080` / WebSocket `18081` |
| 数据库 | `~/smartpark-data/smartpark.db` |
| 启动与日志 | `~/smartpark-data/start.sh` / `~/smartpark-data/server.log` |
| 构建目录 | `~/sp-headless/build-server` |
| Qt | `~/Qt/6.8.3/gcc_64` |
| 布局 | 部署使用六层 75 车位布局 |
| 演示账号 | 新库初始化创建 `admin` / `gate` / `user`，口令 `smartpark` |

2026-10-09 已启用真实 LPR。识别命令使用 `~/Cpp-CourseProject_smart_park/scripts/recognize_plate.py`、该训练目录的实体模型、`~/miniforge3/envs/smartpark-lpr/bin/python` 与 `~/miniforge3/envs/smartpark-ocr/bin/python`，不使用部署目录中的 LFS 指针。

必须设置 `YOLO_OFFLINE=true`，避免导入时约 58 秒的外网 DNS 等待；`OMP_NUM_THREADS`、`OPENBLAS_NUM_THREADS`、`MKL_NUM_THREADS` 当前均为 4。单张样例经 TCP 返回“皖AMJ570”、`backend=script`、13.66 秒，仅代表该样例链路验证。旧启动配置备份为 `~/smartpark-data/start.sh.before-lpr-20261009-1626`。

运行与训练信息见 [LPR 文档](docs/lpr.md)，完整服务器配置见 [部署文档](docs/deploy-server.md)。本次目录整理不自动更新或重启 s1 正在运行的服务。

## 数据与维护约定

- 数据库实现位于 [core/persistence](src/core/persistence/DatabaseManager.cpp)，是当前业务依赖；移出的是顶层空目录。
- 当前管理端与网页写入 `Reservation` 时段预约，包含冲突检查、到场核销、取消／爽约与定金抵扣。旧 `Booking` 仍供历史数据和核心兼容使用，保留两代模型及测试。
- macOS 表单使用 `createReservation / checkInReservation / cancelReservation`。选择当前订单时按车牌、车位、有效状态和最新开始时间筛选，不可按车牌取第一笔历史记录。
- TCP 登录使用 `user` / `pass`，REST 使用 `username` / `password`；TCP 数据位于 `payload`。普通用户查询预约必须指定自己的车牌。
- TCP 明文仅用于内网／隧道。不要同时用本地端和服务端打开同一 SQLite 数据库，内存状态无法互相同步。
- 服务器拒绝与持久化数据不兼容的布局，不会自动清库。布局变化应明确选择数据迁移或独立数据库。
- 对真实 s1 的 macOS 远程测试默认只读；`SMARTPARK_REMOTE_WRITE_TEST=1` 才执行业务写入，必须保留自清理流程。
- `open --args` 对已运行应用不会传参；直接启动脚本可传入远程参数。macOS 自定义参数必须在构造 Qt 应用前摘出。
- Documents／iCloud 中的源码可能变成 dataless 占位；构建报 `Operation timed out` 时先恢复可读性，避免误改业务代码。
- H5 是静态文件，无需前端构建；当前原生界面不依赖已归档的 Qt Widgets 与旧通知层。

本轮测试数量以实际 CTest 输出为准；旧版 13 项包含归档应用，不能再作为主分支基线。
