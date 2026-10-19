#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 5：远程模式时序图（macOS 管理端 ↔ TCP ↔ ParkingService ↔ SQLite ↔ WebSocket）。"""
import sys
sys.path.insert(0, "/Users/Zhuanz/Documents/ChatGPT/c++课设/smartpark/docs/report-build/figures")
from _figlib import *  # noqa
from _figlib import _wrap, _dashed

W = 2000
PARTS = [
    ("macOS 管理端", "RemoteDataSource / 界面", C_BLUE_F, C_BLUE),
    ("TCP 服务 :9527", "SmartParkTcpServer", C_INDIGO_F, C_INDIGO),
    ("核心业务", "ParkingService", C_PURPLE_F, C_PURPLE),
    ("持久化", "ParkingRepository / SQLite", C_AMBER_F, C_AMBER),
    ("事件总线", "EventHub（发布/订阅）", C_TEAL_F, C_TEAL),
    ("WebSocket :18081", "RestGateway → 网页端", C_ORANGE_F, C_ORANGE),
]
CX = [216 + i * 306 for i in range(6)]

STEPS = [
    ("self", 0, "车辆作业页：识别车牌 沪A·12345，点击“入场”", C_BLUE),
    ("msg", 0, 1, "组帧：4 字节大端长度 + JSON（version=1, requestId, action=parking.enter）", C_INDIGO, False),
    ("self", 1, "会话校验：role ∈ {user, admin}；按 action 路由；校验车牌与在场记录", C_INDIGO),
    ("msg", 1, 2, "ParkingService.enter(plate, 入场时间)", C_INDIGO, False),
    ("self", 2, "命中预约则核销，否则 SpotAllocator.propose 选位 + GridPlanner 规划路线", C_PURPLE),
    ("msg", 2, 3, "事务：INSERT parking_records + UPDATE parking_spots（部分唯一索引）", C_AMBER, False),
    ("self", 3, "SQLite 事务提交；失败回滚内存状态并返回错误码", C_AMBER),
    ("msg", 3, 2, "返回记录 id、车位与路线", C_AMBER, True),
    ("msg", 2, 4, "EventHub.publish(\"parking.entered\", payload, Origin::Remote)", C_TEAL, False),
    ("msg", 4, 1, "事件回推 TCP 在线会话", C_TEAL, False),
    ("msg", 1, 0, "事件帧 + response{ok:true, record, spot} → 界面列表与地图刷新", C_INDIGO, False),
    ("msg", 4, 5, "同一事件广播给 WebSocket 订阅者", C_ORANGE, False),
    ("self", 5, "先 auth 后订阅；60 秒无帧踢除 → 网页端余位与记录实时刷新", C_ORANGE),
]


def header(d, i):
    name, sub, fill, border = PARTS[i]
    x = CX[i]
    rrect(d, (x - 142, 150, x + 142, 240), fill, border, width=3, radius=14)
    ctext(d, x, 168, name, F(23), border)
    ctext(d, x, 202, sub, F(15), INK2)


def layout():
    y = 300
    out = []
    for st in STEPS:
        if st[0] == "self":
            lines = _wrap(None, st[2], F(17), 400)
        else:
            lines = _wrap(None, st[3], F(17), 470)
        band = 44 + 23 * (len(lines) - 1)
        y += band
        out.append((st, y, len(lines)))
        y += 40
    return out, y


def _wrap_none(text, font, maxw):
    # 用临时 draw 量测：_figlib._wrap 需要 draw 对象，这里用 F 与 tw 近似
    from PIL import Image, ImageDraw
    tmp = ImageDraw.Draw(Image.new("RGB", (10, 10)))
    return _wrap(tmp, text, font, maxw)


_wrap_orig = _wrap


def wrap(d, text, font, maxw):
    return _wrap(d, text, font, maxw)


def main(path):
    global _wrap
    from PIL import Image, ImageDraw
    tmp = ImageDraw.Draw(Image.new("RGB", (10, 10)))
    _wrap = lambda dd, t, f, m: _wrap_orig(tmp, t, f, m)
    laid, endy = layout()
    _wrap = _wrap_orig

    H = endy + 40 + 300
    img, d = canvas(W, H)
    title_block(d, 1000, 16, "远程模式时序图（入场操作与实时推送）",
                "macOS 管理端经 TCP 调用统一服务端，服务端进程内调用核心库并落库，事件经 TCP / WebSocket 双通道回推")
    d.line([(50, 116), (1950, 116)], fill=GRID, width=2)
    for i in range(6):
        header(d, i)

    top, bot = 240, laid[-1][1] + 60
    for x in CX:
        _dashed_orig(d, (x, top), (x, bot), (148, 163, 184), 2, dash=11, gap=8)

    # 激活条
    span = {i: [None, None] for i in range(6)}
    for st, y, _ in laid:
        idx = [st[1]] if st[0] == "self" else [st[1], st[2]]
        for i in idx:
            span[i][0] = y if span[i][0] is None else min(span[i][0], y)
            span[i][1] = y if span[i][1] is None else max(span[i][1], y)
    for i, (a, b) in span.items():
        d.rounded_rectangle((CX[i] - 11, a - 18, CX[i] + 11, b + 26), radius=7,
                            fill=(241, 245, 249), outline=PARTS[i][3], width=2)

    for n, (st, y, nl) in enumerate(laid, 1):
        if st[0] == "self":
            _, i, text, color = st
            x = CX[i]
            if x + 60 + 400 > W - 30:
                arrow(d, [(x, y), (x - 46, y), (x - 46, y + 32), (x - 14, y + 32)],
                      color=color, width=3, head=13)
            else:
                arrow(d, [(x, y), (x + 46, y), (x + 46, y + 32), (x + 14, y + 32)],
                      color=color, width=3, head=13)
            lines = _wrap(d, text, F(17), 400)
            if x + 60 + 400 > W - 30:
                for k, ln in enumerate(lines):
                    rtext(d, x - 60, y - 24 + k * 23, ln, F(17), color)
            else:
                for k, ln in enumerate(lines):
                    ltext(d, x + 60, y - 24 + k * 23, ln, F(17), color)
            px, py = x, y
            color = st[3]
        else:
            _, i, j, text, color, dashed = st
            arrow(d, [(CX[i], y), (CX[j], y)], color=color, width=3, head=15, dashed=dashed)
            lines = _wrap(d, text, F(17), 470)
            cx = (CX[i] + CX[j]) / 2
            ty = y - 26 - (len(lines) - 1) * 23
            for k, ln in enumerate(lines):
                lw = tw(d, ln, F(17))
                d.rounded_rectangle((cx - lw / 2 - 6, ty + k * 23 - 2, cx + lw / 2 + 6, ty + k * 23 + 21),
                                    radius=5, fill=WHITE)
                ctext(d, cx, ty + k * 23, ln, F(17), color)
            px, py = (CX[i] + CX[j]) / 2, y
            color = st[4]
        r = 14
        d.ellipse((CX[st[1]] - r, y - r, CX[st[1]] + r, y + r), fill=color)
        ctext(d, CX[st[1]], y - 12, str(n), F(15), WHITE)

    # 底部约定
    by = H - 300
    rrect(d, (50, by, 1950, H - 30), (248, 250, 252), GRID, width=2, radius=16)
    ltext(d, 74, by + 14, "关键约定", F(23), (51, 65, 85))
    left = [
        "· 管理端不直连数据库：远程模式全部业务经服务端校验与落库",
        "· 同一核心库两处复用：本地模式 ParkingBridge 同进程直调，",
        "   远程模式由服务端在进程内调用",
        "· 请求/响应与事件共用同一帧格式（协议版本 1，单帧上限 9 MiB，",
        "   单图上限 6 MiB）",
    ]
    right = [
        "· 心跳 20 秒；断线重连退避 1–15 秒；在途写请求断线不自动重放，",
        "   避免重复扣费",
        "· 事件双通道：TCP 面向管理端与道闸，WebSocket 面向浏览器，",
        "   推送与查询分离",
        "· 服务端启动按布局签名校验布局文件与数据库快照一致",
    ]
    y = by + 60
    for ln in left:
        ltext(d, 74, y, ln, F(16), INK2)
        y += 27
    y = by + 60
    for ln in right:
        ltext(d, 1010, y, ln, F(16), INK2)
        y += 27
    save(img, path)


_dashed_orig = _dashed

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "fig-remote-seq.png")
