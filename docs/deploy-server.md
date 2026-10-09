# 服务器部署（无 GUI）

只跑服务端与命令行终端，不安装 Qt Widgets / AppKit 管理端。
部署完成后：手机扫码用 H5，道闸与用户端走 TCP 协议，管理端可选。

## 0. 形态

```
                   ┌──────────────────────────────┐
   手机浏览器 ──HTTP──▶│                              │
   （扫码即用）        │      smartpark_server        │
                   │                              │
   WebSocket ◀────────│  REST 网关 + WS 推送 + TCP   │
                   │                              │
   道闸/用户端 ─TCP──▶│       SQLite（单文件）        │
                   └──────────────────────────────┘
                                  ▲
                     静态页由 --web-root 同源伺服
```

一台服务器、一个进程、一个 SQLite 文件。客户端**都不碰数据库**。

## 1. 前置条件

| 项 | 要求 |
| --- | --- |
| 系统 | Linux x86_64（ARM 同理，改 `--arch`） |
| CMake | ≥ 3.21 |
| 编译器 | 支持 C++17（GCC 9+ / Clang 10+） |
| Qt | ≥ 6.2，**需要 5 个模块**：Core、Sql、Network、HttpServer、WebSockets |
| Python 3 | 可选。只在启用「拍照识牌脚本模式」时需要 |

> `smartpark_server` 的运行时依赖实测就这 5 个 Qt 模块，**不含 Widgets**。
> 二进制约 1 MB。

## 2. 安装 Qt

### 方式 A：aqtinstall（推荐，与开发机版本一致）

```bash
pip install aqtinstall
aqt install-qt linux desktop 6.8.3 linux_gcc_64 \
    -m qtwebsockets qthttpserver \
    -O "$HOME/Qt"
```

装完 Qt 前缀是 `$HOME/Qt/6.8.3/gcc_64`（注意 Linux 下是 `gcc_64`，不是 `macos`）。

### 方式 B：发行版包

Debian / Ubuntu：

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build \
    qt6-base-dev libqt6sql6-sqlite \
    qt6-websockets-dev qt6-httpserver-dev
```

> **注意**：`qt6-httpserver-dev` 并非所有发行版都打包，且版本可能低于 6.2。
> 装不上就退回方式 A——只需要它一个模块。

**SQLite 驱动是必须的**：项目用 `QSqlDatabase::addDatabase("QSQLITE")`。
用发行版包时确认 `libqt6sql6-sqlite` 已安装，否则启动会报找不到驱动。
用 aqtinstall 装的 Qt 自带 `plugins/sqldrivers/libqsqlite.so`。

## 3. 构建

```bash
cd /path/to/smartpark

cmake -S . -B build-server \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/gcc_64" \
    -DSMARTPARK_BUILD_MACOS_ADMIN=OFF \
    -DBUILD_TESTING=OFF

cmake --build build-server -j"$(nproc)"
```

两个开关的作用：

- `-DSMARTPARK_BUILD_MACOS_ADMIN=OFF` — 不构建 AppKit 管理端；Linux 默认关闭。当前分支不再包含 Qt Widgets 管理端。
- `-DBUILD_TESTING=OFF` — 不构建测试，部署只包含业务应用。

也可用 `server-release` 预设，默认输出到 `build/server-release`。

产物：

```
build-server/apps/server/smartpark_server     ← 服务端（REST + WS + TCP + 静态页）
build-server/apps/gate/smartpark_gate         ← 道闸终端
build-server/apps/user/smartpark_user         ← 用户命令行端
```

### 布局文件

仓库自带 `data/garage-6f.txt`（六层车库平面，75 个车位）。
**服务端不带 `--layout` 时用内置 60 车位布局**，两者签名不同——
如果指定的数据库是用另一种布局建的，服务端会拒绝启动并明确报错（不会自动清库）。

## 4. 部署布局

```bash
sudo mkdir -p /opt/smartpark/{bin,web,data}

sudo cp build-server/apps/server/smartpark_server /opt/smartpark/bin/
sudo cp build-server/apps/gate/smartpark_gate     /opt/smartpark/bin/
sudo cp build-server/apps/user/smartpark_user     /opt/smartpark/bin/

# H5 是纯静态文件，原样拷过去即可（无需构建）
sudo cp -r apps/webclient/. /opt/smartpark/web/

sudo cp data/garage-6f.txt /opt/smartpark/
sudo useradd -r -s /usr/sbin/nologin smartpark || true
sudo chown -R smartpark:smartpark /opt/smartpark
```

## 5. 启动

```bash
sudo -u smartpark /opt/smartpark/bin/smartpark_server \
    --port 9527 \
    --http-port 8080 \
    --ws-port 8081 \
    --db /opt/smartpark/data/smartpark.db \
    --layout /opt/smartpark/garage-6f.txt \
    --web-root /opt/smartpark/web \
    --advertise 10.0.0.5 \
    --site-name "XX 停车场"
```

> `--advertise` 建议显式给：多网卡 / VPN / Docker 环境下自动选取虽然会跳过虚拟
> 接口，但把对外地址写死最不容易出错。详见 10.3。

启动后会打印局域网访问地址、**带点位票据的二维码**，以及数据库与车位数量：

```
SmartPark server listening on port 9527 | REST http://10.0.0.5:8080/api/v1/meta | ws 8081 | db: ... | spots: 75

Scan to open SmartPark H5: http://10.0.0.5:8080/#/claim?t=<票据>
```

| 参数 | 说明 | 默认 |
| --- | --- | --- |
| `--port` | TCP 协议端口（道闸 / 用户端 / 管理端） | 9527 |
| `--http-port` | H5 页面 + REST API 端口 | 8080 |
| `--ws-port` | WebSocket 推送端口（`0` = 关闭推送） | 8081 |
| `--db` | SQLite 路径；不传则用 `~/.local/share/smartpark/smartpark.db` | — |
| `--layout` | 布局文件；不传用内置 60 车位 | — |
| `--web-root` | H5 静态目录；**不传就不伺服网页** | — |
| `--advertise` | 对外广播的地址（IP 或域名），横幅与二维码用它 | 自动选取 |
| `--ws-public-url` | 对外暴露的 WebSocket 地址（如 `wss://域名/ws`），反代/HTTPS 时必填 | 按请求 Host 推导 |
| `--site-name` | 点位名，扫码后 H5 显示 | SmartPark 停车场 |
| `--lpr-command` | 识牌命令模板（`%1` 替换为图片路径）；不传用内置 mock | — |
| `--selftest` | 进程内端到端自测（不联网），跑完退出 | — |

先验证一次再上服务：

```bash
/opt/smartpark/bin/smartpark_server --selftest
```

## 6. systemd

`/etc/systemd/system/smartpark.service`：

```ini
[Unit]
Description=SmartPark server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=smartpark
Group=smartpark
WorkingDirectory=/opt/smartpark
ExecStart=/opt/smartpark/bin/smartpark_server \
    --port 9527 --http-port 8080 --ws-port 8081 \
    --db /opt/smartpark/data/smartpark.db \
    --layout /opt/smartpark/garage-6f.txt \
    --web-root /opt/smartpark/web \
    --advertise 10.0.0.5 \
    --site-name "XX 停车场"
Restart=on-failure
RestartSec=3

# 崩了要能拿到栈；拒写其余目录
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/opt/smartpark/data

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now smartpark
sudo systemctl status smartpark
sudo journalctl -u smartpark -f
```

> 二维码会打进日志。用 `journalctl -u smartpark | grep "Scan to open"` 随时取回。

## 7. 端口与防火墙

| 端口 | 协议 | 是否必须对外开放 |
| --- | --- | --- |
| 8080 | HTTP | **是**（手机访问 H5） |
| 8081 | WebSocket | 直连部署要放；**反代部署不用放**，把 `/ws` 转进来即可（见 10.2） |
| 9527 | TCP 自定义协议 | 仅限内网 / 隧道。**明文，不要直接暴露公网** |

```bash
# firewalld
sudo firewall-cmd --permanent --add-port=8080/tcp --add-port=8081/tcp
sudo firewall-cmd --reload

# ufw
sudo ufw allow 8080/tcp && sudo ufw allow 8081/tcp
```

云服务器还要在**安全组**里放行同样的端口。

## 8. 客户端接入

| 客户端 | 接入方式 |
| --- | --- |
| 手机 / 平板 | 浏览器打开 `http://<服务器>:8080/`，或扫启动横幅的二维码 |
| 道闸终端 | `smartpark_gate --mode entrance --host <服务器> --port 9527 --user gate --pass smartpark` |
| 用户命令行 | `smartpark_user --host <服务器> --port 9527 --user user --pass smartpark` |
| macOS 管理端 | 登录页勾「连接远程服务端」，填地址与端口 |

内置账号（**仅新建空库时自动播种**，口令都是 `smartpark`）：

| 账号 | 角色 | 用途 |
| --- | --- | --- |
| `admin` | admin | 管理端 |
| `gate` | gate | 道闸终端 |
| `user` | user | H5 / 用户端 |

> 用已有的数据库时**不会**补种账号。要么在 H5 上注册，要么换一个新库。

## 9. 故障排查

### 服务端起不来：`The proxy type is invalid for this operation`

**症状**：服务端连监听都失败，`--selftest` 里的 listen 断言也挂。道闸自测反而是过的
（它不监听）。

**原因**：Qt 会自动读取 `all_proxy` / `http_proxy` 等环境变量，并把它套用到**所有**
socket 上——**包括监听 socket**。集群、公司内网、CI 环境普遍设了这些变量，于是
`listen()` 直接被代理层拒绝。

在 s1 上实测到的环境：

```
all_proxy=socks5h://127.0.0.1:7891
http_proxy=http://127.0.0.1:7891
```

**现状**：程序里已经显式调用 `QNetworkProxy::setApplicationProxy(NoProxy)`，
不再受这些变量影响。如果你用的是旧版本二进制，可以用下面的办法绕过：

```bash
env -u all_proxy -u ALL_PROXY -u http_proxy -u https_proxy \
    -u HTTP_PROXY -u HTTPS_PROXY smartpark_server ...
```

> 注意：用 `unset` 只对当前 shell 有效；systemd 里要写 `Environment=` 清空。

### 启动横幅里的地址不是对外地址

见 10.3。多网卡 / VPN / Docker 环境下「第一个非回环 IPv4」可能取错。

### 手机打不开页面

按顺序排查：

```bash
# 1. 服务端本机自测（排除服务端问题）
curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:8080/

# 2. 用对外 IP 自测（排除监听问题）
curl -s -o /dev/null -w '%{http_code}\n' http://<服务器IP>:8080/

# 3. 看监听与防火墙
ss -lntp | grep -E '8080|8081'
sudo firewall-cmd --list-ports
```

反代之前请先确认直连是通的。

三步都通、只有手机不行的话，问题在手机与服务器之间的链路：
手机是否在同一网段、路由器是否开了客户端隔离（AP Isolation）、
服务器上是否有代理工具接管了路由。

## 10. 已知限制

以下都是当前实现的真实约束，部署前请确认能接受。

### 10.1 明文传输

TCP 协议与 HTTP 都没有 TLS。内网 / 实验室环境可以；**公网必须**加一层：

- 用 Caddy / Nginx 反代 8080，自动签发证书
- **9527 不要暴露**，走 SSH 隧道（`ssh -L 9527:localhost:9527 服务器`）或 WireGuard

### 10.2 反代时要用 --ws-public-url 覆盖 WebSocket 地址

`/api/v1/meta` 默认按**请求 Host + 内部 ws 端口**拼 `wsUrl`：

```
直连：手机访问 10.0.0.5:8080  →  ws://10.0.0.5:8081/ws      ✓
反代：手机访问 park.example.com →  ws://park.example.com:8081/ws  ✗
```

反代场景下这个地址是错的：8081 通常没对外放行，HTTPS 页面下浏览器还会按
混合内容把它拦掉（`ws://` 出现在 `https://` 页面里）。

**做法**：反代把 `/ws` 转到内部的 ws 端口，同时用 `--ws-public-url` 告诉服务端
对外该报什么地址：

```bash
smartpark_server ... --ws-port 8081 \
    --ws-public-url wss://park.example.com/ws
```

Caddy 示例：

```
park.example.com {
    reverse_proxy /ws* 127.0.0.1:8081
    reverse_proxy       127.0.0.1:8080
}
```

Nginx 示例：

```nginx
location /ws {
    proxy_pass http://127.0.0.1:8081;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header Host $host;
}
location / {
    proxy_pass http://127.0.0.1:8080;
}
```

> `--ws-port 0`（关闭推送）时不会广播 `wsUrl`，`--ws-public-url` 也就不起作用。
> 要让反代能连上内部 ws，`--ws-port` 必须是非 0。

### 10.3 对外地址的选取

启动横幅与二维码的地址按下面的顺序确定：

1. `--advertise <IP或域名>` —— **显式指定，多网卡机器推荐直接用这个**
2. 默认路由所在、且**不是虚拟接口**的地址
3. 第一个非虚拟接口的地址
4. 第一个非回环地址（兜底）
5. `127.0.0.1`

第 2 步要求「是默认路由」**且**「非虚拟」两个条件同时成立：装了 VPN 的机器
默认路由会指向 `utun*`，只看选路结果反而会取到隧道地址。

虚拟接口（`docker*`、`br-*`、`veth*`、`tun*`、`utun*`、`feth*`、`bridge*`、
`vmnet*`、`awdl*` 等）一律跳过——它们的地址对局域网里的其他设备没有意义。

**仍然建议显式指定**。以 s1 为例，一台机器上就有 6 个非回环 IPv4：

```
ens41f3          10.108.17.55      ← 真实以太网，默认路由
ibp134s0         172.168.1.1       ← InfiniBand，也有默认路由
br-64f66944c369  172.18.0.1        ← Docker
br-3dabe6f7598b  172.19.0.1        ← Docker
docker0          172.17.0.1        ← Docker
tun0             10.144.0.1        ← VPN
```

自动选取能挑对，但**部署时把对外地址写死最省心**：

```bash
smartpark_server ... --advertise 10.108.17.55
# 或者用域名（反代场景）
smartpark_server ... --advertise park.example.com
```

验证方法：看启动横幅那行地址是不是你期望的对外地址。

### 10.4 SQLite 单写者

只能跑**一个**服务端实例，不能多副本/水平扩展。数据量小、单机场景够用。

### 10.5 同一个数据库不要同时被本地端和服务端打开

管理端的 `--local` 模式会直接读同一个 SQLite 文件。此时它的 `ParkingService`
把状态缓存在内存里，**看不到服务端的写入**，两边还可能互相覆盖。

服务器部署后，管理端一律用远程模式，不要用 `--local`。

## 11. 升级与回滚

```bash
# 升级：停 → 换二进制 → 起
sudo systemctl stop smartpark
sudo cp build-server/apps/server/smartpark_server /opt/smartpark/bin/
sudo systemctl start smartpark

# 备份：SQLite 是单文件，连同 -wal / -shm 一起拷
sudo systemctl stop smartpark
sudo cp /opt/smartpark/data/smartpark.db* /backup/
sudo systemctl start smartpark
```

数据库 schema 变更由程序自己处理（`CREATE TABLE IF NOT EXISTS` +
启动时补列），升级不需要手工迁移。
