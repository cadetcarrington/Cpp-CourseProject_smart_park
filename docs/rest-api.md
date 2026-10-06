# SmartPark REST/WebSocket 网关协议 v1（移动端 H5 接入）

本文档定义 SmartPark 服务端面向移动端 H5（扫码即用网页）的 HTTP REST 与
WebSocket 接口。网关与 TCP v1（见 `docs/tcp-protocol.md`）**同进程、共享同一
`ParkingService` 实例**，TCP 9527 协议原样保留，旧终端零改动。

设计原则：

- REST 语义与 TCP 动作一一对应（见第 8 节映射表），同一业务两条入口，
  核心状态机只此一份。
- 认证从「连接级 token」升级为「服务端 token（带过期、可吊销）」，TCP 侧
  行为不变，REST 侧统一 Bearer 认证。
- 在线缴费采用网关层支付订单（`payment_orders`），核心层 `FakePaymentGateway`
  语义不变，先真实感流水、后业务核销。

## 1. 传输与通用约定

- 端口：默认 `8080`（`--http-port` 可调）。REST 与 WebSocket 同端口：
  `GET /ws` 以 WebSocket 升级握手接入（Qt 6.8 QHttpServer 支持 WS 升级；
  若实现阶段确认升级路径受限，回退方案为独立 `QWebSocketServer` 监听
  `8081`，客户端行为不变）。
- 静态资源：网关直接伺服 H5 单页应用（`--web-root` 指定构建产物目录，
  默认 `apps/webclient/dist`），`/` 与未匹配路径回退 `index.html`（SPA 路由）。
  生产/演示为同源部署，无 CORS；开发模式（环境变量 `SMARTPARK_CORS=*`）
  允许 Vite 开发服务器跨源调试。
- 编码：JSON（UTF-8，无 BOM）。请求体 `Content-Type: application/json`。
- 时间字段为 epoch 毫秒（int64），金额单位为元（double），车型字符串
  `car | motorcycle | truck | electric`——与 TCP v1 完全一致。
- 并发模型：延续 v1 单线程事件驱动，REST/WS 与 TCP 共享一个
  `ParkingService`，不引入多线程锁库。
- 二维码接入：服务端启动时取本机局域网 IP，横幅打印
  `http://<ip>:<port>/api/v1/meta` 连接地址；二维码图片生成随 M1 H5 端
  交付（内容预留 `scene` 参数直达入口/缴费/寻车页），当前阶段可先用任意
  二维码工具包一层该 URL。

## 2. 认证与会话

- 认证方式：`Authorization: Bearer <token>`。免认证端点：`/api/v1/meta`、
  `/api/v1/auth/*`、静态资源。
- token：登录成功生成 32 字节随机 hex；服务端只存其 SHA-256 摘要
  （`auth_tokens.token_hash`），泄露库文件不泄露可用 token。
- 有效期：access token 2 小时；`POST /api/v1/auth/refresh` 换发新 token 并
  吊销旧 token（滑动续期）。课设不引入独立 refresh token，作为简化声明。
- 登录失败锁定：`users` 表账号级计数，连续 5 次失败锁定 10 分钟
  （`failed_attempts` / `locked_until_ms`），替代 TCP 侧「断连即清零」的
  连接级计数；登录失败统一返回 `AUTH_FAILED`，不区分用户不存在与密码
  错误，防账号枚举。
- 角色：`users.role`（`user | admin | gate`，默认 `user`）。REST 面向
  `user`（admin 亦可调用只读端点）；`gate.replay` 等设备动作不在 REST 暴露。
- 注册：`UserStore::registerUser` 已实现（用户名 2–24 字符不含空白、密码
  6–64 字符），REST 首次对外暴露；注册账号一律 `role=user`。
- 所有登录/注册/登出写入 `AuditLogService` 哈希链（与 TCP 同一埋点）。

## 3. 错误模型

```json
{"code": "CONFLICT", "message": "车辆已在场内"}
```

| code | HTTP | 语义 |
| --- | --- | --- |
| `VALIDATION` | 400 | 字段缺失/格式非法（message 说明具体字段） |
| `AUTH_FAILED` | 401 | 账号或密码错误（不区分两种情况） |
| `AUTH_REQUIRED` | 401 | 缺少/非法 Bearer token |
| `AUTH_EXPIRED` | 401 | token 已过期或被吊销，客户端应重新登录 |
| `FORBIDDEN` | 403 | 角色无权访问该端点 |
| `AUTH_LOCKED` | 423 | 连续失败锁定中，message 含剩余秒数 |
| `NOT_FOUND` | 404 | 资源不存在（订单/预约/车牌无在场记录等） |
| `CONFLICT` | 409 | 业务状态冲突（车辆已在场、时段重叠、一人一单等） |
| `PAYMENT_STATE` | 409 | 订单状态不允许该操作（已过期/已支付/已退款） |
| `VERSION` | 400 | URL 版本段高于服务端支持版本 |
| `INTERNAL` | 500 | 服务端内部错误 |

`message` 为中文人类可读描述，与 TCP `error` 字段同源文案。

## 4. 端点清单（总览）

| 方法与路径 | 认证 | 说明 | 对应 TCP 动作 |
| --- | --- | --- | --- |
| `GET /api/v1/meta` | 免 | 服务信息、支付模式声明 | —（新增） |
| `POST /api/v1/auth/register` | 免 | 注册 | —（新增，复用 UserStore） |
| `POST /api/v1/auth/login` | 免 | 登录，发 token | `login` |
| `POST /api/v1/auth/refresh` | Bearer | 换发 token | —（新增） |
| `POST /api/v1/auth/logout` | Bearer | 吊销当前 token | —（新增） |
| `GET /api/v1/me` | Bearer | 当前账号与角色 | —（新增） |
| `GET /api/v1/parking/status` | Bearer | 全场概览 | `parking.status` |
| `GET /api/v1/spots` | Bearer | 全量车位（不含他人车牌） | `spot.list` |
| `GET /api/v1/layout` | Bearer | 平面图几何 + 车位（渲染底图） | `admin.snapshot` 子集 |
| `GET /api/v1/records` | Bearer | 停车记录查询（可按车牌/状态） | —（新增） |
| `GET /api/v1/records/{plate}/active` | Bearer | 某车牌在场中记录 + 预估费用 | —（新增） |
| `POST /api/v1/parking/enter` | Bearer | 扫码登记入场 | `parking.enter` |
| `POST /api/v1/parking/leave` | Bearer | 离场（未预缴时按规则计费） | `parking.leave` |
| `POST /api/v1/reservations` | Bearer | 创建时段预约（付定金见第 5 节） | `reservation.create` |
| `GET /api/v1/reservations` | Bearer | 我的预约列表 | —（新增） |
| `POST /api/v1/reservations/{id}/checkin` | Bearer | 到场核销 | `reservation.checkin` |
| `POST /api/v1/reservations/{id}/cancel` | Bearer | 取消退定金 | `reservation.cancel` |
| `POST /api/v1/payments/orders` | Bearer | 创建支付订单（定金/停车费） | —（新增） |
| `GET /api/v1/payments/orders` | Bearer | 我的订单列表（?plate=&status=&page=） | —（新增） |
| `GET /api/v1/payments/orders/{id}` | Bearer | 订单状态（收银台轮询） | —（新增） |
| `POST /api/v1/payments/orders/{id}/confirm` | Bearer | 模拟支付确认（幂等） | —（新增） |
| `GET /api/v1/qr?text=...` | **免** | 文本转二维码 SVG（供 `<img>` 引用） | —（新增） |
| `GET /api/v1/me/frictionless` | Bearer | 无感支付已开通车牌列表 | —（新增） |
| `POST /api/v1/me/frictionless` | Bearer | 开通/关闭车牌无感支付 | —（新增） |
| `POST /api/v1/lpr/recognize` | Bearer | 拍照识牌 {image: base64} → {plate,...} | —（新增，脚本/mock 可插拔） |
| `GET /api/v1/guide/{plate}` | Bearer | 车位指引/反向寻车路线 | —（新增，复用 GridPlanner） |
| `GET /ws` | 升级 | WebSocket 事件推送 | 事件广播 |

分页端点（`records`、`reservations`）统一 `?page=1&pageSize=20`（上限 100），
响应含 `total`。

## 5. 在线缴费（模拟收银台闭环）

支付订单状态机：`pending → paid | expired | refunded`（创建方取消为
`cancelled`）。订单默认 **15 分钟**超时关单（服务端 QTimer 周期扫描，
与 `ReservationService` 计时器同风格）；`confirm` 幂等——重复确认同一
`paid` 订单返回 200 与相同结果，不产生二次流水。

### 5.1 预约定金（创建即收取，订单为支付凭证）

v1 实现：`POST /api/v1/reservations` 创建预约时，服务端即时收取定金
（复用核心层 `ReservationService` + `FakePaymentGateway` 语义），网关随即
落一张 `status=paid` 的 deposit 订单作为支付凭证，响应携带预期路线与订单。

```json
// 请求
{"plate": "粤B12345", "vehicleType": "car",
 "startMs": 1730000000000, "durationMin": 120, "accessible": false}
// 201 —— 时段校验/冲突检查失败时 409，message 为核心层错误文案
{"reservationId": "r_5c1", "plate": "粤B12345", "spotId": "A-12",
 "deposit": 20.0, "startMs": ..., "endMs": ...,
 "entranceIndex": 0, "exitIndex": 1,
 "entryRoute": {"points": [{"x": 2.0, "y": 3.5}], "distanceM": 18.4, "turns": 2},
 "exitRoute": {"points": [], "distanceM": 21.0, "turns": 3},
 "order": {"orderId": "po_9f3c", "outTradeNo": "SP202610049F3C",
            "kind": "deposit", "amount": 20.0, "status": "paid"}}
```

取消预约时网关自动生成 `status=refunded` 的 deposit 流水订单（对应核心层
`DepositPayment::Refund`）。「先支付、后锁位」的挂单模式列为后续增强，
需要核心层支持预约草稿状态，v1 不引入。

### 5.2 离场停车费（先缴后走，或出口正常计费）

- `POST /api/v1/payments/orders`：`{"kind": "parking_fee", "plate": "粤B12345"}`
  → 服务端取在场记录，按 `BillingService` 快照计费，返回 `amount`
  （响应多 `durationMin` 字段）。订单创建不锁车、不影响正常离场。
- `confirm` 成功即触发 `parking.leave`（费用由订单结清，不重复计费），
  响应含 `leave: {plate, spotId, durationMin, fee, paidByOrder: true}`。
- 用户未预缴而直接离场：出口 `parking.leave` 按现有规则计费，未支付订单
  置 `cancelled`（message 记录「出口已结算」）。
- 取消预约退定金：`reservation.cancel` 成功时，网关自动生成
  `kind=deposit`、`status=refunded` 的流水订单（对齐 `DepositPayment::Refund`）。

### 5.3 字段与流水对齐

`payment_orders.out_trade_no` 生成规则 `SP + yyyyMMdd + 4 字节 hex`；
订单与 `deposit_payments` 表并存：后者仍是核心层定金事实流水
（Charge/Refund/Forfeit/Apply），前者是网关层支付过程流水（下单/支付/
超时/退款），两者以 `reservation_id` / `plate` 关联，答辩可讲清两层职责。

### 5.4 无感支付（先离场后付 mock）

`POST /api/v1/me/frictionless` 开通车牌（`frictionless_plates` 表）。
任何入口广播 `parking.exited`（Gate 出口抬杆或 REST 离场）时，网关为
已开通车牌自动生成 `status=paid` 的 parking_fee 订单（金额取离场事件
费用，免费时段 ¥0 也落单以示流程可见），归属开通者账号，并广播
`payment.paid`（含 `frictionless:true`）。收银台完成支付后的 10 分钟内
同车牌离场不重复扣费（防双扣护栏）。H5「更多」页开关 + 缴费页订单列表，
Admin 大屏弹幕同步显示「⚡ 无感支付」。

## 5b. 拍照识牌（LPR，可插拔后端）

`POST /api/v1/lpr/recognize` `{"image": "<base64 JPEG ≤6MB>"}`：

- 未配置 `--lpr-command`：内置 mock，按图片 SHA-256 确定性生成演示车牌，
  响应 `{"plate": "...", "confidence": 0.87, "source": "mock", "backend": "mock"}`；
- 配置 `--lpr-command "<cmd> %1"`：图片写入临时文件后经 `/bin/sh` 执行
  命令模板（本机可接 `~/.smartpark/lpr/bin/python scripts/recognize_plate.py`，
  其 JSON 输出的 `plate` 键被直接采用），超时 30 秒，失败回 INTERNAL。
- 二维码：`GET /api/v1/qr?text=<urlencoded>` 免认证返回 SVG
  （Nayuki qrcodegen，MIT，`third_party/QR-Code-generator`）；服务端启动
  横幅同步打印 H5 接入地址的 ASCII 二维码。

## 6. 车位指引与反向寻车

`GET /api/v1/guide/{plate}` 依车辆状态返回两种语义：

- **在场车辆**（反向寻车）：以该车牌在场记录的 `spotId` 为目标，
  GridPlanner 即时规划「最近入口 → 车位」与「车位 → 出口」两条路线；
- **有有效预约**（到场引导）：返回预约时下发的 `ExpectedRoute` 快照。

```json
// 200 —— 在场车辆（source=active）：反向寻车步行路线（行人栅格，可穿越车位区）；
// 有有效预约（source=reservation）：返回预约时下发的 entryRoute / exitRoute
//（结构同 §5.1），此时响应为预约字段 + source 字段。
{"plate": "粤B12345", "source": "active", "spotId": "A-12", "zone": "A",
 "anchorIndex": 0, "anchor": {"x": 2.0, "y": 3.5},
 "route": {"points": [{"x": 2.0, "y": 3.5}], "distanceM": 18.4, "turns": 2}}
// 404 {"code": "NOT_FOUND", "message": "该车牌无在场记录或有效预约"}
```

路线为**折线拐点序列**（米制场地坐标），不下发 0.5m 栅格；H5 端
Canvas 2D 以 `GET /api/v1/layout` 的矩形底图（`siteWidth/siteHeight/
entrances/exits/obstacles/regions/spots`，与 `admin.snapshot` 同构，但
`spots` 不含他人 `plate` 字段，隐私隔离）自绘平面图与动画路线。

## 7. WebSocket 事件推送

- 接入：`GET /ws` 升级；认证用 `?token=<bearer>` 或升级后首条
  `{"type":"auth","token":...}`，60 秒未认证断开。
- 心跳：客户端每 20 秒发 `{"type":"ping"}`（服务端回 `{"type":"pong"}`）；
  服务端沿用「60 秒无帧踢除」，与 TCP 会话同一扫描器。
- 事件负载与 TCP 广播同构（第 8 节映射表同源）；新增支付事件：

| event | 触发 | payload |
| --- | --- | --- |
| `payment.paid` | 订单确认支付 | `{orderId, kind, plate, amount, reservationId?}` |
| `payment.expired` | 订单超时关单 | `{orderId, kind, plate}` |

- 推送范围：v1 简化实现——事件全量转发给所有已认证 WS 会话（内网演示
  口径，车位明细经 REST 拉取且不含他人车牌）；按角色/车牌的隐私过滤与
  `occupancy.changed` 摘要事件列为后续项。

## 8. REST ↔ TCP v1 动作映射

| REST 端点 | TCP 动作 | 差异说明 |
| --- | --- | --- |
| `POST /api/v1/auth/login` | `login` | REST 发可过期 token；TCP 仍为连接级（后续版本统一） |
| `GET /api/v1/parking/status` | `parking.status` | 负载同构 |
| `GET /api/v1/spots` | `spot.list` | REST 隐藏他人车牌 |
| `POST /api/v1/parking/enter` | `parking.enter` | 同构；广播 `parking.entered` |
| `POST /api/v1/parking/leave` | `parking.leave` | 同构；若存在已支付停车费订单则由订单结清 |
| `POST /api/v1/reservations` | `reservation.create` | 同构；网关同步生成已支付的 deposit 凭证订单 |
| `POST /api/v1/reservations/{id}/checkin` | `reservation.checkin` | REST 以预约 id 寻址（TCP 以 plate） |
| `POST /api/v1/reservations/{id}/cancel` | `reservation.cancel` | 同上；附赠退款流水 |
| `GET /api/v1/layout` | `admin.snapshot`（layout 部分） | user 角色可见，去车牌 |
| TCP 独有 | `heartbeat` / `gate.replay` / `analytics.report` / `admin.snapshot` | 设备与管理端动作，不进 REST |
| REST 新增 | — | `auth.register/refresh/logout`、`me`、`meta`、`records`、`reservations` 列表、`payments.*`、`guide` |

版本协商：URL 前缀 `/api/v1/`；未知版本段返回 `VERSION` 错误并附
`maxV` 字段。TCP 信封 `v` 字段机制不变。

## 9. 数据库 schema 演进

```sql
-- users：角色与账号级锁定（加列，旧代码按列名 SELECT 不受影响）
ALTER TABLE users ADD COLUMN role TEXT NOT NULL DEFAULT 'user';
ALTER TABLE users ADD COLUMN failed_attempts INTEGER NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN locked_until_ms INTEGER NOT NULL DEFAULT 0;

-- 服务端 token（存哈希，不存原文）
CREATE TABLE IF NOT EXISTS auth_tokens(
  token_hash    TEXT PRIMARY KEY,
  username      TEXT NOT NULL,
  role          TEXT NOT NULL,
  created_at_ms INTEGER NOT NULL,
  expires_at_ms INTEGER NOT NULL,
  revoked       INTEGER NOT NULL DEFAULT 0
);

-- 网关层支付订单流水
CREATE TABLE IF NOT EXISTS payment_orders(
  order_id      TEXT PRIMARY KEY,
  out_trade_no  TEXT NOT NULL UNIQUE,
  kind          TEXT NOT NULL CHECK(kind IN ('deposit','parking_fee')),
  username      TEXT NOT NULL,
  plate         TEXT NOT NULL,
  reservation_id TEXT,
  amount        REAL NOT NULL,
  status        TEXT NOT NULL
                CHECK(status IN ('pending','paid','expired','refunded','cancelled')),
  created_at_ms INTEGER NOT NULL,
  expire_at_ms  INTEGER NOT NULL,
  paid_at_ms    INTEGER,
  refund_at_ms  INTEGER,
  payload       TEXT NOT NULL DEFAULT '{}'
);
```

历史库打开时惰性迁移（`CREATE IF NOT EXISTS` + `ALTER` 失败忽略已存在列），
与现有「布局签名不一致可选重置」机制并存。

## 10. 实现要点

- 新增 `src/network/RestGateway`（QHttpServer 路由 + Bearer 鉴权 + 订单
  状态机 + WS hub），与 `SmartParkTcpServer` 平行，共享同一
  `ParkingService` / `UserStore` / `AuditLogService` 指针；TCP 动作实现不动。
- 事件广播抽取：`SmartParkTcpServer::broadcastEvent` 的事件来源改为
  `EventHub`（QObject 信号 `event(name, payload)`），TCP 广播与 WS 推送
  各自订阅，保证两条入口事件一致。
- `records` / `reservations` 查询：`ParkingService::records()` 内存过滤 +
  `ReservationService` 新增只读 list 方法（读 `reservations` 表），不新增
  写路径。
- 自测：`smartpark_server --selftest` 追加 REST 断言段（进程内起网关，
  用 QNetworkAccessManager 跑「注册 → 登录 → 状态 → 下单 → confirm →
  预约 → 指引 → leave」链路），CTest 新增 `smartpark_rest_selftest`。
- WS 推送采用独立端口（`--ws-port`，默认 8081，传 0 禁用）；客户端以
  `meta` 端点返回的 `wsPort`/`wsUrl` 为准，不做硬编码。QHttpServer 同端口
  升级（Qt 6.8 已提供 API）列为后续增强。

## 11. 安全边界与非目标（v1）

- 明文 HTTP/WS，限内网或 SSH 隧道（与 TCP v1 同一口径）；TLS 列后续项。
- token 服务端只存哈希；登录防枚举 + 账号级锁定；注册/登录写审计哈希链。
- 非目标：真实支付回调与验签、HTTPS/wss、微信 openid 绑定、短信验证码、
  multipart 图片上传（仅预留 `/api/v1/lpr/recognize`）、多楼层路线。

## 12. curl 冒烟示例

```bash
BASE=http://192.168.1.10:8080/api/v1

# 注册 + 登录
curl -s $BASE/auth/register -d '{"username":"alice","password":"secret1"}'
TOKEN=$(curl -s $BASE/auth/login -d '{"username":"alice","password":"secret1"}' \
        | jq -r .token)

# 概览 / 底图 / 指引
curl -s -H "Authorization: Bearer $TOKEN" $BASE/parking/status
curl -s -H "Authorization: Bearer $TOKEN" $BASE/layout

# 创建时段预约（定金即时收取，返回路线与已支付凭证订单）
curl -s -H "Authorization: Bearer $TOKEN" $BASE/reservations \
  -d '{"plate":"粤B12345","vehicleType":"car","startMs":1730000000000,"durationMin":120}'

# 离场停车费：下单 → 模拟支付（确认即结算离场）
ORDER=$(curl -s -H "Authorization: Bearer $TOKEN" $BASE/payments/orders \
  -d '{"kind":"parking_fee","plate":"粤B12345"}' | jq -r .orderId)
curl -s -X POST -H "Authorization: Bearer $TOKEN" \
  $BASE/payments/orders/$ORDER/confirm -d '{}'

# 反向寻车 + 记录
curl -s -H "Authorization: Bearer $TOKEN" $BASE/guide/粤B12345
curl -s -H "Authorization: Bearer $TOKEN" "$BASE/records?plate=粤B12345"
```
