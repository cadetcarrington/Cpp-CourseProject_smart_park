#!/usr/bin/env bash
#
# SmartPark REST + TCP 端到端测试。
#
# 起一个真实服务端进程，用真实 HTTP / WebSocket / TCP 客户端把所有业务链路走一遍。
# 与单元测试（ctest 里的 smartpark_*_tests）互补：那些测组件，这个测「装起来能不能用」。
#
# 用法：
#   tests/e2e_rest.sh --server build/apps/server/smartpark_server \
#                     --bin-dir build/apps --web-root apps/webclient
#
# 只依赖 curl / python3 / sqlite3，不需要 Qt 环境变量（脚本会尽量推断）。
# 以 ctest 方式注册时带 e2e 标签：ctest -LE e2e 可跳过。
set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

SERVER=""
BIN_DIR=""
WEB_ROOT="$REPO_ROOT/apps/webclient"
LAYOUT="$REPO_ROOT/data/garage-6f.txt"
TCP_PORT=19527
HTTP_PORT=18080
WS_PORT=18081
KEEP_TMP=0

while [ $# -gt 0 ]; do
    case "$1" in
        --server)    SERVER="$2"; shift 2 ;;
        --bin-dir)   BIN_DIR="$2"; shift 2 ;;
        --web-root)  WEB_ROOT="$2"; shift 2 ;;
        --layout)    LAYOUT="$2"; shift 2 ;;
        --tcp-port)  TCP_PORT="$2"; shift 2 ;;
        --http-port) HTTP_PORT="$2"; shift 2 ;;
        --ws-port)   WS_PORT="$2"; shift 2 ;;
        --keep)      KEEP_TMP=1; shift ;;
        -h|--help)   sed -n '2,16p' "$0"; exit 0 ;;
        *) echo "未知参数: $1" >&2; exit 2 ;;
    esac
done

# 未指定二进制目录时，从 --server 的路径推断同级目录。
if [ -z "$BIN_DIR" ]; then
    if [ -n "$SERVER" ]; then
        BIN_DIR="$(cd "$(dirname "$SERVER")/.." && pwd)"
    else
        BIN_DIR="$REPO_ROOT/build/apps"
    fi
fi
[ -z "$SERVER" ] && SERVER="$BIN_DIR/server/smartpark_server"
GATE="$BIN_DIR/gate/smartpark_gate"
USERCLI="$BIN_DIR/user/smartpark_user"

GATE_USER="gate"; ADMIN_USER="admin"; PLAIN_USER="user"; PASS="smartpark"

DIR="$(mktemp -d "${TMPDIR:-/tmp}/smartpark-e2e.XXXXXX")"
PASS_N=0; FAIL_N=0
SRVPID=""

ok()   { PASS_N=$((PASS_N+1)); printf '  [OK]   %s\n' "$1"; }
no()   { FAIL_N=$((FAIL_N+1)); printf '  [FAIL] %s\n' "$1"; }
chk()  { if [ "$2" = "$3" ]; then ok "$1 ($2)"; else no "$1: 期望 $3，得到 $2"; fi; }
# ---- 请求辅助层 ----------------------------------------------------------
# 不在 "$( ... )" 里写 \" 转义：bash 在双引号内部对 $( ) 里层的反斜杠处理
# 与外层不同，JSON body 会残留字面反斜杠，服务端解析失败返回 400。
# 统一走辅助函数，body 一律先构造好再传。
jbody(){ python3 -c "
import json,sys
a=sys.argv[1:]
d={}
for k,v in zip(a[0::2],a[1::2]):
    try: d[k]=int(v)
    except ValueError:
        try: d[k]=float(v)
        except ValueError: d[k]=v
print(json.dumps(d))
" "$@"; }

# api METHOD PATH [token] [body]  -> 响应体
api(){
    local method="$1" path="$2" token="${3:-}" body="${4:-}"
    local args=(-s -m 5 -X "$method" "$B$path")
    [ -n "$token" ] && args+=(-H "Authorization: Bearer $token")
    [ -n "$body" ] && args+=(-H 'Content-Type: application/json' -d "$body")
    curl "${args[@]}"
}
# code METHOD PATH [token] [body]  -> HTTP 状态码
code(){
    local method="$1" path="$2" token="${3:-}" body="${4:-}"
    local args=(-s -m 5 -o /dev/null -w '%{http_code}' -X "$method" "$B$path")
    [ -n "$token" ] && args+=(-H "Authorization: Bearer $token")
    [ -n "$body" ] && args+=(-H 'Content-Type: application/json' -d "$body")
    curl "${args[@]}"
}

# jget 把参数拼到 JSON 对象 d 之后求值，例如 "['token']"、"['plates'][0]['plate']"
jget() { python3 -c "import sys,json;d=json.load(sys.stdin);print(eval('d'+sys.argv[1]))" "$1" 2>/dev/null || echo "ERR"; }
jlen() { python3 -c "import sys,json;print(len(json.load(sys.stdin).get(sys.argv[1],[])))" "$1" 2>/dev/null || echo "ERR"; }
urlenc() { python3 -c "import sys,urllib.parse;print(urllib.parse.quote(sys.argv[1]))" "$1"; }

# macOS 没有 timeout(1)。这里不用「函数内后台 + $( )」——那样命令替换拿到的是
# 空输出；改成把结果写进文件、由看门狗兜底，最后再 cat 出来。
run_capture(){
    local secs="$1" out="$2" input="$3"; shift 3
    printf '%s' "$input" | "$@" > "$out" 2>&1 &
    local pid=$!
    ( sleep "$secs"; kill -0 "$pid" 2>/dev/null && kill "$pid" 2>/dev/null ) 2>/dev/null &
    local watcher=$!
    wait "$pid" 2>/dev/null
    kill "$watcher" 2>/dev/null
    wait "$watcher" 2>/dev/null
    cat "$out"
}

cleanup(){
    if [ -n "$SRVPID" ] && kill -0 "$SRVPID" 2>/dev/null; then
        kill "$SRVPID" 2>/dev/null; wait "$SRVPID" 2>/dev/null
    fi
    [ "$KEEP_TMP" -eq 1 ] || rm -rf "$DIR"
}
trap cleanup EXIT

if [ ! -x "$SERVER" ]; then
    echo "找不到服务端二进制：$SERVER" >&2
    echo "先构建，或用 --server 指定路径。" >&2
    exit 2
fi

echo "=== 启动服务端 ==="
echo "  服务端: $SERVER"
echo "  数据目录: $DIR"
"$SERVER" --port "$TCP_PORT" --http-port "$HTTP_PORT" --ws-port "$WS_PORT" \
    --db "$DIR/smartpark.db" --layout "$LAYOUT" \
    --web-root "$WEB_ROOT" --advertise 127.0.0.1 \
    --site-name "E2E 测试车场" > "$DIR/server.log" 2>&1 &
SRVPID=$!
sleep 4

if ! kill -0 "$SRVPID" 2>/dev/null; then
    echo "服务端启动失败：" >&2
    cat "$DIR/server.log" >&2
    exit 1
fi
head -1 "$DIR/server.log" | cut -c1-150 | sed 's/^/  /'
ok "服务端启动"

B="http://127.0.0.1:$HTTP_PORT/api/v1"

echo
echo "=== A. 静态资源与安全头 ==="
chk "H5 首页"   "$(curl -s -m 5 -o /dev/null -w '%{http_code}' http://127.0.0.1:$HTTP_PORT/)" 200
chk "app.js"    "$(curl -s -m 5 -o /dev/null -w '%{http_code}' http://127.0.0.1:$HTTP_PORT/app.js)" 200
chk "style.css" "$(curl -s -m 5 -o /dev/null -w '%{http_code}' http://127.0.0.1:$HTTP_PORT/style.css)" 200
HDR=$(curl -s -m 5 -D - -o /dev/null http://127.0.0.1:$HTTP_PORT/)
for h in content-security-policy x-content-type-options referrer-policy x-frame-options; do
    if echo "$HDR" | grep -qi "^$h:"; then ok "安全头 $h"; else no "缺少安全头 $h"; fi
done
# SPA 路由回退到首页，否则刷新子路由会 404
chk "SPA 回退（未知路径回首页）" \
    "$(curl -s -m 5 -o /dev/null -w '%{http_code}' http://127.0.0.1:$HTTP_PORT/nonexistent-route)" 200

echo
echo "=== B. 认证 ==="
chk "meta" "$(curl -s -m 5 -o /dev/null -w '%{http_code}' $B/meta)" 200
BODY=$(jbody username "$ADMIN_USER" password "$PASS")
AT=$(api POST /auth/login "" "$BODY" | jget "['token']")
[ ${#AT} -gt 10 ] && ok "admin 登录拿到 token" || no "admin 登录失败"
BODY=$(jbody username "$GATE_USER" password "$PASS")
GT=$(api POST /auth/login "" "$BODY" | jget "['token']")
[ ${#GT} -gt 10 ] && ok "gate 登录拿到 token" || no "gate 登录失败"
BODY=$(jbody username "$ADMIN_USER" password nope)
chk "错误口令被拒" "$(code POST /auth/login "" "$BODY")" 401

# /auth/register 返回 201 且**不带 token**，前端是注册后再登录一次
# （见 apps/webclient/app.js）。这里照做。
NU="e2e$(date +%s)"
BODY=$(jbody username "$NU" password secret123)
chk "注册新用户（201 Created）" "$(code POST /auth/register "" "$BODY")" 201
chk "重复注册被拒" "$(code POST /auth/register "" "$BODY")" 409
UT=$(api POST /auth/login "" "$BODY" | jget "['token']")
[ ${#UT} -gt 10 ] && ok "新用户登录拿到 token" || no "新用户登录失败"
chk "无 token 访问受保护接口" \
    "$(curl -s -m 5 -o /dev/null -w '%{http_code}' $B/parking/status)" 401

echo
echo "=== C. 扫码进入流程 ==="
TOK=$(sqlite3 "$DIR/smartpark.db" "select token from site_tickets where revoked=0 limit 1;" 2>/dev/null)
[ -n "$TOK" ] && ok "点位票据已落库" || no "没有点位票据"
chk "票据解析（免认证）" \
    "$(curl -s -m 5 -o /dev/null -w '%{http_code}' "$B/site/resolve?t=$TOK")" 200
chk "点位名回显" "$(curl -s -m 5 "$B/site/resolve?t=$TOK" | jget "['siteName']")" "E2E 测试车场"
chk "无效票据" "$(curl -s -m 5 -o /dev/null -w '%{http_code}' "$B/site/resolve?t=deadbeef")" 404
BODY=$(jbody plate 京A66666 ticket "$TOK")
chk "绑定车牌" "$(code POST /me/plates "$UT" "$BODY")" 200
chk "车牌列表" "$(api GET /me/plates "$UT" | jget "['plates'][0]['plate']")" "京A66666"
BODY=$(jbody plate 京A66666)
chk "重复绑定幂等" "$(api POST /me/plates "$UT" "$BODY" | jget "['created']")" "False"
chk "未登录绑定被拒" "$(code POST /me/plates "" "$(jbody plate 京B11111)")" 401

echo
echo "=== D. 车位与状态 ==="
chk "车位总数（车库布局）" "$(api GET /parking/status "$UT" | jget "['capacity']")" 75
chk "车位明细" "$(api GET /spots "$UT" | jlen spots)" 75
chk "布局接口" "$(code GET /layout "$UT")" 200

echo
echo "=== E. 入场 → 离场 → 结算 ==="
BODY=$(jbody plate 京A77777 vehicleType car)
chk "入场" "$(code POST /parking/enter "$GT" "$BODY")" 200
SPOT=$(api GET "/records/$(urlenc 京A77777)/active" "$UT" | jget "['spotId']")
if [ -n "$SPOT" ] && [ "$SPOT" != "ERR" ]; then ok "查到在场记录，车位 $SPOT"; else no "查不到在场记录"; fi
chk "占用数 +1" "$(api GET /parking/status "$UT" | jget "['occupied']")" 1
BODY=$(jbody plate 京A77777)
chk "离场" "$(code POST /parking/leave "$GT" "$BODY")" 200
REC=$(api GET /records "$UT" | jget "['total']")
[ "$REC" -ge 1 ] 2>/dev/null && ok "停车记录 $REC 条" || no "停车记录为空（total=$REC）"
FEE=$(api GET /records "$UT" | jget "['records'][0].get('fee',0)")
python3 -c "import sys;sys.exit(0 if float('$FEE')>=0 else 1)" 2>/dev/null \
    && ok "费用字段 $FEE" || no "费用字段异常 ($FEE)"
# 已经在场外再离场一次必须是冲突，而不是再结算一遍
BODY=$(jbody plate 京A77777)
chk "重复离场被拒" "$(code POST /parking/leave "$GT" "$BODY")" 409

echo
echo "=== F. 预约 → 到场确认 ==="
# ReservationRule（src/core/model/Reservation.h）：至少提前 30 分钟；到场窗口 =
# 预约时间前后各 30 分钟（gracePeriod）。两个规则刚好卡在边界，所以取 +30分5秒：
# 过得了提前量校验，窗口又会在 5 秒后打开，两条规则都能验到。
STARTMS=$(python3 -c "import time;print(int((time.time()+30*60+5)*1000))")
BODY=$(jbody plate 京A88888 vehicleType car startMs "$STARTMS" durationMin 60)
RES=$(api POST /reservations "$UT" "$BODY")
RID=$(echo "$RES" | jget "['reservationId']")
if [ -n "$RID" ] && [ "$RID" != "ERR" ]; then ok "创建预约 $RID"; else
    no "创建预约失败"; echo "     $(echo "$RES" | head -c 160)"; fi
SOON=$(python3 -c "import time;print(int((time.time()+10*60)*1000))")
BODY=$(jbody plate 京A88899 vehicleType car startMs "$SOON" durationMin 60)
chk "提前量不足被拒" "$(code POST /reservations "$UT" "$BODY")" 409
# /reservations 对非 admin 强制要求 plate（避免看到他人预约），admin 可查全部
P888=$(urlenc 京A88888)
chk "预约列表（按车牌）" "$(api GET "/reservations?plate=$P888" "$UT" | jlen reservations)" 1
chk "非 admin 不指定车牌被拒" "$(code GET /reservations "$UT")" 400
chk "admin 可查全部预约" "$(code GET /reservations "$AT")" 200
EARLY=$(code POST "/reservations/$RID/checkin" "$UT")
if [ "$EARLY" -ge 400 ] 2>/dev/null; then ok "窗口未到拒绝到场确认 ($EARLY)"; else
    no "窗口未到却允许确认 ($EARLY)"; fi
echo "     等到场窗口打开（5 秒）…"
sleep 6
chk "到场确认（窗口内）" "$(code POST "/reservations/$RID/checkin" "$UT")" 200
# /guide/<arg> 的参数是**车牌**，不是预约 id
chk "寻车指引（按车牌）" "$(code GET "/guide/$(urlenc 京A88888)" "$UT")" 200

echo
echo "=== G. 模拟收银台 ==="
# 下单要求车牌**当前在场**，且 kind 只能是 parking_fee（定金单随预约自动生成）
BODY=$(jbody plate 京A33333 vehicleType car)
chk "造一条在场记录" "$(code POST /parking/enter "$GT" "$BODY")" 200
BODY=$(jbody kind parking_fee plate 京A00000)
chk "不在场车牌下单被拒" "$(code POST /payments/orders "$UT" "$BODY")" 404
BODY=$(jbody kind parking_fee plate 京A33333)
OID=$(api POST /payments/orders "$UT" "$BODY" | jget "['orderId']")
if [ -n "$OID" ] && [ "$OID" != "ERR" ]; then ok "创建支付单 $OID"; else no "创建支付单失败"; fi
chk "确认支付" "$(code POST "/payments/orders/$OID/confirm" "$UT" '{}')" 200
chk "订单状态" "$(api GET "/payments/orders/$OID" "$UT" | jget "['status']")" "paid"
ORDN=$(api GET "/payments/orders?pageSize=8" "$UT" | jlen orders)
[ "$ORDN" -ge 1 ] 2>/dev/null && ok "缴费记录列表 $ORDN 张（含预约定金单）" || no "缴费记录列表为空"

echo
echo "=== H. 二维码 / 车牌 ==="
chk "服务端生成二维码" "$(curl -s -m 5 -o /dev/null -w '%{http_code}' "$B/qr?text=hello")" 200
chk "无感支付车牌绑定" "$(code POST /me/frictionless "$UT" "$(jbody plate 京A66666)")" 200

echo
echo "=== I. WebSocket 实时推送 ==="
# 手写最小 WS 客户端：握手 + 认证帧 + 等一个业务事件帧。
WSRES=$(python3 - "$HTTP_PORT" "$WS_PORT" "$UT" <<'PY'
import base64, json, os, socket, struct, sys, threading, time, urllib.request
http, ws, token = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
def frame(payload):
    d = payload.encode(); h = bytearray([0x81]); n = len(d)
    if n < 126: h.append(0x80 | n)
    elif n < 65536: h.append(0x80 | 126); h += struct.pack('>H', n)
    else: h.append(0x80 | 127); h += struct.pack('>Q', n)
    m = os.urandom(4); h += m
    return bytes(h) + bytes(b ^ m[i % 4] for i, b in enumerate(d))
def read_frame(s):
    b = s.recv(2)
    if len(b) < 2: return None
    ln = b[1] & 0x7F
    if ln == 126: ln = struct.unpack('>H', s.recv(2))[0]
    elif ln == 127: ln = struct.unpack('>Q', s.recv(8))[0]
    data = b''
    while len(data) < ln: data += s.recv(ln - len(data))
    return data.decode('utf-8', 'replace')
try:
    s = socket.create_connection(('127.0.0.1', ws), timeout=8)
except OSError as e:
    print('HANDSHAKE_FAIL:' + str(e)); sys.exit(0)
key = base64.b64encode(os.urandom(16)).decode()
s.sendall(("GET /ws HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
           "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
           "Sec-WebSocket-Version: 13\r\n\r\n" % key).encode())
resp = s.recv(4096).decode('utf-8', 'replace')
if '101' not in resp.split('\r\n')[0]:
    print('HANDSHAKE_FAIL'); sys.exit(0)
print('HANDSHAKE_OK')
s.sendall(frame(json.dumps({'type': 'auth', 'token': token})))
auth = read_frame(s)
print('AUTH_OK' if auth and '"ok"' in auth else 'AUTH_FAIL:' + str(auth)[:80])
def trigger():
    time.sleep(0.4)
    req = urllib.request.Request(
        'http://127.0.0.1:%d/api/v1/parking/enter' % http,
        data=json.dumps({'plate': '京A99999', 'vehicleType': 'car'}).encode(),
        headers={'Content-Type': 'application/json',
                 'Authorization': 'Bearer ' + token})
    try: urllib.request.urlopen(req, timeout=5).read()
    except Exception: pass
threading.Thread(target=trigger, daemon=True).start()
s.settimeout(6)
try:
    for _ in range(5):
        msg = read_frame(s)
        if msg and 'event' in msg and '京A99999' in msg:
            print('PUSH_OK'); break
    else:
        print('PUSH_TIMEOUT')
except Exception as e:
    print('PUSH_ERR:' + str(e)[:60])
PY
)
echo "$WSRES" | sed 's/^/    /'
echo "$WSRES" | grep -q "HANDSHAKE_OK" && ok "WebSocket 握手 101" || no "WebSocket 握手失败"
echo "$WSRES" | grep -q "AUTH_OK" && ok "WebSocket 认证" || no "WebSocket 认证失败"
echo "$WSRES" | grep -q "PUSH_OK" && ok "实时事件推送收到" || no "未收到实时推送"

echo
echo "=== I2. 跨入口事件：REST 动作要能到 TCP 客户端 ==="
# 这条曾经是坏的：REST 入口的动只 publish 到 EventHub，而 TCP 服务端没有订阅 hub，
# 于是网页上刚建的预约在 macOS 管理端（TCP）看不到——远程模式靠事件触发重拉快照。
TCPEVT=$(python3 - "$TCP_PORT" "$HTTP_PORT" "$UT" <<'PY'
import json, socket, struct, sys, threading, time, urllib.request
tcp, http, token = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
def send(sock, obj):
    data = json.dumps(obj, ensure_ascii=False).encode()
    sock.sendall(struct.pack('>I', len(data)) + data)
def recv(sock):
    head = b''
    while len(head) < 4:
        chunk = sock.recv(4 - len(head))
        if not chunk: return None
        head += chunk
    n = struct.unpack('>I', head)[0]
    body = b''
    while len(body) < n:
        chunk = sock.recv(n - len(body))
        if not chunk: return None
        body += chunk
    return json.loads(body.decode())
try:
    s = socket.create_connection(('127.0.0.1', tcp), timeout=10)
except OSError as e:
    print('TCP_CONNECT_FAIL:' + str(e)); sys.exit(0)
# 注意：TCP 协议的登录字段是 user/pass，与 REST 的 username/password 不同
send(s, {'type': 'request', 'id': '1', 'action': 'login',
         'payload': {'user': 'gate', 'pass': 'smartpark'}})
resp = recv(s)
if not resp or not resp.get('ok'):
    print('TCP_LOGIN_FAIL:' + str(resp)[:80]); sys.exit(0)
print('TCP_LOGIN_OK')

def trigger():
    time.sleep(0.5)
    start = int((time.time() + 45 * 60) * 1000)
    body = json.dumps({'plate': '京A77788', 'vehicleType': 'car',
                       'startMs': start, 'durationMin': 60}).encode()
    req = urllib.request.Request(
        'http://127.0.0.1:%d/api/v1/reservations' % http, data=body,
        headers={'Content-Type': 'application/json',
                 'Authorization': 'Bearer ' + token})
    try: urllib.request.urlopen(req, timeout=5).read()
    except Exception: pass
threading.Thread(target=trigger, daemon=True).start()

s.settimeout(6)
try:
    for _ in range(6):
        msg = recv(s)
        if msg and msg.get('type') == 'event' and msg.get('event') == 'reservation.created':
            print('CROSS_EVENT_OK'); break
    else:
        print('CROSS_EVENT_TIMEOUT')
except Exception as e:
    print('CROSS_EVENT_ERR:' + str(e)[:60])
PY
)
echo "$TCPEVT" | sed 's/^/    /'
echo "$TCPEVT" | grep -q "TCP_LOGIN_OK" && ok "TCP 客户端登录" || no "TCP 客户端登录失败"
echo "$TCPEVT" | grep -q "CROSS_EVENT_OK" && ok "REST 创建预约的事件到达 TCP 客户端" \
    || no "TCP 客户端收不到 REST 入口的事件"

echo
echo "=== J. TCP 协议（道闸 / 用户端） ==="
if [ -x "$GATE" ]; then
    chk "道闸自测" "$("$GATE" --selftest 2>&1 | grep -c 'PASS')" 1
    GATEOUT=$(run_capture 20 "$DIR/gate.out" 'status
京A55555
status
quit
' "$GATE" --mode entrance --host 127.0.0.1 --port "$TCP_PORT" \
              --user "$GATE_USER" --pass "$PASS" --queue "$DIR/gate.jsonl")
    echo "$GATEOUT" | grep -q "在线" && ok "道闸连上服务端" || no "道闸连不上"
    echo "$GATEOUT" | grep -qE "入场|放行|京A55555" && ok "道闸入场成功" || no "道闸入场失败"
else
    echo "  (跳过：未找到 $GATE)"
fi
if [ -x "$USERCLI" ]; then
    USEROUT=$(run_capture 20 "$DIR/user.out" 'status
quit
' "$USERCLI" --host 127.0.0.1 --port "$TCP_PORT" \
              --user "$PLAIN_USER" --pass "$PASS")
    echo "$USEROUT" | grep -qE "余位|车位|空闲" && ok "用户端查询成功" || no "用户端查询失败"
else
    echo "  (跳过：未找到 $USERCLI)"
fi

echo
echo "=== K. 数据一致性（相对变化，不依赖固定数字） ==="
OCC0=$(api GET /parking/status "$UT" | jget "['occupied']")
REC0=$(api GET /records "$UT" | jget "['total']")
api POST /parking/enter "$GT" "$(jbody plate 京A22222 vehicleType car)" > /dev/null
OCC1=$(api GET /parking/status "$UT" | jget "['occupied']")
REC1=$(api GET /records "$UT" | jget "['total']")
chk "入场使占用数 +1" "$((OCC1-OCC0))" 1
chk "入场使记录数 +1" "$((REC1-REC0))" 1
api POST /parking/leave "$GT" "$(jbody plate 京A22222)" > /dev/null
OCC2=$(api GET /parking/status "$UT" | jget "['occupied']")
REC2=$(api GET /records "$UT" | jget "['total']")
chk "离场使占用数 -1" "$((OCC1-OCC2))" 1
chk "离场不新增记录（同一条转已结算）" "$((REC2-REC1))" 0
chk "离场后状态为已结算" \
    "$(api GET "/records?plate=$(urlenc 京A22222)" "$UT" | jget "['records'][0]['status']")" "closed"

echo
echo "================================"
printf '  通过 %d 项，失败 %d 项\n' "$PASS_N" "$FAIL_N"
echo "================================"
[ "$FAIL_N" -eq 0 ] || exit 1
exit 0
