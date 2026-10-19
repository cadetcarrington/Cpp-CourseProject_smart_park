#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 6：payment_orders 支付订单状态机。"""
import sys
sys.path.insert(0, "/Users/Zhuanz/Documents/ChatGPT/c++课设/smartpark/docs/report-build/figures")
from _figlib import *  # noqa
from _figlib import _wrap

W, H = 2000, 1440
INK3 = (71, 85, 105)


def state(d, box, name, desc, fill, border, terminal=False, sub=None):
    rrect(d, box, fill, border, width=3, radius=20)
    if terminal:
        x0, y0, x1, y1 = box
        rrect(d, (x0 + 7, y0 + 7, x1 - 7, y1 - 7), fill, border, width=2, radius=15)
    x0, y0, x1, y1 = box
    cx = (x0 + x1) / 2
    ctext(d, cx, y0 + 22, name, F(28), border)
    ctext(d, cx, y0 + 62, desc, F(16), INK2)
    if sub:
        ctext(d, cx, y0 + 88, sub, F(15), MUTED)


def trans(d, pts, text, color=INK3, dashed=False, side="right", dy=0):
    arrow(d, pts, color=color, width=3, head=15, dashed=dashed)
    lines = _wrap(d, text, F(16), 420)
    if side == "right":
        x = pts[0][0] + 14
        y = pts[0][1] + 8 + dy
        for k, ln in enumerate(lines):
            ltext(d, x, y + k * 22, ln, F(16), color)
    elif side == "above":
        mx = (pts[0][0] + pts[-1][0]) / 2
        y = pts[0][1] - 26 - (len(lines) - 1) * 22
        for k, ln in enumerate(lines):
            lw = tw(d, ln, F(16))
            d.rounded_rectangle((mx - lw / 2 - 5, y + k * 22 - 2, mx + lw / 2 + 5, y + k * 22 + 20),
                                radius=5, fill=WHITE)
            ctext(d, mx, y + k * 22, ln, F(16), color)
    elif side == "below":
        mx = (pts[0][0] + pts[-1][0]) / 2
        y = pts[0][1] + 12
        for k, ln in enumerate(lines):
            lw = tw(d, ln, F(16))
            d.rounded_rectangle((mx - lw / 2 - 5, y + k * 22 - 2, mx + lw / 2 + 5, y + k * 22 + 20),
                                radius=5, fill=WHITE)
            ctext(d, mx, y + k * 22, ln, F(16), color)
    elif side == "left":
        f = F(16)
        for k, ln in enumerate(lines):
            rtext(d, pts[0][0] - 14, y + k * 22 if False else pts[0][1] + 8 + k * 22, ln, f, color)


def dot(d, x, y, color):
    d.ellipse((x - 13, y - 13, x + 13, y + 13), fill=color)


def panel(d, box, title, lines, color=INK3):
    rrect(d, box, (249, 250, 252), GRID, width=2, radius=16)
    ltext(d, box[0] + 22, box[1] + 14, title, F(21), (51, 65, 85))
    y = box[1] + 56
    for ln in lines:
        wrapped = _wrap(d, ln, F(16), box[2] - box[0] - 40)
        for k, w in enumerate(wrapped):
            ltext(d, box[0] + 22 if k == 0 else box[0] + 44, y, w, F(16), color)
            y += 25
        y += 4


def fig6(path):
    img, d = canvas(W, H)
    title_block(d, 1000, 16, "支付订单状态机（payment_orders）",
                "状态名取自库表 CHECK 约束（src/network/RestGateway.cpp:506）：pending / paid / expired / cancelled / refunded")
    d.line([(50, 116), (1950, 116)], fill=GRID, width=2)

    PEND = (380, 330, 800, 470)
    PAID = (1240, 330, 1660, 470)
    EXP = (170, 700, 590, 840)
    CAN = (700, 700, 1120, 840)
    REF = (1240, 700, 1660, 840)

    def note(x, y, text, color, wmax=400, align="left", bg=True):
        lines = _wrap(d, text, F(16), wmax)
        for k, ln in enumerate(lines):
            lw = tw(d, ln, F(16))
            if bg:
                if align == "left":
                    d.rectangle((x - 4, y + k * 22 - 2, x + lw + 4, y + k * 22 + 20), fill=WHITE)
                else:
                    d.rectangle((x - lw - 4, y + k * 22 - 2, x + 4, y + k * 22 + 20), fill=WHITE)
            if align == "left":
                ltext(d, x, y + k * 22, ln, F(16), color)
            else:
                rtext(d, x, y + k * 22, ln, F(16), color)

    # 起始来源 1：网页下单
    dot(d, 250, 400, C_AMBER)
    arrow(d, [(264, 400), (378, 400)], color=C_AMBER, width=3, head=15)
    note(140, 300, "网页端下单", C_AMBER)
    note(110, 322, "POST /api/v1/payments/orders", C_AMBER, wmax=270)

    # 起始来源 2：预约定金订单
    dot(d, 980, 200, C_GREEN)
    d.line([(994, 200), (1450, 200)], fill=C_GREEN, width=3)
    arrow(d, [(1450, 200), (1450, 328)], color=C_GREEN, width=3, head=15, dashed=True)
    note(1010, 150, "创建预约自动生成定金订单，直接落 paid", C_GREEN, wmax=420)
    note(1010, 172, "（定金随预约收取，无独立确认流程）", C_GREEN, wmax=420)

    state(d, PEND, "pending", "已下单，等待确认支付", C_AMBER_F, C_AMBER, sub="expire_at_ms = 下单时间 + 15 分钟")
    state(d, PAID, "paid", "已支付（已结算离场）", C_GREEN_F, C_GREEN, sub="paid_at_ms 落库，可退定金")
    state(d, EXP, "expired", "超时未支付", C_SLATE_F, (100, 116, 139), terminal=True, sub="终态")
    state(d, CAN, "cancelled", "被出口结算或异常关闭", C_RED_F, C_RED, terminal=True, sub="终态")
    state(d, REF, "refunded", "已退款（定金退回）", C_BLUE_F, C_BLUE, terminal=True, sub="终态")

    # pending -> paid
    arrow(d, [(802, 400), (1238, 400)], color=C_GREEN, width=3, head=15)
    note(820, 348, "confirm 确认支付", C_GREEN, wmax=380)
    note(820, 370, "同一请求内结算离场并返回 leave", C_GREEN, wmax=380)

    # paid 幂等自环
    arrow(d, [(1600, 328), (1600, 292), (1700, 292), (1700, 328)], color=C_GREEN, width=3, head=13)
    note(1420, 222, "重复确认：幂等返回当前状态", C_GREEN, wmax=300, align="right")
    note(1420, 244, "不产生二次扣款", C_GREEN, wmax=300, align="right")

    # pending -> expired
    arrow(d, [(430, 472), (430, 600), (300, 600), (300, 698)], color=C_AMBER, width=3, head=15)
    note(448, 500, "15 分钟未确认 → expired", C_AMBER, wmax=240)
    note(448, 522, "sweep 每 15 秒扫描", C_AMBER, wmax=240)
    note(448, 544, "广播 payment.expired", C_AMBER, wmax=240)
    # pending -> cancelled
    arrow(d, [(700, 472), (700, 600), (880, 600), (880, 698)], color=C_RED, width=3, head=15)
    note(718, 490, "车辆先离场：该车牌未支付的停车费订单一并置 cancelled", C_RED, wmax=430)
    # paid -> refunded
    arrow(d, [(1400, 472), (1400, 698)], color=C_BLUE, width=3, head=15)
    note(1418, 520, "取消预约 / 爽约：新建退款流水订单（status=refunded）", C_BLUE, wmax=420)

    # 面板
    panel(d, (60, 900, 660, 1200), "状态取值与约束", [
        "· status CHECK IN ('pending','paid','expired','refunded','cancelled')（RestGateway.cpp:506）",
        "· 默认超时 15 分钟（orderTimeoutMs，RestGateway.h:44）",
        "· 超时扫描定时器 15 秒（RestGateway.cpp:163）",
        "· 仅 pending 可 confirm；非 pending / paid 返回 409 PAYMENT_STATE",
    ])
    panel(d, (700, 900, 1300, 1200), "触发点与代码位置", [
        "· 下单 handleOrderCreate（:1162）→ pending，写 expire_at_ms",
        "· 确认 handleOrderConfirm（:1232）→ paid + paid_at_ms + 结算离场",
        "· 超时 sweep（:1942）→ expired",
        "· 出口结算 performLeave（:1370）→ 其余 pending 停车费订单 cancelled",
        "· 建预约（:987）定金订单直接 paid；取消（:1148）落 refunded",
    ])
    panel(d, (1340, 900, 1940, 1200), "事件与一致性", [
        "· payment.paid / payment.expired 经 EventHub 广播到 WS 与 TCP",
        "· paidLeaveGuard_（:1275）标记已由订单结算的车牌，防重复扣费",
        "· 支付成功但离场失败 → 订单置 cancelled 并返回 409（:1266）",
        "· 与 deposit_payments（Charge/Refund/Forfeit/Apply）为两层记账",
    ])

    # 图例
    rrect(d, (60, 1230, 1940, 1400), (248, 250, 252), GRID, width=2, radius=16)
    ltext(d, 84, 1244, "图例与说明", F(21), (51, 65, 85))
    arrow(d, [(110, 1300), (210, 1300)], color=INK3, width=3, head=14)
    ltext(d, 226, 1290, "业务事件驱动的状态迁移", F(16), INK2)
    arrow(d, [(520, 1300), (620, 1300)], color=INK3, width=3, head=14, dashed=True)
    ltext(d, 636, 1290, "系统自动触发（超时 / 初始化）", F(16), INK2)
    d.ellipse((860, 1290, 886, 1316), fill=C_AMBER)
    ltext(d, 898, 1290, "起始来源", F(16), INK2)
    rrect(d, (1070, 1288, 1130, 1318), C_SLATE_F, (100, 116, 139), width=2, radius=8)
    rrect(d, (1076, 1293, 1124, 1313), C_SLATE_F, (100, 116, 139), width=1, radius=5)
    ltext(d, 1142, 1290, "终态（双圈）", F(16), INK2)
    ltext(d, 84, 1350,
          "说明：本图只覆盖网关流水表 payment_orders；核心定金字流水 deposit_payments 的 Charge / Refund / Forfeit / Apply 是另一层记账，两者通过 reservation_id 对应。",
          F(16), MUTED)
    save(img, path)


if __name__ == "__main__":
    fig6(sys.argv[1] if len(sys.argv) > 1 else "fig-payment-flow.png")
