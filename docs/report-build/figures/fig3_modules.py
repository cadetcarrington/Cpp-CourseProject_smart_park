#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图 3：功能模块图。"""
import sys
sys.path.insert(0, "/Users/Zhuanz/Documents/ChatGPT/c++课设/smartpark/docs/report-build/figures")
from _figlib import *  # noqa


def mod(d, box, name, desc, fill, border, tcolor=None):
    card(d, box, name, [desc], fill=fill, border=border, tsize=21, lsize=16,
         tcolor=tcolor or border, gap=3, center=False, pad=16)


def col(d, box, caption, sub, modules, fill, border, tcolor=None):
    container(d, box, caption, sub, fill=(250, 250, 252))
    x0, x1 = box[0] + 20, box[2] - 20
    y = box[1] + 58
    for name, desc in modules:
        mod(d, (x0, y, x1, y + 78), name, desc, fill, border, tcolor)
        y += 88


def fig3(path):
    img, d = canvas(2000, 1600)
    title_block(d, 1000, 16, "SmartPark 功能模块图",
                "三端功能模块与核心公共模块（按页面与实现文件划分）")
    d.line([(50, 116), (1950, 116)], fill=GRID, width=2)

    macos = [
        ("登录 / 注册窗口", "账号密码校验，本地或远程登录"),
        ("主窗口 + 侧边栏", "7 个功能页切换、状态栏与定时刷新"),
        ("仪表盘", "余位 / 占用率 / 收入 / 6 小时预测"),
        ("停车地图", "Canvas 绘制车位、分区与 A* 路线预览"),
        ("车辆作业", "入场 / 离场 / 识牌 / 应急入场"),
        ("车位占用", "车位状态、车型更正、释放"),
        ("预约管理", "创建 / 核销 / 取消 / 爽约扫描"),
        ("停车记录", "分页查询与记录详情"),
        ("设置", "布局编辑、分配策略、计费规则（远程只读）"),
        ("数据源桥接层", "ParkingBridge / RemoteDataSource"),
    ]
    server = [
        ("TCP 服务 :9527", "长度前缀帧；登录/状态/入场/离场/补报等 action"),
        ("REST 网关 :18080", "34 个端点，Bearer token，统一错误码"),
        ("WebSocket :18081", "事件推送；60 秒无帧踢除"),
        ("静态资源", "--web-root 伺服网页用户端"),
        ("会话与鉴权", "token 哈希入库、角色、登录失败锁定"),
        ("支付订单", "payment_orders：15 分钟超时、幂等确认"),
        ("站点票据", "site_tickets / user_plates 扫码绑定"),
        ("识牌后端", "/api/v1/lpr/recognize 代理 Python 脚本"),
        ("启动与自检", "参数解析、布局签名校验、--selftest"),
        ("EventHub", "事件发布订阅，TCP / WS 双通道广播"),
    ]
    web = [
        ("扫码进入 #/claim", "站点票据校验，绑定车牌"),
        ("登录 / 注册 #/login", "token 保存，接口统一带 Bearer"),
        ("首页 #/home", "余位、分区统计、车位图"),
        ("时段预约 #/reserve", "选择时段与时长，定金 20 元"),
        ("缴费 #/pay", "停车费订单，模拟收银台"),
        ("记录 #/records", "在场与历史停车记录"),
        ("到场指引", "反向寻车步行路线（米 / 转弯数）"),
        ("拍照识牌", "上传照片 → 服务端识牌 → 入场/预约"),
        ("实时推送", "WebSocket 状态灯与事件刷新"),
        ("接入二维码", "生成同局域网访问二维码"),
        ("服务信息", "支付模式、WS 状态、当前账号与退出"),
    ]
    col(d, (50, 140, 650, 1170), "macOS 管理端", "apps/macos · AppKit + Objective-C++",
        macos, C_BLUE_F, C_BLUE)
    col(d, (690, 140, 1290, 1170), "统一服务端", "apps/server + src/network",
        server, C_INDIGO_F, C_INDIGO)
    col(d, (1370, 140, 1950, 1170), "网页用户端", "apps/webclient · 原生 HTML/CSS/JS SPA",
        web, C_ORANGE_F, C_ORANGE)

    # 连接：macOS / 服务端 -> 核心；网页端 -> 服务端
    arrow(d, [(350, 1172), (350, 1228)], color=C_BLUE, width=3)
    rtext(d, 330, 1182, "本地模式：同进程直调核心", F(16), C_BLUE)
    arrow(d, [(990, 1172), (990, 1228)], color=C_INDIGO, width=3)
    rtext(d, 970, 1182, "服务端链接核心库", F(16), C_INDIGO)
    arrow(d, [(1640, 1172), (1640, 1202), (1250, 1202), (1250, 1174)],
          color=C_ORANGE, width=3)
    label(d, 1445, 1206, "HTTP :18080 / WS :18081", size=16, fg=C_ORANGE)

    # 核心公共模块
    container(d, (50, 1230, 1950, 1540), "核心公共模块（src/core）",
              "管理端本地模式与服务端共用；页面与协议之外的全部业务逻辑")
    core = [
        ("领域模型", "7 组模型：车位 / 记录 / 预约 / 布局 / 车辆"),
        ("ParkingService", "业务聚合根：入场/离场/预约/查询/事务编排"),
        ("SpotAllocator + GridPlanner", "加权评分选位 · A* / Dijkstra 路径"),
        ("ReservationService + Billing", "预约规则 · 定金 · 计费规则"),
        ("AnalyticsEngine + Insight", "统计快照 · 6 小时预测 · 拥堵风险"),
        ("ParkingRepository", "QSQLITE 连接、12 张表、事务提交与回滚"),
        ("UserStore + AuditLogService", "账号 / token / 角色 · SHA-256 哈希链"),
        ("FakePaymentGateway", "charge / refund / forfeit 与流水前缀"),
        ("PlateRecognition + EventHub", "识牌调用与看门狗 · 事件总线"),
    ]
    xs = [75, 450, 825, 1200, 1575]
    for i, (t, desc) in enumerate(core):
        x = xs[i % 5]
        y = 1296 if i < 5 else 1410
        card(d, (x, y, x + 350, y + 100), t, [desc], fill=C_PURPLE_F,
             border=C_PURPLE, tsize=21, lsize=15, tcolor=C_PURPLE, gap=3,
             pad=14)

    save(img, path)


if __name__ == "__main__":
    fig3(sys.argv[1] if len(sys.argv) > 1 else "fig-modules.png")
