#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 4：SQLite 12 张表 ER 关系图。"""
import math
import sys
sys.path.insert(0, "/Users/Zhuanz/Documents/ChatGPT/c++课设/smartpark/docs/report-build/figures")
from _figlib import *  # noqa
from _figlib import _wrap

BADGE = {"PK": ((219, 234, 254), (37, 99, 235)),
         "FK": ((254, 226, 226), (185, 28, 28)),
         "UQ": ((254, 249, 195), (161, 98, 7))}


def table(d, box, name, purpose, fields, border, hfill, cols=1, note=None):
    x0, y0, x1, y1 = box
    rrect(d, box, WHITE, border, width=3, radius=14)
    d.rounded_rectangle((x0, y0, x1, y0 + 40), radius=14, fill=hfill, outline=None)
    d.rectangle((x0, y0 + 26, x1, y0 + 40), fill=hfill)
    ltext(d, x0 + 16, y0 + 7, name, F(22), border)
    pw = tw(d, name, F(22))
    if pw + tw(d, purpose, F(15)) + 52 > (x1 - x0):
        rtext(d, x1 - 14, y0 + 12, purpose, F(14), MUTED)
    else:
        ltext(d, x0 + 26 + pw, y0 + 13, purpose, F(15), MUTED)
    n = len(fields)
    rows = math.ceil(n / cols)
    cw = (x1 - x0 - 24) / cols
    for i, (badge, fname, ftype) in enumerate(fields):
        c, r = i // rows, i % rows
        cx = x0 + 12 + c * cw
        cy = y0 + 52 + r * 28
        if badge:
            bg, fg = BADGE[badge]
            bw = tw(d, badge, F(12)) + 10
            d.rounded_rectangle((cx, cy + 5, cx + bw, cy + 21), radius=5, fill=bg, outline=None)
            ctext(d, cx + bw / 2, cy + 7, badge, F(12), fg)
            cx += bw + 8
        ltext(d, cx, cy + 4, fname, F(16), INK)
        tx = cx + tw(d, fname, F(16)) + 10
        ltext(d, tx, cy + 6, ftype, F(14), MUTED)
        if tx + tw(d, ftype, F(14)) > x0 + 12 + (c + 1) * cw - 6:
            print("WARN field overflow:", name, fname, ftype)
    need = 52 + rows * 28 + 12
    if note:
        lines = _wrap(d, note, F(14), x1 - x0 - 28)
        d.line([(x0 + 8, y0 + 52 + rows * 28 + 2), (x1 - 8, y0 + 52 + rows * 28 + 2)],
               fill=(226, 232, 240), width=1)
        ny = y0 + 58 + rows * 28
        for ln in lines:
            ltext(d, x0 + 14, ny, ln, F(14), MUTED)
            ny += 21
        need = 58 + rows * 28 + len(lines) * 21 + 8
    if need > y1 - y0:
        print("WARN table height:", name, "need", need, "have", y1 - y0)


def group(d, box, caption, sub, color):
    rrect(d, box, (249, 250, 252), color, width=3, radius=18)
    ltext(d, box[0] + 22, box[1] + 10, caption, F(23), color)
    ltext(d, box[0] + 22 + tw(d, caption, F(23)) + 14, box[1] + 15, sub, F(16), MUTED)


def rel(d, pts, text, color=(71, 85, 105), dashed=False, fill=WHITE, below=False):
    arrow(d, pts, color=color, width=2, dashed=dashed)
    if not text:
        return
    f = F(15)
    w = tw(d, text, f)
    segs = []
    for i in range(len(pts) - 1):
        (x0, y0), (x1, y1) = pts[i], pts[i + 1]
        segs.append((abs(x1 - x0), "h", x0, x1, y0) if y0 == y1 else (abs(y1 - y0), "v", y0, y1, x0))
    segs.sort(key=lambda s: s[0], reverse=True)
    _, orient, a, b, c = segs[0]
    m = (a + b) / 2
    if orient == "h":
        if below:
            box = (m - w / 2 - 6, c + 4, m + w / 2 + 6, c + 26)
        else:
            box = (m - w / 2 - 6, c - 25, m + w / 2 + 6, c - 3)
    else:
        box = (c + 9, m - 12, c + w + 21, m + 10)
    d.rounded_rectangle(box, radius=5, fill=fill)
    ctext(d, (box[0] + box[2]) / 2, box[1] + 3, text, f, (71, 85, 105))


def fig4(path):
    img, d = canvas(2000, 1940)
    title_block(d, 1000, 16, "SQLite 数据库 ER 关系图（12 张表）",
                "按 账号 / 支付 / 预约 / 布局与停车 / 审计 分组；PK 主键、FK 外键、UQ 唯一")
    d.line([(50, 116), (1950, 116)], fill=GRID, width=2)

    group(d, (50, 130, 1950, 424), "账号组", "登录鉴权、扫码绑定与站点票据", C_BLUE)
    table(d, (60, 182, 520, 414), "users", "账号与角色",
          [("PK", "username", "TEXT"), ("", "role", "'user'/'admin'/'gate'"),
           ("", "password_hash", "TEXT"), ("", "salt", "TEXT"),
           ("", "failed_attempts", "INTEGER"), ("", "locked_until_ms", "INTEGER")],
          C_BLUE, C_BLUE_F)
    table(d, (600, 182, 1060, 414), "auth_tokens", "登录令牌",
          [("PK", "token_hash", "TEXT"), ("FK", "username", "TEXT"),
           ("", "role", "TEXT"), ("", "expires_at_ms", "INTEGER"),
           ("", "revoked", "INTEGER")],
          C_BLUE, C_BLUE_F)
    table(d, (1140, 182, 1540, 414), "user_plates", "用户车牌绑定",
          [("PK", "username", "TEXT"), ("PK", "plate", "TEXT"),
           ("FK", "site_token", "TEXT"), ("", "created_at_ms", "INTEGER")],
          C_BLUE, C_BLUE_F)
    table(d, (1620, 182, 1940, 414), "site_tickets", "站点扫码票据",
          [("PK", "token", "TEXT"), ("", "site_name", "TEXT"),
           ("", "expires_at_ms", "INTEGER"), ("", "revoked", "INTEGER")],
          C_BLUE, C_BLUE_F)
    rel(d, [(522, 298), (598, 298)], "username")
    rel(d, [(1542, 298), (1618, 298)], "site_token")

    group(d, (50, 484, 1950, 784), "支付组", "网关支付流水与核心定金流水两层记账", C_ORANGE)
    table(d, (60, 544, 940, 769), "deposit_payments", "预约定金流水",
          [("PK", "id", "TEXT"), ("FK", "reservation_id", "TEXT"),
           ("", "plate_number", "TEXT"), ("", "kind", "Charge/Refund/Forfeit/Apply"),
           ("", "amount", "REAL"), ("", "status", "INTEGER"),
           ("", "created_at_ms", "INTEGER")],
          C_ORANGE, C_ORANGE_F, cols=2,
          note="创建预约即收定金；取消→Refunded 爽约→Forfeited 离场→Applied")
    table(d, (1060, 544, 1940, 769), "payment_orders", "支付订单（网关流水）",
          [("PK", "order_id", "TEXT"), ("UQ", "out_trade_no", "TEXT"),
           ("FK", "username", "TEXT"), ("FK", "reservation_id", "TEXT"),
           ("", "kind", "'deposit'/'parking_fee'"), ("", "plate", "TEXT"),
           ("", "amount", "REAL"), ("", "status", "5 态，见正文"),
           ("", "expire_at_ms", "INTEGER"), ("", "paid_at_ms", "INTEGER")],
          C_ORANGE, C_ORANGE_F, cols=2,
          note="网页端下单；confirm 在同一请求内完成结算离场")
    rel(d, [(400, 416), (400, 452), (1200, 452), (1200, 542)], "users.username")
    rel(d, [(500, 771), (500, 812), (1500, 812), (1500, 902)], "reservation_id")
    rel(d, [(1600, 771), (1600, 902)], "reservation_id")

    group(d, (50, 844, 1950, 1144), "预约组", "时段预约与兼容保留的预约记录", C_GREEN)
    table(d, (60, 904, 940, 1129), "bookings", "预约记录（兼容保留）",
          [("PK", "id", "TEXT"), ("FK", "spot_id", "TEXT"),
           ("", "plate_number", "TEXT"), ("", "created_at_ms", "INTEGER"),
           ("", "arrival_time_ms", "INTEGER"), ("", "arrival_deadline_ms", "INTEGER"),
           ("", "status", "INTEGER"), ("", "deposit", "REAL")],
          C_GREEN, C_GREEN_F, cols=2,
          note="早期预约表，当前业务已由 reservations 承接")
    table(d, (1060, 904, 1940, 1129), "reservations", "时段预约（当前主用）",
          [("PK", "id", "TEXT"), ("FK", "spot_id", "TEXT"),
           ("", "plate_number", "TEXT"), ("", "vehicle_type", "INTEGER"),
           ("", "start_time_ms", "INTEGER"), ("", "end_time_ms", "INTEGER"),
           ("", "grace_deadline_ms", "INTEGER"), ("", "deposit", "REAL"),
           ("", "status", "7 态，见正文"), ("", "deposit_state", "Pending/Refunded/…")],
          C_GREEN, C_GREEN_F, cols=2)

    group(d, (50, 1204, 1950, 1564), "布局与停车组", "布局快照、车位与停车记录", C_PURPLE)
    table(d, (60, 1264, 560, 1539), "layout_snapshot", "布局签名快照",
          [("PK", "name", "TEXT"), ("", "value", "TEXT")],
          C_PURPLE, C_PURPLE_F,
          note="启动时比对布局文件签名与快照，不一致则拒绝启动并提示")
    table(d, (640, 1264, 1260, 1539), "parking_spots", "车位",
          [("PK", "identifier", "TEXT"), ("", "zone", "TEXT"),
           ("", "type", "INTEGER"), ("", "status", "INTEGER"),
           ("", "plate_number", "TEXT"), ("", "reserved_until_ms", "INTEGER"),
           ("", "row_index / column_index", "INTEGER"),
           ("", "x / y / width / height", "REAL")],
          C_PURPLE, C_PURPLE_F, cols=2,
          note="同车牌占用时写入 plate_number，预约锁位用 reserved_until_ms")
    table(d, (1340, 1264, 1940, 1539), "parking_records", "停车记录",
          [("PK", "id", "INTEGER"), ("FK", "spot_id", "TEXT"),
           ("", "plate_number", "TEXT"), ("", "entry_time_ms", "INTEGER"),
           ("", "exit_time_ms", "INTEGER NULL"), ("", "vehicle_type", "INTEGER"),
           ("", "fee", "REAL")],
          C_PURPLE, C_PURPLE_F, cols=2,
          note="唯一部分索引：同一车牌仅一条 exit_time_ms IS NULL 记录")
    rel(d, [(562, 1400), (638, 1400)], "布局签名", color=C_PURPLE, dashed=True)
    rel(d, [(1262, 1400), (1338, 1400)], "spot_id", color=C_PURPLE)
    rel(d, [(700, 1131), (700, 1178), (900, 1178), (900, 1262)], "spot_id", color=C_GREEN, below=True)
    rel(d, [(1760, 1131), (1760, 1178), (1180, 1178), (1180, 1262)], "spot_id", color=C_GREEN, below=True)
    ltext(d, 70, 1546,
          "注：parking_records / reservations / payment_orders 的 plate_number 与 user_plates.plate 为业务键关联（未建库级外键）。",
          F(15), MUTED)

    group(d, (50, 1624, 1950, 1904), "审计组", "只追加的哈希链审计", C_TEAL)
    table(d, (60, 1684, 1080, 1884), "audit_logs", "审计日志",
          [("PK", "id", "INTEGER"), ("", "ts_ms", "INTEGER"),
           ("", "actor", "TEXT"), ("", "action", "TEXT"),
           ("", "detail", "TEXT"), ("", "prev_hash", "TEXT"),
           ("", "hash", "SHA-256")],
          C_TEAL, C_TEAL_F, cols=2)
    rrect(d, (1140, 1684, 1940, 1884), WHITE, GRID, width=2, radius=14)
    ltext(d, 1162, 1698, "关系与哈希链说明", F(21), (71, 85, 105))
    notes = [
        "· audit_logs 独立追加：hash = SHA-256(prev_hash|ts|actor|action|detail)",
        "· 布局与停车：layout_snapshot 与 parking_spots 通过布局签名校验对应",
        "· 预约 → 支付：reservations.id 被 deposit_payments / payment_orders 引用",
        "· 账号 → 支付：users.username 被 payment_orders 引用（退款流水记 system）",
        "· unique：同一车牌仅一条未离场停车记录、一个未结束预约",
    ]
    y = 1738
    for ln in notes:
        ltext(d, 1162, y, ln, F(15), INK2)
        y += 27

    save(img, path)


if __name__ == "__main__":
    fig4(sys.argv[1] if len(sys.argv) > 1 else "fig-db-er.png")
