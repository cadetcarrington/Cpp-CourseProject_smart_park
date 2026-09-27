# SmartPark TCP 协议 v1

本文档定义 SmartPark 服务端（`apps/server/smartpark_server`）与各终端
（管理员端 / Gate 出入口 / 用户端）之间的 TCP 通信协议。设计参考
DB4403/T 313 智慧停车业务数据与接口规范、北京 DB11/T 3001 ETC 停车场
接口（请求-应答 + 重传）与深圳公共智慧停车平台数据接入规范（心跳、
断线补报）。

## 1. 传输层

- 传输：TCP 长连接，一个终端一条连接；默认端口 `9527`。
- 分帧：**4 字节大端长度前缀 + UTF-8 JSON 载荷**。长度只计 JSON 字节，
  不含前缀自身；单帧上限 1 MiB，超限立即断开。
- 编码：JSON（UTF-8，无 BOM）。所有时间字段为 epoch 毫秒（int64），
  所有金额单位为元（double）。
- 并发模型：服务端单线程事件驱动（Qt 事件循环），所有会话共享一个
  `ParkingService` 实例，不引入多线程锁库。

## 2. 消息封包（envelope）

请求（终端 → 服务端）：

```json
{"v":1, "type":"request", "id":"c1", "action":"parking.enter",
 "token":"<登录后下发>", "payload":{...}}
```

应答（服务端 → 终端，`id` 与请求一致）：

```json
{"v":1, "type":"response", "id":"c1", "ok":true, "payload":{...}}
{"v":1, "type":"response", "id":"c1", "ok":false, "error":"车辆已在场内"}
```

事件（服务端 → 所有已登录连接，广播）：

```json
{"v":1, "type":"event", "event":"parking.entered", "payload":{...}}
```

## 3. 会话与心跳

- `login` 是唯一免 token 的 action；成功后服务端为该连接生成会话
  （记录用户与角色），后续请求必须携带登录响应中的 `token`。
- 终端应每 20 秒发送一次 `heartbeat`；服务端每 15 秒扫描一次，
  **超过 60 秒无任何帧**的连接被判定掉线并关闭。
- 登录失败连续 5 次断开连接；登录成功/失败写入审计哈希链。
- 断线补报由 Gate 把 JSONL 队列分批（每次最多 500 条）提交至 `gate.replay`；
  只在全部事件获确认后移除已确认前缀，超时重试依原车牌与事件时间戳去重。

## 4. 动作清单（v1）

| action | payload | 应答 payload | 说明 |
| --- | --- | --- | --- |
| `login` | `{user, pass}` | `{token, user}` | 会话建立；写审计 |
| `heartbeat` | `{}` | `{ts}` | 保活 |
| `parking.status` | `{}` | `{capacity, occupied, available, reserved, zones:[{zone,total,load}]}` | 全场概览 |
| `spot.list` | `{}` | `{spots:[{spotId, zone, type, status, plate}]}` | 全量车位 |
| `parking.enter` | `{plate, vehicleType}` | 同 CLI 分配结果（spotId、entryRoute、exitRoute、score） | 入场；广播 `parking.entered` |
| `parking.leave` | `{plate}` | `{plate, spotId, durationMin, fee}` | 离场计费；广播 `parking.exited` |
| `reservation.create` | `{plate, vehicleType, startMs, durationMin, accessible}` | `{reservationId, spotId, deposit, accessible, startMs, entranceIndex, exitIndex, entryDistance, exitDistance, entryTurns, exitTurns, entryPoints:[{x,y}]}` | 时段预约；广播 `reservation.created` |
| `reservation.cancel` | `{plate}` | `{}` | 取消退定金；广播 `reservation.cancelled` |
| `reservation.checkin` | `{plate}` | `{spotId}` | 到场核销；广播 `reservation.checkin`；Gate 普通入场直接用 `parking.enter`，自动核销匹配预约 |
| `gate.replay` | `{events:[{kind, plate, vehicleType?, ts}]}` | `{applied, duplicate, skipped, results:[{plate, kind, ok, duplicate?, error?, spotId?, fee?}]}` | Gate 账号补报；`kind=enter|exit`，`ts` 为事件发生时 epoch 毫秒 |
| `analytics.report` | `{}` | `{model, summary, findings, recommendations}` | 本地分析结论 |

`vehicleType` 取值：`car | motorcycle | truck | electric`。补报按数组顺序处理，
仅接受过去 30 天至未来 5 分钟内的时间戳，每批 1–500 条。`ok=true` 表示
这一批请求被处理，不代表每条都成功；`skipped>0` 时 Gate 保留本地队列。
重复事件以停车记录中的车牌、事件类型和毫秒时间戳匹配，成功去重计入
`duplicate`。跨设备重复识别但时间戳不同、或历史记录被清理后的补报不在
这一简化去重保证内；演示部署须保护本地队列文件。

## 5. 事件清单

| event | 触发 | payload |
| --- | --- | --- |
| `parking.entered` | 入场成功 | `{plate, spotId, entryTime}` |
| `parking.exited` | 离场结算成功 | `{plate, spotId, fee, durationMin}` |
| `reservation.created` | 时段预约创建 | `{plate, spotId, startMs, endMs, accessible}` |
| `reservation.cancelled` | 取消 | `{plate}` |
| `reservation.checkin` | 到场核销 | `{plate, spotId}` |
| `reservation.noshow` | 爽约判定 | `{plate, spotId}`（预留，当前版本不广播） |
| `gate.replayed` | 一批补报处理结束 | `{applied, duplicate, skipped}` |

## 6. 错误码约定

`error` 为中文人类可读描述；机器判断看 `ok`。v1 不引入数字错误码，
保留字段 `code`（后续版本补充，如 `AUTH_REQUIRED` / `NOT_FOUND` / `CONFLICT`）。

## 7. 安全边界（v1 现状与后续）

- v1 使用明文 TCP + 连接级会话；部署在内网或经 SSH 隧道。
- 后续：TLS（QSslServer）、token 过期、按角色权限（对接审计与 RBAC 路线）。
- 所有登录与敏感动作写入 `AuditLogService` 哈希链。

## 8. 自测

`smartpark_server --selftest` 在进程内起服务（临时端口 + 临时库），
用内置 `TcpClient` 跑通“登录 → 状态 → 入场 → 建预约 → 到场 → 离场 →
分析 → 事件广播”全链路并校验应答，供 CTest 与现场演示。
