#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SmartPark 课程设计报告 DOCX 生成脚本。

输入:
  docs/report-build/01-overall.md       Lead 撰写的总纲 + <!--PART:...--> 插入标记
  docs/report-build/parts/02-macos.md   Part A（macos-writer）
  docs/report-build/parts/03-server.md  Part B（server-writer）
  docs/report-build/parts/04-web.md     Part C（web-writer）
  docs/report-build/05-figures.md       Part D（figures-evidence，含 4.2.6 正文）
  docs/report-build/figures/*.png       插图

输出:
  docs/SmartPark课程设计报告-macOS服务端网页部分.docx

不使用模板文件本身，按模板的章节骨架与版式重新生成。
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

from docx import Document
from docx.enum.section import WD_ORIENT
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import (
    WD_ALIGN_PARAGRAPH,
    WD_LINE_SPACING,
    WD_TAB_ALIGNMENT,
    WD_TAB_LEADER,
)
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

BUILD_DIR = Path(__file__).resolve().parent
ROOT = BUILD_DIR.parents[1]
PARTS_DIR = BUILD_DIR / "parts"
FIG_DIR = BUILD_DIR / "figures"
OUT_PATH = ROOT / "docs" / "SmartPark课程设计报告-macOS服务端网页部分.docx"

BODY_FONT_CN = "Arial Unicode MS"
BODY_FONT_EN = "Times New Roman"
HEAD_FONT_CN = "Arial Unicode MS"
CODE_FONT = "Menlo"
TOC_PAGE_NUMBERS_PATH = BUILD_DIR / "toc-pages.json"

FIG_TITLES = {
    "fig-architecture": "系统总体架构",
    "fig-flow-main": "系统主业务流程",
    "fig-modules": "功能模块图",
    "fig-db-er": "数据库表关系",
    "fig-remote-seq": "远程模式时序",
    "fig-payment-flow": "支付订单状态机",
    "fig-web-shot": "网页用户端运行界面（登录、扫码进入与注册）",
    "fig-macos-shot": "macOS 管理端运行界面",
}

# 附录代码图片: (编号标题, 文件, 起始行, 结束行, 说明, Carbon 图片)
# 图片来自对应源码片段，行号为 1-based 闭区间。
APPENDIX_SNIPPETS = [
    (
        "A.1 多目标栅格搜索",
        "src/core/service/GridPlanner.cpp",
        163,
        184,
        "多目标一次搜索的入口：先求每个目标对应的可通行格子，再共用 searchFromGoals 得到搜索结果，"
        "最后按目标分别重建路线；单目标搜索只是目标集合只有一个的特例。",
        "appendix-grid-planner.png",
    ),
    (
        "A.2 车位图坐标变换与绘制",
        "apps/macos/ParkingMapView.mm",
        331,
        352,
        "macOS 管理端停车地图的自定义绘制入口：先按图纸尺寸计算世界坐标到视图坐标的变换，"
        "再以统一的 tx/ty/rectFor 变换绘制背景与图元。",
        "appendix-map-drawing.png",
    ),
    (
        "A.3 支付确认的幂等与离场结算",
        "src/network/RestGateway.cpp",
        1247,
        1274,
        "模拟支付订单的确认流程：先做幂等判断和状态校验，再在同一流程内完成结算离场、"
        "订单状态更新、防重复扣费标记与事件发布。",
        "appendix-payment.png",
    ),
    (
        "A.4 网页收银台的下单与支付确认",
        "apps/webclient/app.js",
        622,
        650,
        "网页用户端模拟收银台：查询在场车辆并生成订单，展示金额、二维码和倒计时，"
        "确认支付后调用订单确认接口并以服务端返回的结算结果展示放行信息。",
        "appendix-web-cashier.png",
    ),
]

INLINE_RE = re.compile(r"(\*\*.+?\*\*|`[^`]+`)")
EAST_ASIAN_RE = re.compile(r"([\u2E80-\u9FFF\uF900-\uFAFF\uFF00-\uFFEF]+)")


# ---------------------------------------------------------------------------
# 基础排版工具
# ---------------------------------------------------------------------------

def set_run_font(run, cn=BODY_FONT_CN, en=BODY_FONT_EN, size=12, bold=None):
    run.font.name = en
    run.font.size = Pt(size)
    if bold is not None:
        run.font.bold = bold
    rpr = run._element.get_or_add_rPr()
    rfonts = rpr.find(qn("w:rFonts"))
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.append(rfonts)
    rfonts.set(qn("w:ascii"), en)
    rfonts.set(qn("w:hAnsi"), en)
    rfonts.set(qn("w:eastAsia"), cn)


def add_script_runs(p, text, *, size=12, cn=BODY_FONT_CN,
                    en=BODY_FONT_EN, bold=None):
    """Split CJK and Latin text so LibreOffice honors the intended CJK font."""
    runs = []
    for piece in EAST_ASIAN_RE.split(text):
        if not piece:
            continue
        run = p.add_run(piece)
        if EAST_ASIAN_RE.fullmatch(piece):
            set_run_font(run, cn, cn, size, bold=bold)
        else:
            set_run_font(run, cn, en, size, bold=bold)
        runs.append(run)
    return runs


def add_rich_runs(p, text, size=12, cn=BODY_FONT_CN, en=BODY_FONT_EN, base_bold=None):
    """支持 **粗体** 与 `行内代码`。"""
    for token in INLINE_RE.split(text):
        if not token:
            continue
        if token.startswith("**") and token.endswith("**") and len(token) > 4:
            add_script_runs(p, token[2:-2], size=size, cn=cn, en=en,
                            bold=True if base_bold is None else base_bold)
        elif token.startswith("`") and token.endswith("`") and len(token) > 2:
            add_script_runs(p, token[1:-1], size=max(size - 1.5, 9),
                            cn=cn, en=CODE_FONT, bold=False)
        else:
            add_script_runs(p, token, size=size, cn=cn, en=en,
                            bold=base_bold)


def set_paragraph_shading(p, fill):
    ppr = p._p.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    ppr.append(shd)


def set_paragraph_border(p, color="BFBFBF", sz=4):
    ppr = p._p.get_or_add_pPr()
    pbdr = OxmlElement("w:pBdr")
    for side in ("top", "left", "bottom", "right"):
        edge = OxmlElement(f"w:{side}")
        edge.set(qn("w:val"), "single")
        edge.set(qn("w:sz"), str(sz))
        edge.set(qn("w:space"), "4")
        edge.set(qn("w:color"), color)
        pbdr.append(edge)
    ppr.append(pbdr)


def set_cell_shading(cell, fill):
    tcpr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    tcpr.append(shd)


def set_cell_width(cell, cm):
    cell.width = Cm(cm)
    tcpr = cell._tc.get_or_add_tcPr()
    tcw = tcpr.find(qn("w:tcW"))
    if tcw is None:
        tcw = OxmlElement("w:tcW")
        tcpr.append(tcw)
    tcw.set(qn("w:w"), str(int(cm * 567)))
    tcw.set(qn("w:type"), "dxa")


def add_page_break_paragraph(doc):
    return doc.add_page_break()


def new_para(doc, text="", size=12, bold=None, align=WD_ALIGN_PARAGRAPH.JUSTIFY,
             first_indent=Pt(24), cn=BODY_FONT_CN, en=BODY_FONT_EN, style=None,
             space_before=0, space_after=0, line_spacing=1.5):
    p = doc.add_paragraph(style=style)
    pf = p.paragraph_format
    pf.alignment = align
    pf.first_line_indent = first_indent
    pf.space_before = Pt(space_before)
    pf.space_after = Pt(space_after)
    if line_spacing is not None:
        pf.line_spacing = line_spacing
    if text:
        add_rich_runs(p, text, size=size, cn=cn, en=en, base_bold=bold)
    return p


# ---------------------------------------------------------------------------
# 文档样式
# ---------------------------------------------------------------------------

def setup_document(doc):
    sec = doc.sections[0]
    sec.orientation = WD_ORIENT.PORTRAIT
    sec.page_width = Cm(21.0)
    sec.page_height = Cm(29.7)
    # 与学院模板保持一致：A4，左侧装订边略宽，正文区比旧稿更舒展。
    sec.top_margin = Cm(2.0)
    sec.bottom_margin = Cm(1.2)
    sec.left_margin = Cm(2.5)
    sec.right_margin = Cm(2.0)
    sec.header_distance = Cm(1.5)
    sec.footer_distance = Cm(0.7)

    # Normal 样式：宋体小四、1.5 倍行距、首行缩进 2 字符、两端对齐
    normal = doc.styles["Normal"]
    normal.font.name = BODY_FONT_EN
    normal.font.size = Pt(12)
    rpr = normal.element.get_or_add_rPr()
    rfonts = rpr.find(qn("w:rFonts"))
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.append(rfonts)
    rfonts.set(qn("w:ascii"), BODY_FONT_EN)
    rfonts.set(qn("w:hAnsi"), BODY_FONT_EN)
    rfonts.set(qn("w:eastAsia"), BODY_FONT_CN)
    normal.paragraph_format.line_spacing = 1.5
    normal.paragraph_format.first_line_indent = Pt(24)
    normal.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
    normal.paragraph_format.space_after = Pt(0)

    specs = {
        1: (18, WD_ALIGN_PARAGRAPH.CENTER, 0, 18, True),
        2: (14, WD_ALIGN_PARAGRAPH.LEFT, 18, 6, False),
        3: (12, WD_ALIGN_PARAGRAPH.LEFT, 12, 6, False),
        4: (12, WD_ALIGN_PARAGRAPH.LEFT, 6, 3, True),
    }
    for level, (size, align, before, after, bold) in specs.items():
        st = doc.styles[f"Heading {level}"]
        st.font.name = BODY_FONT_EN
        st.font.size = Pt(size)
        st.font.bold = bold
        st.font.color.rgb = RGBColor(0, 0, 0)
        rpr = st.element.get_or_add_rPr()
        rfonts = rpr.find(qn("w:rFonts"))
        if rfonts is None:
            rfonts = OxmlElement("w:rFonts")
            rpr.append(rfonts)
        rfonts.set(qn("w:ascii"), BODY_FONT_EN)
        rfonts.set(qn("w:hAnsi"), BODY_FONT_EN)
        rfonts.set(qn("w:eastAsia"), HEAD_FONT_CN)
        pf = st.paragraph_format
        pf.alignment = align
        pf.first_line_indent = Pt(0)
        pf.space_before = Pt(before)
        pf.space_after = Pt(after)
        pf.line_spacing = 1.3
        pf.keep_with_next = True
        if level == 1:
            pf.page_break_before = True

    # 打开文档时提示更新域（目录页码）
    settings = doc.settings.element
    upd = OxmlElement("w:updateFields")
    upd.set(qn("w:val"), "true")
    settings.append(upd)

    # 页脚居中页码。正文目录使用静态页码，打开 Word 后仍可直接打印。
    footer = sec.footer
    fp = footer.paragraphs[0]
    fp.alignment = WD_ALIGN_PARAGRAPH.CENTER
    fp.paragraph_format.first_line_indent = Pt(0)
    run = fp.add_run()
    fld_begin = OxmlElement("w:fldChar")
    fld_begin.set(qn("w:fldCharType"), "begin")
    instr = OxmlElement("w:instrText")
    instr.set(qn("xml:space"), "preserve")
    instr.text = " PAGE "
    fld_sep = OxmlElement("w:fldChar")
    fld_sep.set(qn("w:fldCharType"), "separate")
    cached = OxmlElement("w:t")
    cached.text = "1"
    fld_end = OxmlElement("w:fldChar")
    fld_end.set(qn("w:fldCharType"), "end")
    for el in (fld_begin, instr, fld_sep, cached, fld_end):
        run._r.append(el)
    set_run_font(run, BODY_FONT_CN, BODY_FONT_EN, 9)


# ---------------------------------------------------------------------------
# 前置页面：封面 / 组员 / 任务书 / 目录
# ---------------------------------------------------------------------------

def add_cover(doc):
    for _ in range(3):
        new_para(doc, "", first_indent=Pt(0), align=WD_ALIGN_PARAGRAPH.CENTER)
    p = new_para(doc, "山西大学", size=24, bold=True, align=WD_ALIGN_PARAGRAPH.CENTER,
                 first_indent=Pt(0))
    p = new_para(doc, "计算机与信息技术学院", size=24, bold=True,
                 align=WD_ALIGN_PARAGRAPH.CENTER, first_indent=Pt(0))
    for _ in range(4):
        new_para(doc, "", first_indent=Pt(0), align=WD_ALIGN_PARAGRAPH.CENTER)

    fields = [
        ("项目名称：", "智能停车系统"),
        ("小组编号：", "第 三 组"),
        ("年　　级：", "2025 级"),
        ("指导教师：", "李 玲"),
    ]
    for label, value in fields:
        p = doc.add_paragraph()
        pf = p.paragraph_format
        pf.alignment = WD_ALIGN_PARAGRAPH.CENTER
        pf.first_line_indent = Pt(0)
        pf.space_after = Pt(18)
        pf.line_spacing = 1.5
        add_script_runs(p, label + "    ", size=22, bold=True)
        for run in add_script_runs(p, value, size=22, bold=True):
            run.font.underline = True

    for _ in range(3):
        new_para(doc, "", first_indent=Pt(0), align=WD_ALIGN_PARAGRAPH.CENTER)
    new_para(doc, "2026 年 10 月 19 日", size=16, bold=True,
             align=WD_ALIGN_PARAGRAPH.CENTER, first_indent=Pt(0))


def add_member_page(doc):
    title = new_para(doc, "第 3 小组", size=18, bold=True,
                     align=WD_ALIGN_PARAGRAPH.CENTER, first_indent=Pt(0),
                     space_before=36, space_after=30)
    title.paragraph_format.page_break_before = True
    lines = [
        "组　长： 颜爱辉      学号： 202501004146",
        "组　员： 刘尚东      学号： 202501004126",
        "　　　　 张嵩浦      学号： 202501004152",
    ]
    for line in lines:
        new_para(doc, line, size=16, bold=True, align=WD_ALIGN_PARAGRAPH.CENTER,
                 first_indent=Pt(0), space_after=18)


TASK_GOAL = (
    "以 C++17 完成一个多端协同的智能停车场管理系统，综合运用数据结构与算法、数据库、"
    "计算机网络和软件工程等课程知识，完成需求分析、系统设计、编码实现、测试验证和文档"
    "编写的完整开发过程，并保证系统能够在本机和服务器环境下独立运行与演示。"
)
TASK_ENV = (
    "macOS 15.6（Apple M1，arm64）；Apple clang 17.0.0；CMake 4.4.3、Ninja 1.13.2；"
    "Qt 6.8.3（Core、Sql、Network、HttpServer、WebSockets）；SQLite；Python 3.12 + uv"
    "（YOLO11、PP-OCRv5）；Linux 服务器 10.108.17.55。"
)
TASK_REQ = (
    "实现 macOS 管理端、统一服务端和网页用户端；支持车辆入离场、车位分配与路径规划、"
    "时段预约与定金、停车计费与模拟支付、图片识牌、多端同步与断网补报；服务端提供 "
    "TCP、REST 和 WebSocket 接口并伺服网页静态资源；具备单元测试、集成测试和端到端"
    "测试；按学院模板完成课程设计报告。"
)
TASK_SCHEDULE = [
    ("1", "2026.08.31—2026.09.06", "车库图纸数据建模，栅格地图与路径规划算法原型，命令行验证"),
    ("2", "2026.09.07—2026.09.27", "核心业务（停放、预约、计费）、SQLite 持久化与 TCP 服务端"),
    ("3", "2026.09.28—2026.10.09", "macOS 管理端、网页用户端、图片识牌与服务器部署"),
    ("4", "2026.10.10—2026.10.18", "自动化测试、性能优化、报告编写与答辩准备"),
]


def _cell_text(cell, text, *, size=12, bold=False, align=WD_ALIGN_PARAGRAPH.LEFT):
    cell.text = ""
    p = cell.paragraphs[0]
    pf = p.paragraph_format
    pf.alignment = align
    pf.first_line_indent = Pt(0)
    pf.space_before = Pt(0)
    pf.space_after = Pt(0)
    pf.line_spacing = 1.15
    add_rich_runs(p, text, size=size, base_bold=bold)


def mark_table_header(row):
    """Mark a row as a semantic/repeating table header in Word."""
    tr_pr = row._tr.get_or_add_trPr()
    if tr_pr.find(qn("w:tblHeader")) is None:
        flag = OxmlElement("w:tblHeader")
        flag.set(qn("w:val"), "true")
        tr_pr.append(flag)


def add_picture_with_alt(run, image_path, *, width, alt_text):
    """Insert an inline picture and set accessible title/description metadata."""
    shape = run.add_picture(str(image_path), width=width)
    shape._inline.docPr.set("title", alt_text)
    shape._inline.docPr.set("descr", alt_text)
    return shape


def add_taskbook_page(doc):
    title = new_para(doc, "设计任务书", size=24, bold=True,
                     align=WD_ALIGN_PARAGRAPH.CENTER, first_indent=Pt(0),
                     space_before=54, space_after=18)
    title.paragraph_format.page_break_before = True
    rows = 4 + 1 + 1 + len(TASK_SCHEDULE)
    tbl = doc.add_table(rows=rows, cols=4)
    tbl.style = "Table Grid"
    tbl.alignment = WD_TABLE_ALIGNMENT.CENTER
    tbl.autofit = False
    mark_table_header(tbl.rows[0])
    widths = [2.6, 3.6, 2.4, 6.0]
    for row in tbl.rows:
        for idx, cell in enumerate(row.cells):
            set_cell_width(cell, widths[idx])

    def label(row, text):
        c = tbl.cell(row, 0)
        _cell_text(c, text, bold=True, align=WD_ALIGN_PARAGRAPH.CENTER)
        set_cell_shading(c, "F2F2F2")

    label(0, "课题名称")
    m = tbl.cell(0, 1).merge(tbl.cell(0, 3))
    _cell_text(m, "停车场管理系统", bold=True, align=WD_ALIGN_PARAGRAPH.CENTER)

    for r, lab, txt in [
        (1, "设计目的", TASK_GOAL),
        (2, "实验环境", TASK_ENV),
        (3, "任务要求", TASK_REQ),
    ]:
        label(r, lab)
        mm = tbl.cell(r, 1).merge(tbl.cell(r, 3))
        _cell_text(mm, txt, size=11)
        tbl.rows[r].height = Cm(2.0)

    mm = tbl.cell(4, 0).merge(tbl.cell(4, 3))
    _cell_text(mm, "工作进度计划", bold=True, align=WD_ALIGN_PARAGRAPH.CENTER)
    set_cell_shading(mm, "F2F2F2")

    hdr = ["序号", "起止日期", "工　作　内　容"]
    _cell_text(tbl.cell(5, 0), hdr[0], bold=True, align=WD_ALIGN_PARAGRAPH.CENTER)
    _cell_text(tbl.cell(5, 1), hdr[1], bold=True, align=WD_ALIGN_PARAGRAPH.CENTER)
    hdr3 = tbl.cell(5, 2).merge(tbl.cell(5, 3))
    _cell_text(hdr3, hdr[2], bold=True, align=WD_ALIGN_PARAGRAPH.CENTER)
    mark_table_header(tbl.rows[5])
    for idx, (no, dates, content) in enumerate(TASK_SCHEDULE):
        r = 6 + idx
        _cell_text(tbl.cell(r, 0), no, align=WD_ALIGN_PARAGRAPH.CENTER)
        _cell_text(tbl.cell(r, 1), dates, align=WD_ALIGN_PARAGRAPH.CENTER)
        cc = tbl.cell(r, 2).merge(tbl.cell(r, 3))
        _cell_text(cc, content, size=11)
        tbl.rows[r].height = Cm(1.3)


def add_toc_page(doc, entries):
    title = new_para(doc, "目　录", size=24, bold=True,
                     align=WD_ALIGN_PARAGRAPH.CENTER, first_indent=Pt(0),
                     space_after=18)
    title.paragraph_format.page_break_before = True
    pages = {}
    if TOC_PAGE_NUMBERS_PATH.exists():
        pages = json.loads(TOC_PAGE_NUMBERS_PATH.read_text(encoding="utf-8"))
    for level, title in entries:
        p = doc.add_paragraph()
        pf = p.paragraph_format
        pf.first_line_indent = Pt(0)
        pf.left_indent = Cm({1: 0, 2: 0.45, 3: 0.9}.get(level, 1.2))
        pf.right_indent = Pt(0)
        pf.space_before = Pt(0)
        pf.space_after = Pt(1)
        pf.line_spacing = 1.05
        pf.tab_stops.add_tab_stop(Cm(15.2), WD_TAB_ALIGNMENT.RIGHT,
                                  WD_TAB_LEADER.DOTS)
        # Keep the title, tab leader and page number in one run. LibreOffice can
        # duplicate a tabbed number when the run is split at CJK boundaries.
        run = p.add_run(f"{title}\t{pages.get(title, '')}")
        set_run_font(
            run, BODY_FONT_CN, BODY_FONT_CN,
            11 if level == 1 else 10.5,
            bold=(level == 1),
        )


def add_review_page(doc):
    p = new_para(doc, "指导教师评语：", size=14, first_indent=Pt(0), space_after=6)
    p.paragraph_format.page_break_before = True
    for _ in range(8):
        new_para(doc, "", first_indent=Pt(0))
    new_para(doc, "设计成绩：　　　　　　　　　　　　　　　　　教师签名：",
             size=14, first_indent=Pt(0), space_before=24)
    new_para(doc, "", first_indent=Pt(0))
    new_para(doc, "年　　　月　　　日", size=14, first_indent=Pt(0))


# ---------------------------------------------------------------------------
# Markdown 解析与渲染
# ---------------------------------------------------------------------------

def load_text(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8")


def heading_blocks(lines):
    """返回标题列表，含标题行号、级别、文字、块结束行号。"""
    heads = []
    for i, line in enumerate(lines):
        m = re.match(r"^(#{1,6})\s+(.+?)\s*$", line)
        if m:
            heads.append({"i": i, "level": len(m.group(1)), "title": m.group(2).strip()})
    for k, h in enumerate(heads):
        end = len(lines)
        for later in heads[k + 1:]:
            if later["level"] <= h["level"]:
                end = later["i"]
                break
        h["end"] = end
    return heads


def num_tuple(title):
    m = re.match(r"^(\d+(?:\.\d+)*)", title)
    if not m:
        return None
    return tuple(int(x) for x in m.group(1).split("."))


def extract_sections(md: str, spec: str):
    """spec 形如 2.2.1 或 3.2.1..3.2.2，返回对应小节 markdown 文本。"""
    lines = md.splitlines()
    heads = heading_blocks(lines)
    if ".." in spec:
        start_s, end_s = spec.split("..")
        start_n, end_n = num_tuple(start_s), num_tuple(end_s)
    else:
        start_s = spec
        start_n = end_n = num_tuple(spec)
    if start_n is None:
        return None

    start = None
    for h in heads:
        if h["level"] <= 3 and num_tuple(h["title"]) == start_n:
            start = h
            break
    if start is None:
        return None
    chunks = [lines[start["i"]:start["end"]]]
    last_end = start["end"]
    for h in heads:
        if h["i"] < last_end:
            continue
        nt = num_tuple(h["title"])
        if h["level"] == start["level"] and nt is not None and start_n < nt <= end_n:
            chunks.append(lines[h["i"]:h["end"]])
            last_end = h["end"]
    text = "\n".join(line for chunk in chunks for line in chunk)
    return text.strip("\n")


def part_extract(parts: dict, fname: str, spec: str, warnings: list):
    md = parts.get(fname)
    if not md:
        warnings.append(f"缺少分块文件 parts/{fname}（{spec}）")
        return f"### 【缺失】{fname}：{spec}\n\n（该节内容尚未产出）\n"
    text = extract_sections(md, spec)
    if text is None:
        warnings.append(f"parts/{fname} 中找不到小节 {spec}")
        return f"### 【缺失】{spec}\n\n（该节内容尚未产出）\n"
    return text + "\n"


def assemble(overall: str, parts: dict, figures_md: str, warnings: list) -> str:
    out = []
    for line in overall.splitlines():
        m = re.match(r"^<!--PART:([^:]+):([0-9.]+(?:\.\.[0-9.]+)?)-->$", line.strip())
        if m:
            fname = m.group(1)
            if not fname.endswith(".md"):
                fname += ".md"
            out.append(part_extract(parts, fname, m.group(2), warnings))
            continue
        m = re.match(r"^<!--FIGPART:([0-9.]+(?:\.\.[0-9.]+)?)-->$", line.strip())
        if m:
            out.append(part_extract({"05-figures.md": figures_md}, "05-figures.md",
                                    m.group(1), warnings))
            continue
        if line.strip() == "<!--APPENDIX-->":
            continue
        out.append(line)
    return "\n".join(out)


def is_table_line(line):
    return line.strip().startswith("|") and line.strip().endswith("|")


def table_split(line):
    body = line.strip()
    if body.startswith("|"):
        body = body[1:]
    if body.endswith("|"):
        body = body[:-1]
    return [c.strip() for c in body.split("|")]


def add_md_table(doc, rows):
    if len(rows) < 2:
        return
    header = table_split(rows[0])
    data = [table_split(r) for r in rows[2:]]
    ncols = len(header)
    tbl = doc.add_table(rows=1 + len(data), cols=ncols)
    tbl.style = "Table Grid"
    tbl.alignment = WD_TABLE_ALIGNMENT.CENTER
    tbl.autofit = True
    mark_table_header(tbl.rows[0])
    for c, text in enumerate(header):
        cell = tbl.rows[0].cells[c]
        _cell_text(cell, text, size=10.5, bold=True, align=WD_ALIGN_PARAGRAPH.CENTER)
        set_cell_shading(cell, "D9D9D9")
    for r, row in enumerate(data, start=1):
        for c in range(ncols):
            text = row[c] if c < len(row) else ""
            _cell_text(tbl.rows[r].cells[c], text, size=10.5,
                       align=WD_ALIGN_PARAGRAPH.LEFT)
    # 表后留一点间距
    p = doc.add_paragraph()
    p.paragraph_format.first_line_indent = Pt(0)
    p.paragraph_format.space_after = Pt(6)
    set_run_font(p.add_run(""), size=6)


def add_code_block(doc, lines):
    for line in lines:
        p = doc.add_paragraph()
        pf = p.paragraph_format
        pf.first_line_indent = Pt(0)
        pf.left_indent = Cm(0.3)
        pf.space_before = Pt(0)
        pf.space_after = Pt(0)
        pf.line_spacing = 1.0
        pf.alignment = WD_ALIGN_PARAGRAPH.LEFT
        set_paragraph_shading(p, "F5F5F5")
        add_script_runs(p, line if line else " ", size=9,
                        cn=BODY_FONT_CN, en=CODE_FONT, bold=False)
    p = doc.add_paragraph()
    p.paragraph_format.first_line_indent = Pt(0)
    p.paragraph_format.space_after = Pt(6)


class Renderer:
    def __init__(self, doc, fig_dir: Path, warnings: list):
        self.doc = doc
        self.fig_dir = fig_dir
        self.warnings = warnings
        self.chapter = 0
        self.chapter_name = ""
        self.fig_counter = {}
        self.fig_used = []
        self.sections = []

    def heading(self, level, title):
        m = re.match(r"^第([一二三四五六七八九十]+)章", title)
        if level == 1:
            cn_num = "一二三四五六七八九十"
            self.chapter = cn_num.index(m.group(1)) + 1 if m else 0
            self.chapter_name = title
        if level == 1:
            style_level = 1
        else:
            nt = num_tuple(title)
            style_level = min(len(nt), 4) if nt else 2
            style_level = max(2, min(style_level, 4))
            self.sections.append(title)
        p = self.doc.add_paragraph(style=f"Heading {style_level}")
        p.paragraph_format.left_indent = Pt(0)
        p.paragraph_format.right_indent = Pt(0)
        p.paragraph_format.first_line_indent = Pt(0)
        p.paragraph_format.keep_with_next = True
        p.paragraph_format.keep_together = True
        add_script_runs(
            p, title,
            size={1: 18, 2: 14, 3: 12, 4: 12}[style_level],
            cn=HEAD_FONT_CN, en=BODY_FONT_EN,
            bold=(style_level == 4),
        )
        if style_level == 1:
            self.sections.append(title)

    def figure(self, fig_id):
        title = FIG_TITLES.get(fig_id, fig_id)
        path = None
        for ext in (".png", ".jpg", ".jpeg"):
            cand = self.fig_dir / f"{fig_id}{ext}"
            if cand.exists():
                path = cand
                break
        if self.chapter:
            self.fig_counter[self.chapter] = self.fig_counter.get(self.chapter, 0) + 1
            no = f"{self.chapter}-{self.fig_counter[self.chapter]}"
        else:
            self.fig_counter[0] = self.fig_counter.get(0, 0) + 1
            no = str(self.fig_counter[0])
        p = self.doc.add_paragraph()
        pf = p.paragraph_format
        pf.alignment = WD_ALIGN_PARAGRAPH.CENTER
        pf.first_line_indent = Pt(0)
        pf.space_before = Pt(6)
        pf.space_after = Pt(3)
        pf.line_spacing = 1.0
        if path is not None:
            run = p.add_run()
            add_picture_with_alt(run, path, width=Cm(14.2),
                                 alt_text=f"图{no} {title}")
            self.fig_used.append((no, fig_id, str(path)))
        else:
            self.warnings.append(f"缺图 {fig_id}：{fig_id}.png 不存在")
            add_script_runs(p, f"【缺图：{fig_id}】", size=10.5, bold=True)
        cap = self.doc.add_paragraph()
        cf = cap.paragraph_format
        cf.alignment = WD_ALIGN_PARAGRAPH.CENTER
        cf.first_line_indent = Pt(0)
        cf.space_after = Pt(12)
        cf.line_spacing = 1.0
        add_script_runs(cap, f"图{no}　{title}", size=10.5)

    def body_paragraph(self, text):
        if not text.strip():
            return
        body_align = (WD_ALIGN_PARAGRAPH.LEFT
                      if "`" in text else WD_ALIGN_PARAGRAPH.JUSTIFY)
        if text.strip().startswith("- "):
            p = new_para(self.doc, "\u2022 " + text.strip()[2:], first_indent=Cm(-0.37),
                         align=body_align)
            p.paragraph_format.left_indent = Cm(0.74)
            return
        if re.match(r"^\[\d+\]\s", text.strip()):
            p = new_para(self.doc, text.strip(), first_indent=Cm(-0.74),
                         align=WD_ALIGN_PARAGRAPH.LEFT)
            p.paragraph_format.left_indent = Cm(0.74)
            return
        if re.match(r"^\d+[.、)]\s*", text.strip()):
            p = new_para(self.doc, text.strip(), first_indent=Cm(-0.37),
                         align=body_align)
            p.paragraph_format.left_indent = Cm(0.74)
            return
        if text.strip().startswith(">"):
            text = text.strip().lstrip(">").strip()
        new_para(self.doc, text.strip(), align=body_align)

    def render(self, md: str):
        lines = md.splitlines()
        i = 0
        while i < len(lines):
            line = lines[i]
            stripped = line.strip()

            m = re.match(r"^(#{1,6})\s+(.+?)\s*$", line)
            if m:
                self.heading(len(m.group(1)), m.group(2).strip())
                i += 1
                continue

            if re.match(r"^\[\[FIG:([A-Za-z0-9_-]+)\]\]$", stripped):
                self.figure(re.match(r"^\[\[FIG:([A-Za-z0-9_-]+)\]\]$", stripped).group(1))
                i += 1
                continue

            if stripped.startswith("```"):
                j = i + 1
                code = []
                while j < len(lines) and not lines[j].strip().startswith("```"):
                    code.append(lines[j])
                    j += 1
                add_code_block(self.doc, code)
                i = j + 1
                continue

            if is_table_line(line):
                j = i
                rows = []
                while j < len(lines) and is_table_line(lines[j]):
                    rows.append(lines[j])
                    j += 1
                # 第二行必须是分隔行
                if len(rows) >= 2 and re.match(r"^\|?\s*:?-{2,}", rows[1].strip()):
                    add_md_table(self.doc, rows)
                    i = j
                    continue

            if stripped in ("---", "***", "___"):
                i += 1
                continue

            self.body_paragraph(line)
            i += 1


# ---------------------------------------------------------------------------
# 附录
# ---------------------------------------------------------------------------

def append_appendix(doc, warnings):
    new_para(
        doc,
        "附录选取四段代表性代码，覆盖路径搜索、macOS 车位图、支付结算和网页收银台。"
        "代码图片由 Carbon 生成；内容来自仓库原文件，仅调整换行以便排版。",
    )
    for title, rel, start, end, desc, image_name in APPENDIX_SNIPPETS:
        path = ROOT / rel
        if not path.exists():
            warnings.append(f"附录代码文件不存在：{rel}")
            continue
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        if start < 1 or end > len(lines):
            warnings.append(f"附录代码行号越界：{rel}:{start}-{end}（共 {len(lines)} 行）")
            continue
        note = doc.add_paragraph()
        note.paragraph_format.first_line_indent = Pt(0)
        note.paragraph_format.space_after = Pt(6)
        note.paragraph_format.keep_with_next = True
        add_script_runs(
            note, f"{title}　文件：{rel}，第 {start}–{end} 行。{desc}", size=10.5,
        )
        image_path = FIG_DIR / "code" / image_name
        if not image_path.exists():
            warnings.append(f"附录代码图片不存在：{image_path}")
            continue
        p = doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.first_line_indent = Pt(0)
        p.paragraph_format.space_after = Pt(10)
        add_picture_with_alt(
            p.add_run(), image_path, width=Cm(14.8),
            alt_text=f"{title} 代码截图：{rel} 第 {start}–{end} 行",
        )


# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

def main():
    warnings: list = []
    overall = load_text(BUILD_DIR / "01-overall.md")
    if not overall:
        print("错误：缺少 docs/report-build/01-overall.md", file=sys.stderr)
        return 1
    parts = {
        "02-macos.md": load_text(PARTS_DIR / "02-macos.md"),
        "03-server.md": load_text(PARTS_DIR / "03-server.md"),
        "04-web.md": load_text(PARTS_DIR / "04-web.md"),
    }
    figures_md = load_text(BUILD_DIR / "05-figures.md")

    assembled = assemble(overall, parts, figures_md, warnings)

    toc_entries = []
    for line in assembled.splitlines():
        m = re.match(r"^(#{1,3})\s+(.+?)\s*$", line)
        if m:
            toc_entries.append((len(m.group(1)), m.group(2).strip()))

    doc = Document()
    setup_document(doc)
    doc.core_properties.title = "SmartPark 智能停车场管理系统课程设计报告"
    doc.core_properties.author = "第三组"

    add_cover(doc)
    add_member_page(doc)
    add_taskbook_page(doc)
    add_toc_page(doc, toc_entries)

    renderer = Renderer(doc, FIG_DIR, warnings)
    # 正文：第一章之前补一个分页
    first_heading = True
    for line in assembled.splitlines():
        if first_heading and re.match(r"^#\s+第[一二三四五六七八九十]+章", line):
            break
    # 直接在目录页后分页，交由 Heading1 的 page_break_before 处理

    # 逐块渲染，并在 <!--APPENDIX--> 原位置插入附录
    pre, _, post = assembled.partition("<!--APPENDIX-->")
    renderer.render(pre)
    append_appendix(doc, warnings)
    if post.strip():
        renderer.render(post)

    add_review_page(doc)
    doc.save(OUT_PATH)

    print("=" * 70)
    print(f"已生成：{OUT_PATH}")
    print(f"  段落数：{len(doc.paragraphs)}，表格数：{len(doc.tables)}")
    print(f"  正文小节：{len(renderer.sections)} 个标题")
    print(f"  插图：{len(renderer.fig_used)} 张")
    for no, fid, path in renderer.fig_used:
        print(f"    图{no}  {fid}  {Path(path).name}")
    if warnings:
        print("  警告：")
        for w in warnings:
            print(f"    - {w}")
    print("=" * 70)
    return 0


if __name__ == "__main__":
    sys.exit(main())
