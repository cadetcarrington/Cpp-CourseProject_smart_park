#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 2：系统主业务流程。"""
import sys
sys.path.insert(0, "/Users/Zhuanz/Documents/ChatGPT/c++课设/smartpark/docs/report-build/figures")
from _figlib import *  # noqa
from _figlib import _wrap


def diamond(d, box, lines, fill=C_YELLOW_F, border=C_YELLOW, tsize=25):
    x0, y0, x1, y1 = box
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    d.polygon([(cx, y0), (x1, cy), (cx, y1), (x0, cy)], fill=fill,
              outline=border, width=3)
    f = F(tsize)
    n = len(lines)
    total = n * (tsize + 6)
    y = cy - total / 2.0
    for ln in lines:
        ctext(d, cx, y, ln, f, border)
        y += tsize + 6


def step(d, box, title, lines, fill=C_INDIGO_F, border=C_INDIGO,
         tsize=24, lsize=18):
    card(d, box, title, lines, fill=fill, border=border, tsize=tsize,
         lsize=lsize, tcolor=border, gap=4)


def panel(d, box, title, lines, tsize=23, lsize=18):
    rrect(d, box, C_SLATE_F, GRID, width=2, radius=16)
    x0, y0 = box[0] + 20, box[1] + 14
    maxw = box[2] - box[0] - 52
    ltext(d, x0, y0, title, F(tsize), (71, 85, 105))
    y = y0 + tsize + 14
    f = F(lsize)
    for ln in lines:
        for seg in _wrap(d, ln, f, maxw - 16):
            d.ellipse((x0 + 4, y + 11, x0 + 11, y + 18), fill=(100, 116, 139))
            ltext(d, x0 + 20, y, seg, f, INK2)
            y += lsize + 9
    need = y - box[1] + 10
    if need > box[3] - box[1]:
        print("WARN panel overflow:", title, "need", need, "have", box[3]-box[1])


def fig2(path):
    img, d = canvas(2000, 2080)
    title_block(d, 1000, 16, "SmartPark 主业务流程",
                "服务端视角：启动 → 入场 → 车位分配 → 在场 → 预约/缴费 → 离场 → 结算（括号内为默认规则）")
    d.line([(50, 116), (1950, 116)], fill=GRID, width=2)

    cx = 1000
    sx0, sx1 = 640, 1360
    # 主流程卡片（y0, y1, 标题, 行）
    steps = [
        (140, 250, "① 启动统一服务端  apps/server/main.cpp",
         ["解析 --port/--http-port/--ws-port/--db/--layout/--web-root",
          "Persistence 打开 SQLite、加载布局并校验签名、播种演示账号",
          "启动 TCP :9527 与 REST :18080（含 WebSocket :18081）"]),
        (294, 404, "② 客户端接入与认证",
         ["管理端 TCP login；网页端 POST /api/v1/auth/login",
          "UserStore 校验（失败 5 次锁 10 分钟）→ 签发 Bearer token（2 小时）"]),
        (448, 558, "③ 入场请求",
         ["TCP parking.enter / REST POST /api/v1/records/enter",
          "道闸：识别车牌（服务端识牌）→ 校验预约与在场记录"]),
        (1006, 1116, "⑤ 建立停车记录并占用车位",
         ["ParkingRepository 事务：INSERT parking_records + UPDATE parking_spots",
          "部分唯一索引保证同一车牌只有一条未离场记录"]),
        (1160, 1270, "⑥ 事件广播与审计",
         ["EventHub.publish → 向 TCP 客户端与 WebSocket 浏览器推送",
          "AuditLogService 追加 SHA-256 哈希链审计记录"]),
        (1314, 1424, "⑦ 在场服务",
         ["余位/分区统计 · 停车地图 · 反向寻车 · 时段预约 · 停车费订单"]),
        (1468, 1578, "⑧ 离场请求",
         ["TCP parking.leave / REST POST /api/v1/records/leave",
          "校验在场记录 → 计算在场时长"]),
        (1622, 1732, "⑨ 离场结算",
         ["Billing 计费：免费 30 分钟，30 分钟单元 × 2.5 元，日封顶 100 元",
          "ReservationService 抵扣/退/没收定金 · 支付订单 confirm → paid"]),
        (1776, 1886, "⑩ 事务提交并完成",
         ["关闭记录 · 释放车位 · 完成预约 · Apply 定金流水（失败整体回滚）",
          "审计追加 + 广播 parking.exited，管理端与网页端实时刷新"]),
    ]
    for y0, y1, t, lines in steps:
        step(d, (sx0, y0, sx1, y1), t, lines)

    # 连线：①→②→③
    arrow(d, [(cx, 250), (cx, 292)])
    arrow(d, [(cx, 404), (cx, 446)])
    arrow(d, [(cx, 558), (cx, 600)])

    # D1：是否有有效预约
    diamond(d, (780, 602, 1220, 752), ["命中有效预约？"])
    l1 = (300, 622, 640, 732)
    r1 = (1420, 622, 1740, 732)
    card(d, l1, "④ 是：核销预约", ["reservation.checkIn", "占用预约车位"],
         fill=WHITE, border=C_GREEN, tsize=23, lsize=18, tcolor=C_GREEN)
    card(d, r1, "④ 否：自动选位", ["SpotAllocator.propose", "GridPlanner 规划路线"],
         fill=WHITE, border=C_INDIGO, tsize=23, lsize=18, tcolor=C_INDIGO)
    arrow(d, [(780, 677), (642, 677)], color=C_GREEN, width=3)
    arrow(d, [(1220, 677), (1418, 677)], color=C_INDIGO, width=3)
    label(d, 748, 626, "是", size=18, fg=C_GREEN)
    label(d, 1252, 626, "否", size=18, fg=C_INDIGO)

    # 汇合后进入 D2
    d.line([(470, 732), (470, 760)], fill=LINE, width=3)
    d.line([(1580, 732), (1580, 760)], fill=LINE, width=3)
    d.line([(470, 760), (1580, 760)], fill=LINE, width=3)
    d.line([(cx, 760), (cx, 810)], fill=LINE, width=3)
    d.ellipse((cx - 5, 755, cx + 5, 765), fill=LINE)
    diamond(d, (780, 812, 1220, 962), ["有空闲车位？"])

    # D2 是 → ⑤；否 → 满场分支
    arrow(d, [(cx, 962), (cx, 1004)])
    label(d, cx + 40, 966, "是", size=18, fg=C_GREEN)
    b2 = (1430, 830, 1950, 1010)
    panel(d, b2, "满场分支（无可用车位）",
          ["无可用车位：返回满场错误，不予放行",
           "应急入场 emergencyEnter：仅管理端本地模式可用",
           "不分配车位，离场时按实际时长补计费"])
    arrow(d, [(1222, 887), (1426, 887)], color=C_RED, width=3, dashed=True)
    label(d, 1332, 852, "否", size=18, fg=C_RED)

    # ⑤ → ⑥ → ⑦ → ⑧ → ⑨ → ⑩
    for y0, y1 in [(1116, 1158), (1270, 1312), (1424, 1466), (1578, 1620),
                   (1732, 1774)]:
        arrow(d, [(cx, y0), (cx, y1)])

    # 右侧分支
    b1 = (1430, 415, 1950, 600)
    panel(d, b1, "断网补报（道闸终端）",
          ["OfflineQueue 追加 JSONL，本地放行不阻塞",
           "重连后 gate.replay 批量补报（1..500 条）",
           "按 plate+kind+原始时间 去重，只确认连续前缀"])
    arrow(d, [(1362, 503), (1426, 503)], color=C_AMBER, width=3, dashed=True)
    label(d, 1394, 472, "补报", size=17, fg=C_AMBER)

    b3 = (1430, 1290, 1950, 1470)
    panel(d, b3, "预约分支（爽约 / 取消）",
          ["到场窗口内 checkIn → CheckedIn",
           "超宽限期未到场 → NoShow，定金 Forfeited",
           "主动取消 → Cancelled，定金 Refunded"])
    arrow(d, [(1362, 1369), (1426, 1369)], color=C_RED, width=3, dashed=True)
    label(d, 1394, 1338, "预约", size=17, fg=C_RED)

    b4 = (1430, 1590, 1950, 1770)
    panel(d, b4, "支付订单（网页端缴费）",
          ["下单即 pending，15 分钟未确认 → expired",
           "confirm 幂等：重复确认直接返回 paid，不二次扣款",
           "出口先离场：其 pending 停车费订单 → cancelled"])
    arrow(d, [(1362, 1677), (1426, 1677)], color=C_ORANGE, width=3, dashed=True)
    label(d, 1394, 1646, "支付", size=17, fg=C_ORANGE)

    # 左侧规则面板
    panel(d, (50, 300, 560, 505), "入场前置校验",
          ["车牌格式与车辆类型校验，未识别车牌拒绝入场",
           "同一车牌仅一条未离场记录（部分唯一索引）",
           "同一车牌仅一个未结束预约（唯一索引约束）",
           "满场判定：remainingSpots() == 0 时拒绝入场"])
    panel(d, (50, 780, 560, 985), "计费规则（BillingRule 默认）",
          ["免费时长 30 分钟，单位 30 分钟向上取整",
           "每单元 2.5 元，日封顶 100 元",
           "示例：30 分钟 0 元 / 31 分钟 2.5 元 / 61 分钟 5 元",
           "规则可配置：freeMinutes / unitMinutes / rate / cap"])
    panel(d, (50, 1100, 560, 1305), "预约规则（ReservationRule 默认）",
          ["最远提前 7 天，最短提前 30 分钟",
           "最短时长 30 分钟，到场宽限 30 分钟",
           "创建即锁位，定金 20 元（创建即收取）",
           "爽约没收定金；主动取消退定金"])
    panel(d, (50, 1420, 560, 1650), "分配与路径算法",
          ["策略：加权评分（路径/拥堵/转弯/占用/分区压力）",
           "或最近车位（Nearest）",
           "路径：A* 与多目标 Dijkstra（距离/成本/转弯数）",
           "栅格 0.5 m、八邻域，转弯惩罚 0.35×cellSize",
           "previewAllocation 只计算不写入，供管理端预览"])


    # 图例
    rrect(d, (50, 1920, 1950, 2050), C_SLATE_F, GRID, width=2, radius=16)
    ltext(d, 74, 1936, "图 例", F(22), (71, 85, 105))
    arrow(d, [(180, 1970), (250, 1970)], color=LINE, width=3)
    ltext(d, 262, 1952, "主流程", F(18), INK2)
    arrow(d, [(370, 1970), (440, 1970)], color=C_AMBER, width=3, dashed=True)
    ltext(d, 452, 1952, "分支 / 旁路", F(18), INK2)
    diamond(d, (560, 1948, 660, 2008), [], tsize=18)
    ltext(d, 674, 1962, "判断", F(18), INK2)
    ltext(d, 780, 1962,
          "注：图为远程模式主流程；管理端本地模式下由 ParkingBridge 同进程直调 ParkingService，路径与规则一致。",
          F(19), INK2)

    save(img, path)


if __name__ == "__main__":
    fig2(sys.argv[1] if len(sys.argv) > 1 else "fig-flow-main.png")
