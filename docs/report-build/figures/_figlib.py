#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SmartPark 报告图表绘制公共库（Pillow，无 matplotlib）。"""
import math
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "/System/Library/Fonts/STHeiti Medium.ttc"
_font_cache = {}


def F(size):
    if size not in _font_cache:
        _font_cache[size] = ImageFont.truetype(FONT_PATH, size, index=0)
    return _font_cache[size]


# 调色板
WHITE = (255, 255, 255)
INK = (15, 23, 42)          # 标题
INK2 = (51, 65, 85)         # 正文
MUTED = (100, 116, 139)     # 弱化文字
GRID = (203, 213, 225)      # 容器边框
LINE = (100, 116, 139)      # 连线

C_BLUE_F, C_BLUE = (219, 234, 254), (37, 99, 235)      # macOS 管理端
C_ORANGE_F, C_ORANGE = (255, 237, 213), (234, 88, 12)  # 网页用户端
C_GREEN_F, C_GREEN = (220, 252, 231), (22, 163, 74)    # 道闸终端
C_GRAY_F, C_GRAY = (229, 231, 235), (71, 85, 105)      # 命令行
C_INDIGO_F, C_INDIGO = (224, 231, 255), (79, 70, 229)  # 服务端
C_PURPLE_F, C_PURPLE = (243, 232, 255), (147, 51, 234)  # 核心
C_AMBER_F, C_AMBER = (254, 243, 199), (217, 119, 6)    # 数据
C_TEAL_F, C_TEAL = (204, 251, 241), (13, 148, 136)     # Python
C_YELLOW_F, C_YELLOW = (254, 249, 195), (161, 98, 7)   # 判断
C_RED_F, C_RED = (254, 226, 226), (185, 28, 28)        # 异常/分支
C_SLATE_F = (248, 250, 252)                            # 图层底色


def canvas(w, h):
    img = Image.new("RGB", (w, h), WHITE)
    return img, ImageDraw.Draw(img)


def tw(d, t, f):
    return d.textlength(t, font=f)


def ctext(d, cx, y, t, f, fill=INK):
    d.text((cx - tw(d, t, f) / 2.0, y), t, font=f, fill=fill)


def ltext(d, x, y, t, f, fill=INK):
    d.text((x, y), t, font=f, fill=fill)


def rtext(d, x, y, t, f, fill=INK):
    d.text((x - tw(d, t, f), y), t, font=f, fill=fill)


def rrect(d, box, fill, outline, width=3, radius=16):
    d.rounded_rectangle(box, radius=radius, fill=fill, outline=outline,
                        width=width)


def card(d, box, title, lines=(), fill=WHITE, border=INK2, tsize=25,
         lsize=19, lfill=INK2, pad=12, tcolor=None, gap=7, radius=16,
         width=3, center=True):
    """圆角卡片：标题 + 若干行正文，整体垂直居中。"""
    rrect(d, box, fill, border, width=width, radius=radius)
    x0, y0, x1, y1 = box
    tf, lf = F(tsize), F(lsize)
    title_lines = []
    for seg in title.split("\n"):
        title_lines.extend(_wrap(d, seg, tf, (x1 - x0) - 2 * pad))
    body = []
    for ln in lines:
        body.extend(_wrap(d, ln, lf, (x1 - x0) - 2 * pad))
    title_h = len(title_lines) * (tsize + 4)
    body_h = len(body) * (lsize + gap) if body else 0
    block = title_h + (10 if body else 0) + body_h
    y = y0 + ((y1 - y0) - block) / 2.0
    cx = (x0 + x1) / 2.0
    for ln in title_lines:
        if center:
            ctext(d, cx, y, ln, tf, tcolor or INK)
        else:
            ltext(d, x0 + pad, y, ln, tf, tcolor or INK)
        y += tsize + 4
    y += 10 if body else 0
    for ln in body:
        if center:
            ctext(d, cx, y, ln, lf, lfill)
        else:
            ltext(d, x0 + pad, y, ln, lf, lfill)
        y += lsize + gap


def _wrap(d, text, font, maxw):
    out = []
    for raw in str(text).split("\n"):
        cur = ""
        for ch in raw:
            if cur and tw(d, cur + ch, font) > maxw:
                out.append(cur)
                cur = ch
            else:
                cur += ch
        out.append(cur)
    return out


def label(d, xt, y, text, size=18, fg=(51, 65, 85), bg=WHITE, border=None,
          pad=6):
    """带底色的说明文字，居中于 xt；返回 (x0,y0,x1,y1)。"""
    f = F(size)
    w = tw(d, text, f)
    h = size + 8
    box = (xt - w / 2 - pad, y, xt + w / 2 + pad, y + h)
    if bg is not None:
        d.rounded_rectangle(box, radius=6, fill=bg,
                            outline=border or bg, width=1)
    d.text((xt - w / 2, y + 4), text, font=f, fill=fg)
    return box


def _dashed(d, p, q, color, w, dash=11, gap=7):
    L = math.hypot(q[0] - p[0], q[1] - p[1])
    if L <= 0:
        return
    ux, uy = (q[0] - p[0]) / L, (q[1] - p[1]) / L
    s = 0.0
    while s < L:
        e = min(s + dash, L)
        d.line([(p[0] + ux * s, p[1] + uy * s),
                (p[0] + ux * e, p[1] + uy * e)], fill=color, width=w)
        s += dash + gap


def arrow(d, pts, color=LINE, width=3, head=15, dashed=False):
    for i in range(len(pts) - 1):
        if dashed:
            _dashed(d, pts[i], pts[i + 1], color, width)
        else:
            d.line([pts[i], pts[i + 1]], fill=color, width=width)
    if len(pts) < 2:
        return
    p, q = pts[-2], pts[-1]
    ang = math.atan2(q[1] - p[1], q[0] - p[0])
    hx, hy = math.cos(ang), math.sin(ang)
    left = (q[0] - head * hx + head * 0.52 * hy,
            q[1] - head * hy - head * 0.52 * hx)
    right = (q[0] - head * hx - head * 0.52 * hy,
             q[1] - head * hy + head * 0.52 * hx)
    d.polygon([q, left, right], fill=color)


def arrow_head_only(d, tip, ang, color=LINE, head=15):
    hx, hy = math.cos(ang), math.sin(ang)
    left = (tip[0] - head * hx + head * 0.52 * hy,
            tip[1] - head * hy - head * 0.52 * hx)
    right = (tip[0] - head * hx - head * 0.52 * hy,
             tip[1] - head * hy + head * 0.52 * hx)
    d.polygon([tip, left, right], fill=color)


def container(d, box, caption, caption_sub=None, fill=C_SLATE_F,
              border=GRID, radius=20, csize=23, color=(71, 85, 105)):
    rrect(d, box, fill, border, width=2, radius=radius)
    x0, y0 = box[0], box[1]
    ltext(d, x0 + 22, y0 + 10, caption, F(csize), color)
    if caption_sub:
        f = F(csize - 5)
        ltext(d, x0 + 22 + tw(d, caption, F(csize)) + 14, y0 + 15,
              caption_sub, f, MUTED)


def title_block(d, x, y, title, sub=None, tsize=44, ssize=21):
    ctext(d, x, y, title, F(tsize), INK)
    if sub:
        ctext(d, x, y + tsize + 14, sub, F(ssize), MUTED)


def save(img, path):
    img.save(path, "PNG", optimize=True)
    print("saved", path, img.size)
