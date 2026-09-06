# SmartPark 调研来源与路线依据

记录 2026-09-07 对智能停车、路径规划和车牌识别相关仓库与论文的评估。评分范围为 1-10。本文件只作为课设路线依据，不引入所列项目的代码。

## 评分摘要

| 参考对象 | 评分 | 对本项目的取舍 |
| --- | ---: | --- |
| [asthasingh0660/SmartPark](https://github.com/asthasingh0660/SmartPark) | 9.0 | 对标独立分配器、A*/Dijkstra、拥堵感知、预约 TTL 和动态重规划。 |
| [Path Planning Algorithms for Smart Parking: Review and Prospects](https://www.mdpi.com/2032-6653/15/7/322) | 8.5 | 保留栅格 A*，引入动态边权而不是换成 Hybrid A*。 |
| [GPR-A* shared parking optimization](https://www.sciencedirect.com/science/article/abs/pii/S0378437123002042) | 8.0 | 将时变拥堵写入 A* 边权，而不是只在最终评分里惩罚。 |
| [soheilraeiss/Parking-Management-System](https://github.com/soheilraeiss) | 8.5 | 后续 Qt 6 + SQLite + 计费拆分可参考，当前阶段不接入数据库。 |
| [foryouos/Parking_System](https://github.com/foryouos/Parking_System) | 8.0 | 国内课设流程可参考，但架构耦合且 EasyPR 过时，不照抄。 |
| [Dolgov Hybrid A*](https://ai.stanford.edu/~ddolgov/papers/dolgov_gpp_stair08.pdf) | 9.5 / 3.0 | 算法本身很强，但不适合“司机引导到车位”的课设场景，明确不做。 |
| [HyperLPR](https://github.com/szad670401/HyperLPR) | 9.0 | 后续车牌识别唯一候选后端，放在 Gate 模块，不进入 core。 |
| [CCPD](https://github.com/detectrecog/ccpd) | 9.0 | 后续中文车牌识别验证数据集。 |

## 对 SmartPark 0.4 的直接影响

- 从 `ParkingService` 中拆出 `SpotAllocator`，业务状态机与选位算法分离。
- 默认策略改为可配置加权代价，并保留 `Nearest` 作为对照。
- 拥堵进入 A* 边权（默认 `k=0.35`）：`edgeCost = baseCost * (1 + k * localOccupancy)`。拥堵分两层生效：路径搜索用边权绕开拥堵通道，车位评分再用 `nearbyOccupiedSpots` 对局部占用密集的车位加罚。
- 增加 `Free -> Reserved(ttl) -> Occupied -> Free`，避免后续 TCP 双端重复分配。
- 布局 DSL 支持车位类型和多个出入口。
- 使用入口/出口一次搜索到全部候选车位，避免每辆车最多 120 次独立 A*。

## 明确不做

EasyPR、Hybrid A*、Apollo 自动泊车、Go/Kratos 微服务、微信小程序、Nash 定价、MARL、MILP，以及只按欧氏距离选择最近空位的简化方案。
