#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 1：系统总体架构。"""
import sys
from PIL import Image, ImageDraw
sys.path.insert(0, "/Users/Zhuanz/Documents/ChatGPT/c++课设/smartpark/docs/report-build/figures")
from _figlib import *  # noqa


def vtext(img, d, x, y, text, size, fill):
    f = F(size)
    w = int(tw(d, text, f)) + 4
    h = size + 8
    tmp = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    td = ImageDraw.Draw(tmp)
    td.text((2, 2), text, font=f, fill=fill)
    rot = tmp.rotate(90, expand=True)
    img.paste(rot, (x, y), rot)


def fig1(path):
    # 图例说明（局部覆盖）
    img, d = canvas(2000, 1500)

    title_block(d, 1000, 16, "SmartPark 系统总体架构",
                "macOS 管理端 · 统一服务端 · 网页用户端 · 道闸终端 · 命令行用户端（C++17 / Qt 6.8 / AppKit）")
    d.line([(50, 116), (1950, 116)], fill=GRID, width=2)

    # ---- 客户端层 ----
    container(d, (50, 132, 1520, 392), "客户端层",
              "四类客户端应用，共用同一套 TCP / HTTP / WebSocket 协议")
    cw = [70, 438, 806, 1174]
    cw_w = 343
    cards = [
        ("macOS 管理端", ["apps/macos", "AppKit + Objective-C++", "本地模式 / 远程模式"],
         C_BLUE_F, C_BLUE),
        ("网页用户端", ["apps/webclient", "原生 HTML/CSS/JS SPA", "扫码 · 预约 · 缴费"],
         C_ORANGE_F, C_ORANGE),
        ("道闸终端", ["apps/gate", "BarrierGate + OfflineQueue", "断网补报 gate.replay"],
         C_GREEN_F, C_GREEN),
        ("命令行用户端", ["apps/user", "终端交互", "在场查询 · 预约 · 取消"],
         C_GRAY_F, C_GRAY),
    ]
    for x, (t, lines, fill, border) in zip(cw, cards):
        card(d, (x, 180, x + cw_w, 352), t, lines, fill=fill, border=border,
             tsize=24, lsize=18, tcolor=border)

    # ---- 网络总线 ----
    bus_y = 470
    d.line([(90, bus_y), (1490, bus_y)], fill=(71, 85, 105), width=4)
    drops = [
        (241, "TCP :9527（远程）"),
        (609, "HTTP :18080 / WS :18081"),
        (977, "TCP :9527"),
        (1345, "TCP :9527"),
    ]
    for cx, lab in drops:
        arrow(d, [(cx, 354), (cx, bus_y - 2)], color=(71, 85, 105), width=3)
        ltext(d, cx + 14, bus_y - 72, lab, F(17), (51, 65, 85))

    # ---- 服务端层 ----
    container(d, (50, 520, 1520, 810), "服务端层",
              "统一服务端 smartpark_server：TCP 服务 + REST 网关 + WebSocket 推送")
    sw = [70, 438, 806, 1174]
    servers = [
        ("TCP 服务 :9527", ["SmartParkTcpServer", "长度前缀帧 + UTF-8 JSON", "登录 / 入场 / 离场 / 补报"],
         C_INDIGO_F, C_INDIGO),
        ("REST 网关 :18080", ["RestGateway", "34 个端点 + Bearer token", "静态资源伺服网页端"],
         C_INDIGO_F, C_INDIGO),
        ("WebSocket :18081", ["实时事件推送", "payment.paid / parking.exited", "60 秒无帧踢除"],
         C_INDIGO_F, C_INDIGO),
        ("识牌后端", ["POST /api/v1/lpr/recognize", "单图 ≤ 6 MiB", "60 秒看门狗"],
         C_INDIGO_F, C_INDIGO),
    ]
    for x, (t, lines, fill, border) in zip(sw, servers):
        card(d, (x, 578, x + cw_w, 792), t, lines, fill=fill, border=border,
             tsize=24, lsize=18, tcolor=border)
        cx = x + cw_w / 2
        arrow(d, [(cx, bus_y + 2), (cx, 576)], color=(71, 85, 105), width=3)

    # ---- 服务端 -> 核心 ----
    arrow(d, [(785, 812), (785, 878)], color=LINE, width=3)
    label(d, 990, 822, "进程内链接核心库：server → src/core", size=18)

    # ---- 核心业务层 ----
    container(d, (50, 880, 1520, 1240), "核心业务层",
              "src/core：领域模型 · 业务服务 · 算法 · 持久化（本地模式与远程服务端共用）")
    core_w, core_h = 455, 122
    xs = [70, 555, 1040]
    ys = [936, 1078]
    cores = [
        ("ParkingService", ["入场 / 离场 / 预约 / 查询", "业务聚合根 · 事务编排"]),
        ("SpotAllocator + GridPlanner", ["加权评分选位 · 最近车位", "A* / Dijkstra 路径规划"]),
        ("ReservationService", ["预约规则 · 定金 · 爽约", "Billing 计费规则"]),
        ("AnalyticsEngine", ["统计快照 · 6 小时预测", "ParkingInsightEngine 风险"]),
        ("UserStore + AuditLogService", ["账号 / token / 角色", "SHA-256 哈希链审计"]),
        ("ParkingRepository", ["QSQLITE 连接与事务", "先内存后落库 · 失败回滚"]),
    ]
    for i, (t, lines) in enumerate(cores):
        x = xs[i % 3]
        y = ys[i // 3]
        card(d, (x, y, x + core_w, y + core_h), t, lines, fill=C_PURPLE_F,
             border=C_PURPLE, tsize=23, lsize=17, tcolor=C_PURPLE, gap=5)

    # ---- 右侧支撑列 ----
    # 图例
    legend = (1620, 132, 1950, 392)
    rrect(d, legend, WHITE, GRID, width=2, radius=16)
    ltext(d, 1648, 146, "图 例", F(22), (71, 85, 105))
    arrow(d, [(1648, 203), (1718, 203)], color=LINE, width=3)
    ltext(d, 1730, 185, "网络调用", F(18), INK2)
    arrow(d, [(1648, 253), (1718, 253)], color=C_BLUE, width=3, dashed=True)
    ltext(d, 1730, 235, "进程内直调", F(18), INK2)
    rrect(d, (1648, 288, 1718, 320), C_PURPLE_F, C_PURPLE, width=2, radius=8)
    ltext(d, 1730, 291, "功能模块", F(18), INK2)
    rrect(d, (1648, 342, 1718, 372), C_SLATE_F, GRID, width=2, radius=8)
    ltext(d, 1730, 343, "分层 / 分组容器", F(18), INK2)

    card(d, (1620, 520, 1950, 810), "Python 识牌进程",
         ["YOLO 检测 + PP-OCRv5", "scripts/recognize_plate.py", "JSON 输出车牌/置信度"],
         fill=C_TEAL_F, border=C_TEAL, tsize=24, lsize=17, tcolor=C_TEAL)
    card(d, (1620, 880, 1950, 1240), "SQLite 数据库",
         ["12 张表", "parking_spots / parking_records",
          "layout_snapshot / reservations", "bookings / deposit_payments",
          "payment_orders / users", "auth_tokens / user_plates",
          "site_tickets / audit_logs"],
         fill=C_AMBER_F, border=C_AMBER, tsize=24, lsize=16, tcolor=C_AMBER,
         gap=6)

    # 服务端 -> Python（子进程）
    arrow(d, [(1522, 660), (1618, 660)], color=C_TEAL, width=3)
    arrow(d, [(1618, 710), (1522, 710)], color=C_TEAL, width=3)
    label(d, 1571, 616, "调用", size=15)
    label(d, 1571, 722, "返回", size=15)

    # 核心 -> SQLite
    arrow(d, [(1522, 1139), (1618, 1139)], color=C_AMBER, width=3)
    label(d, 1571, 1095, "SQL 事务", size=15)

    # ---- 本地模式虚线（左侧走廊） ----
    arrow(d, [(70, 266), (20, 266), (20, 1150), (48, 1150)], color=C_BLUE,
          width=3, dashed=True)
    vtext(img, d, 24, 700, "本地模式：管理端同进程直调核心", 19, C_BLUE)

    # ---- 底部运行约定 ----
    rrect(d, (50, 1290, 1570, 1470), C_SLATE_F, GRID, width=2, radius=18)
    ltext(d, 74, 1306, "运行与协议约定（默认配置）", F(23), (71, 85, 105))
    rows = [
        ("TCP :9527", "4 字节大端长度 + JSON 帧，协议版本 1，帧上限 9 MiB"),
        ("HTTP :18080", "REST 34 端点，Bearer token（SHA-256 存库），2 小时有效"),
        ("WS :18081", "先 auth 后推送，客户端 20 秒 ping，服务端 60 秒踢除"),
        ("数据与文件", "单文件 SQLite 12 表；布局签名校验；单图 ≤ 6 MiB"),
    ]
    y = 1350
    for k, v in rows:
        ltext(d, 80, y, k, F(20), C_INDIGO)
        ltext(d, 262, y, v, F(19), INK2)
        y += 30

    save(img, path)


if __name__ == "__main__":
    fig1(sys.argv[1] if len(sys.argv) > 1 else "fig-architecture.png")
