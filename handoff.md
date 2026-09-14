# SmartPark 接手说明（2026-09-14）

给下一个 Codex / 终端会话用。先读本文件，再动代码。

## 一句话进度

Admin GUI 默认已换成 6 层车库建筑图：58.0m × 42.4m、**75 车位**、北墙双入口单出口。6-1 电气室 / 设备用房 / 蓄电池室和 6-7/6-8 水箱间已改成停车位（充电 / VIP / 无障碍），只保留东西两座楼梯间。CLI 默认仍是 60 车位，不要改。

本轮改动已写入本文件，并上传到 s1 仓库 `~/Cpp-CourseProject_smart_park`。

## 仓库与机器

- 本地 Mac：`/Users/Zhuanz/Documents/ChatGPT/c++课设/smartpark`
- s1：`~/Cpp-CourseProject_smart_park`（本机 `origin` 指向这里）
- GitHub 远程名是 `github`，不是 `origin`
- 本地 Mac GUI：`QT_PREFIX=/Users/Zhuanz/Qt/6.8.3/macos`
- **运行优先 s1**（尤其 CLI / ctest）。Mac 只用来看 Admin GUI
- 不要动：`model/`、`smartpark-my-layout.db`、根目录无关大文件
- `.gitignore` 已忽略根目录 `model/`

## 用户这轮要的东西（都已做）

1. 每个车位上能看见现在停的车（编号 + 车牌，占用表同步）
2. 全部停车记录，可按时间查询
3. 车库路线图更大；「自定义停车场布局」是按钮，点开对话框编辑
4. 把停车位做成 6 层建筑图那样的规划
5. 把 6-1 电气室 / 设备用房 / 蓄电池室、6-7/6-8 水箱间改成停车位，并增设充电、VIP、无障碍

## 图纸布局（GUI 默认）

三处文本必须保持同步：

- `ParkingLayout::garageDescription()`（`src/core/model/ParkingLayout.cpp`）
- `data/garage-6f.txt`
- `my-layout.txt`

| 项目 | 值 |
| --- | --- |
| 场地 | 58.0 × 42.4 m |
| 原点 | 左上角。x=0 为 6-1，y=0 为 6-E（北在上，和 Qt / 图纸一致） |
| X 轴 | 6-1→6-8：0, 9, 18, 27, 36, 45, 54, 58 |
| Y 轴 | 6-E→6-A：0, 12.2, 24.4, 33.4, 42.4 |
| 入口 | `(22.5, 0)` `(31.5, 0)` |
| 出口 | `(49.5, 0)` |
| 车位 | 75 个：8 无障碍 / 15 充电 / 10 VIP / 42 普通 |
| 障碍 | 2 个：西楼梯间、东楼梯间 |

分区：

- `A` / `A2`：北侧 2 无障碍 + 6 普通
- `B`：上岛 8 充电
- `C` / `D`：下岛背靠背 8 + 8 普通
- `E`：南侧 12 普通（MQ2940）
- `F`：西北 4 普通
- `G`：东北 4 VIP
- `H`：原设备用房 + 西侧中部，两列 6 VIP（`0.50 24.00 3 2 ... right vip`）
- `I`：西南 4 普通
- `J` / `J2`：原电气室，4 充电 + 2 无障碍
- `L`：原蓄电池室，4 无障碍
- `M`：原水箱间，3 充电（南侧通道与 D/E 对齐）

CLI **不要**改成这个布局。`ParkingLayout::defaultLayout()` 仍是 100×60、A001–A060。验证图纸：

```bash
./build/qt/apps/cli/smartpark_cli data/garage-6f.txt --reset --db /tmp/smartpark-garage.db
# s1 上无 Qt 时：
./build/cli/apps/cli/smartpark_cli data/garage-6f.txt --reset --db /tmp/smartpark-garage.db
```

## 引擎 / GUI 改动

- `AisleSide` 增加 `Up` / `Down`，也接受 `top` / `bottom`
- 布局命令 `obstacle x y 宽 高 [名称]`
- `GridPlanner` 把障碍当 blocked
- 分区贴边检查有 `1e-9` 容差
- 布局签名把障碍写进 SQLite，旧 DB 对不上会提示重置
- Admin GUI 默认 `garageLayout()`：轴线、外墙开口、楼梯斜线、类型着色、编号+车牌
- 下半 Tab：停车记录（时间过滤）、当前占用表、预约
- 顶栏「自定义停车场布局」弹窗

## 本轮验证（Mac，需出沙箱，否则 Qt 报 neon）

- `./build/qt/tests/smartpark_core_tests`：All core model tests passed
- `ctest --test-dir build/qt`：3/3
- CLI 图纸演示：`Remaining spots: 51/75`，`RESULT: PASS`
- `smartpark_admin` 已重新链接；GUI 还没当面点开看

## 在 s1 上怎么跑

```bash
ssh s1
cd ~/Cpp-CourseProject_smart_park
git status -sb
git log -1 --oneline
cmake --preset cli && cmake --build --preset cli --parallel
ctest --preset cli-tests --output-on-failure
./build/cli/apps/cli/smartpark_cli data/garage-6f.txt --reset --db /tmp/smartpark-garage.db
```

期望：车位数 75，`RESULT: PASS`。s1 上原来有未提交的 `apps/admin/main.cpp` 空白/文案改动，以及未跟踪的 `smartpark-my-layout.db`；不要把那个 db 提交进去。

## 在 Mac 上看 GUI

```bash
cd "/Users/Zhuanz/Documents/ChatGPT/c++课设/smartpark"
QT_PREFIX=/Users/Zhuanz/Qt/6.8.3/macos scripts/build-admin.sh
QT_PREFIX=/Users/Zhuanz/Qt/6.8.3/macos scripts/run-admin.sh --db /tmp/smartpark-gui-garage.db
```

`scripts/build-admin.sh` 若因 `uic` neon 失败，直接：

```bash
cmake --build build/qt --parallel --target smartpark_admin
open build/qt/apps/admin/smartpark_admin.app
# 或
build/qt/apps/admin/smartpark_admin.app/Contents/MacOS/smartpark_admin --db /tmp/smartpark-gui-garage.db
```

旧 59 车位库会提示重置，用上面的新 db 路径即可。

## 改布局时注意

1. 三处图纸文本一起改，然后跑 `testGarageFloorplanLayout`
2. 不要改 CLI 默认 60 车位
3. 通道最小 2.5m；H 西列靠楼梯，南侧约 1.2m 缝接到 L。若 `ensureReachable` 失败，先加宽这条缝或把 H/L 通道对齐
4. 不要为了「更像图纸」去改分配算法主路径

## 不要做的事

- 不要改 CLI 默认 60 车位
- 不要把 `model/` 或 `smartpark-my-layout.db` 提交进去
- 用户没明确说就不要再 commit / push
- 用户说以后运行先在 s1；GUI 外观验收可以在 Mac

## 下一轮建议

1. **s1 上跑 CLI + ctest**，确认 75 车位图纸能分配和出路线
2. **Mac 打开 Admin GUI**，确认原电气室/设备用房/蓄电池室/水箱间已是车位，两座楼梯还在
3. 产品路线仍是 README 里的 SmartPark 0.7：远程预约（TCP Server / 用户端），然后 Gate，然后 LPR。不要跳去车牌识别，除非用户改目标

## 路线回顾

```
① 项目骨架                         完成
② Qt 工程 C++17 + Qt6 + CMake      完成
③ Vehicle + ParkingSpot            完成
④ 内存停车业务                      完成
⑤ 停车场 GUI                        完成，已改成建筑图
⑥ SQLite 持久化                     完成
⑦ 收费 BillingService               完成，CLI/GUI 已展示
⑧ 拆分客户端/服务端 TCP             未开始（0.7 下一件大事）
⑨ 出入口终端 Gate                   未开始
⑩ 车牌识别 OpenCV + 方案 B          资料和训练脚本有了，产品未接入
⑪ 用户端 / 可视化统计               未开始
```

预约第一版（`Booking`：定金、到场窗口、爽约）已在 CLI/GUI 可操作。完整时段预约 `Reservation` 还只在 README 设计里。

## 给下一个 agent 的第一句

「继续 SmartPark。先读 `smartpark/handoff.md`。图纸已是 75 车位，机房改成充电/VIP/无障碍，只留两座楼梯。改动已在 s1。先 `ssh s1` 跑 CLI，再在 Mac 开 Admin GUI。不要改 CLI 默认 60 车位，不要动 model/。」
