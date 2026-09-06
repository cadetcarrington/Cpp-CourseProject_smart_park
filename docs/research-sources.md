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

## 2026-09-07 补充调研：计费、服务端与车牌识别

本节聚焦 BillingService、TCP 服务端、出入口 Gate 终端和车牌识别链路。评分仍为 1-10，四项分别表示思路、落地、算法、文档参考价值。来源用于提炼接口、计费规则与架构边界，不直接复制实现代码。

| 参考来源 | 评分（思路 / 落地 / 算法 / 文档） | 对本项目的取舍 |
| --- | --- | --- |
| [DB4403/T 313—2023 智慧停车业务数据与接口规范](https://amr.sz.gov.cn/attachment/1/1566/1566543/9772236.pdf) | 9 / 9 / 5 / 9 | 借鉴 TCP 登录、心跳、事件清单；不抄城市平台全量 API。 |
| [PARCS 五层架构与费率引擎](https://parkingpaymentguide.com/payment-systems/how-parking-payment-systems-work/) | 9 / 9 / 6 / 9 | 参考费率引擎、离线队列和宽限期设计。 |
| [MDPI SPMS 参考架构](https://www.mdpi.com/2079-8954/13/2/70) | 8 / 8 / 6 / 9 | 参考分层 Server/Gate 组织方式。 |
| [掘金智能停车计费系统](https://juejin.cn/post/7586550947703668746) | 9 / 8 / 7 / 8 | 参考策略模式与规则表组合。 |
| [佛山禅城 2026 停车收费新规](https://www.163.com/dy/article/L5OVP5IU05129QAF.html) | 7 / 9 / 6 / 8 | 参考 15 分钟计费、30 分钟免费和日封顶规则。 |
| [HyperLPR3](https://github.com/szad670401/HyperLPR) | 9 / 8 / 8 / 8 | 使用 OpenCV4 + MNN2 路线，只放在 Gate。 |
| [CCPD](https://github.com/detectRecog/CCPD) 与 [ECCV 论文](https://openaccess.thecvf.com/content_ECCV_2018/html/Zhenbo_Xu_Towards_End-to-End_License_ECCV_2018_paper.html) | 8 / 8 / 9 / 9 | 作为中文车牌识别评测集和方法依据。 |
| [soheilraeiss Qt PMS](https://github.com/soheilraeiss/Parking-Management-System) | 7 / 8 / 5 / 7 | 参考 Qt6 + SQLite + QThread 组织方式。 |
| [北京 DB11/T 3001 ETC 停车场接口](https://jtw.beijing.gov.cn/xxgk/flfg/jthy/201912/P020191231388015746585.pdf) | 8 / 8 / 5 / 8 | 参考 Gate 作为 TCP 客户端、请求应答和重传机制。 |
| [CSDN 高拓展性停车计费能力设计](https://blog.csdn.net/ATFWUS/article/details/143139640) | 8 / 8 / 7 / 8 | 参考 JSON 规则、免费/封顶装饰器设计。 |
| [foryouos Parking_System](https://github.com/foryouos/Parking_System) | 6 / 6 / 4 / 7 | 作为反例：GUI、数据库和 LPR 耦合，不沿用其结构。 |
| [深圳公共智慧停车平台数据接入规范](https://jtys.sz.gov.cn/attachment/1/1596/1596572/12076334.pdf) | 7 / 8 / 5 / 8 | 参考心跳、NTP 和断线补报要求。 |

### 下一阶段优先级

1. **P0 — BillingService**：先完成可测试的计费核心，包括费率规则、免费时长、日封顶和宽限期。
2. **P0.5 — TCP 协议与服务端**：沉淀 `docs/tcp-protocol.md`，并用 `QTcpServer` 实现登录、心跳和事件流转。
3. **P1 — Gate Terminal + Fake LPR + 离线队列**：先以假识别打通出入口事件，补齐断线缓存和补报路径。
4. **P2 — OpenCV + HyperLPR3 + CCPD 评测**：在协议稳定后接入真实识别，并用 CCPD 做可复现评测。
5. **P3 — 支付、图表、用户端**：在计费、服务端和 Gate 链路稳定后再扩展外围能力。

### 明确不做

EasyPR、纯云端计费、微信小程序、Go 微服务，以及把 LPR 放进 `ParkingService`。
