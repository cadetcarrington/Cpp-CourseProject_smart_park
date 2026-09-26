#pragma once
#include <QColor>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <utility>
#include <vector>

// 管理端配色与公共样式。浅色主题：白/浅灰底，低饱和蓝做主色，
// 只用于顶栏、主按钮、链接与选中态；语义色 绿=正常、红=告警。
// 样式表里的 @token@ 由 applyPalette() 统一替换，避免占位符顺序问题。
namespace theme{

inline constexpr const char *Primary      = "#1E5AA8"; // 主蓝：主按钮、选中态、链接
inline constexpr const char *PrimaryDark  = "#174A8C"; // hover
inline constexpr const char *PrimarySoft  = "#E8F0FA"; // 选中底色
inline constexpr const char *PrimaryFaint = "#F2F6FB"; // hover 底色
inline constexpr const char *Window0      = "#F5F6F7";
inline constexpr const char *Window1      = "#F0F2F4";
inline constexpr const char *Surface      = "#FFFFFF";
inline constexpr const char *SurfaceAlt   = "#FAFBFC";
inline constexpr const char *Border       = "#E3E6E9";
inline constexpr const char *InputBorder  = "#C9CED4";
inline constexpr const char *GridLine     = "#EBEEF1";
inline constexpr const char *Text1        = "#1F2329";
inline constexpr const char *Text2        = "#41474E";
inline constexpr const char *Text3        = "#6B7280";
inline constexpr const char *Text4        = "#9AA1A9";
inline constexpr const char *Danger       = "#B42318";
inline constexpr const char *DangerBorder = "#D92D20";
inline constexpr const char *SuccessBg    = "#E6F4EA";
inline constexpr const char *SuccessFg    = "#218838";
inline constexpr const char *SuccessBorder= "#C6E5CF";
inline const QColor chartPrimary(){ return QColor(46, 109, 180); }

inline QString applyPalette(QString text){
    const std::vector<std::pair<const char *, const char *>> palette ={
        {"@primary@", Primary},
        {"@primaryDark@", PrimaryDark},
        {"@primarySoft@", PrimarySoft},
        {"@primaryFaint@", PrimaryFaint},
        {"@window@", Window0},
        {"@content@", Window1},
        {"@surface@", Surface},
        {"@surfaceAlt@", SurfaceAlt},
        {"@border@", Border},
        {"@inputBorder@", InputBorder},
        {"@gridLine@", GridLine},
        {"@text1@", Text1},
        {"@text2@", Text2},
        {"@text3@", Text3},
        {"@text4@", Text4},
        {"@danger@", Danger},
        {"@dangerBorder@", DangerBorder},
        {"@successBg@", SuccessBg},
        {"@successFg@", SuccessFg},
        {"@successBorder@", SuccessBorder},
        {"@authBg@", Window0},
    };
    for (const auto &entry : palette){
        text.replace(QLatin1String(entry.first), QLatin1String(entry.second));
    }
    return text;
}

// 主窗口样式表（纯色版，SMARTPARK_NO_GLASS=1 回退用）。
inline QString solidMainWindowStyleSheet(){
    return applyPalette(QStringLiteral(R"(
        QMainWindow { background: @window@; color: @text1@; }
        QWidget#adminShell, QWidget#contentArea { background: @content@; }
        QFrame#sideBar {
            background: @surface@; border-right: 1px solid @border@;
            min-width: 206px; max-width: 260px;
        }
        QLabel#brandMark {
            color: #FFFFFF; background: @primary@; border-radius: 4px;
            font-size: 13pt; font-weight: 700;
        }
        QLabel#brandName { color: @text1@; font-size: 15pt; font-weight: 700; }
        QLabel#brandCaption, QLabel#sideBarCaption { color: @text3@; font-size: 9pt; }
        QLabel#sideSection { color: @text4@; font-size: 8pt; font-weight: 700; }
        QListWidget#sideNavigation {
            background: transparent; border: 0; outline: none; color: @text2@;
            padding: 4px 8px;
        }
        QListWidget#sideNavigation::item {
            border-radius: 4px; min-height: 32px; padding: 5px 11px; margin: 1px 0;
        }
        QListWidget#sideNavigation::item:hover { background: @primaryFaint@; color: @text1@; }
        QListWidget#sideNavigation::item:selected { background: @primarySoft@; color: @primary@; font-weight: 700; }
        QFrame#topHeader { background: @surface@; border-bottom: 1px solid @border@; }
        QLabel#pageTitle { color: @text1@; font-size: 16pt; font-weight: 700; }
        QLabel#pageSubtitle, QLabel#mutedText { color: @text3@; font-size: 10pt; }
        QLabel#connectionBadge {
            background: @successBg@; color: @successFg@; border: 1px solid @successBorder@;
            border-radius: 3px; padding: 3px 8px; font-size: 9pt; font-weight: 600;
        }
        QLabel#userBadge { color: @text2@; font-size: 10pt; font-weight: 600; }
        QLabel#emergencyBanner {
            background: @danger@; color: #FFFFFF; border-radius: 4px;
            padding: 10px; font-size: 11pt; font-weight: 700;
        }
        QFrame#contentCard {
            background: @surface@; border: 1px solid @border@; border-radius: 6px;
        }
        QFrame#metricCard {
            background: @surface@; border: 1px solid @border@; border-radius: 6px;
            min-height: 96px;
        }
        QLabel#metricTitle { color: @text3@; font-size: 10pt; font-weight: 600; }
        QLabel#metricValue { color: @text1@; font-size: 21pt; font-weight: 700; }
        QLabel#metricHint { color: @text4@; font-size: 9pt; }
        QLabel#cardTitle { color: @text1@; font-size: 12pt; font-weight: 700; }
        QLabel#mapLegend { color: @text3@; font-size: 9pt; }
        QLabel#mapSummary { color: @text2@; font-size: 10pt; font-weight: 600; }
        QLabel#sectionHint { color: @text3@; font-size: 10pt; }
        QLabel#statusInfo { color: @text2@; font-size: 10pt; }
        QLineEdit, QComboBox, QDateTimeEdit {
            background: @surface@; border: 1px solid @inputBorder@; border-radius: 4px;
            min-height: 28px; padding: 0 8px; color: @text1@;
        }
        QLineEdit:focus, QComboBox:focus, QDateTimeEdit:focus {
            border: 2px solid @primary@; padding: 0 7px;
        }
        QPushButton {
            color: @text2@; background: @surface@; border: 1px solid @inputBorder@;
            border-radius: 4px; min-height: 30px; padding: 0 12px; font-weight: 600;
        }
        QPushButton:hover { background: @primaryFaint@; border-color: @primary@; }
        QPushButton:focus { border: 1px solid @primary@; }
        QPushButton[variant="primary"] { color: #FFFFFF; background: @primary@; border-color: @primary@; }
        QPushButton[variant="primary"]:hover { background: @primaryDark@; border-color: @primaryDark@; }
        QPushButton[variant="primary"]:focus { border: 1px solid @text1@; }
        QPushButton[variant="danger"] { color: @danger@; border-color: @dangerBorder@; }
        QPushButton[variant="danger"]:hover { color: #FFFFFF; background: @danger@; border-color: @danger@; }
        QPushButton[variant="quiet"] { color: @text3@; background: transparent; border-color: transparent; }
        QPushButton[variant="quiet"]:hover { background: @primaryFaint@; border-color: @primaryFaint@; color: @text1@; }
        QTableWidget {
            background: @surface@; alternate-background-color: @surfaceAlt@;
            selection-background-color: @primarySoft@; selection-color: @text1@;
            border: 1px solid @border@; border-radius: 4px; gridline-color: @gridLine@;
        }
        QHeaderView::section {
            background: @window@; color: @text2@; border: 0; border-bottom: 1px solid @border@;
            padding: 7px 8px; font-weight: 700;
        }
        QTableWidget::item { padding: 5px 6px; }
        QGraphicsView { background: @surfaceAlt@; border: 1px solid @border@; border-radius: 4px; }
        QScrollArea { background: transparent; border: 0; }
        QScrollArea > QWidget > QWidget { background: transparent; }
        QWidget#dashboardBody { background: @content@; }
        QStatusBar { background: @surface@; color: @text3@; border-top: 1px solid @border@; }
        QStatusBar::item { border: 0; }
        QToolBar { background: @surface@; border-bottom: 1px solid @border@; spacing: 5px; padding: 4px 10px; }
        QToolButton { color: @text2@; border-radius: 4px; padding: 5px 9px; }
        QToolButton:hover { background: @primaryFaint@; color: @text1@; }
        QMenuBar { background: @surface@; color: @text2@; border-bottom: 1px solid @border@; }
        QMenuBar::item:selected { background: @primaryFaint@; }
        QMenu { background: @surface@; color: @text1@; border: 1px solid @border@; }
        QMenu::item:selected { background: @primarySoft@; }
    )"));
}


// 主窗口样式表（毛玻璃版）：整窗先绘制高斯模糊光斑背景（MainWindow::paintEvent），
// 侧边栏/顶栏/工具栏/状态栏/卡片均为半透明玻璃材质，光斑从玻璃下透出。
inline QString glassMainWindowStyleSheet(){
    return applyPalette(QStringLiteral(R"(
        QMainWindow { background: transparent; color: @text1@; }
        QWidget#adminShell, QWidget#contentArea { background: transparent; }
        QStackedWidget { background: transparent; }
        QWidget#dashboardBody { background: transparent; }
        QFrame#sideBar {
            background: rgba(255, 255, 255, 150); border-right: 1px solid rgba(255, 255, 255, 140);
            min-width: 206px; max-width: 260px;
        }
        QLabel#brandMark {
            color: #FFFFFF; background: @primary@; border-radius: 4px;
            font-size: 13pt; font-weight: 700;
        }
        QLabel#brandName { color: @text1@; font-size: 15pt; font-weight: 700; }
        QLabel#brandCaption, QLabel#sideBarCaption { color: @text3@; font-size: 9pt; }
        QLabel#sideSection { color: @text4@; font-size: 8pt; font-weight: 700; }
        QListWidget#sideNavigation {
            background: transparent; border: 0; outline: none; color: @text2@;
            padding: 4px 8px;
        }
        QListWidget#sideNavigation::item {
            border-radius: 4px; min-height: 32px; padding: 5px 11px; margin: 1px 0;
        }
        QListWidget#sideNavigation::item:hover { background: rgba(255, 255, 255, 120); color: @text1@; }
        QListWidget#sideNavigation::item:selected { background: @primarySoft@; color: @primary@; font-weight: 700; }
        QFrame#topHeader { background: rgba(255, 255, 255, 165); border-bottom: 1px solid rgba(255, 255, 255, 150); }
        QLabel#pageTitle { color: @text1@; font-size: 16pt; font-weight: 700; }
        QLabel#pageSubtitle, QLabel#mutedText { color: @text3@; font-size: 10pt; }
        QLabel#connectionBadge {
            background: rgba(230, 242, 230, 210); color: @successFg@; border: 1px solid @successBorder@;
            border-radius: 3px; padding: 3px 8px; font-size: 9pt; font-weight: 600;
        }
        QLabel#userBadge { color: @text2@; font-size: 10pt; font-weight: 600; }
        QLabel#emergencyBanner {
            background: rgba(180, 35, 24, 235); color: #FFFFFF; border-radius: 4px;
            padding: 10px; font-size: 11pt; font-weight: 700;
        }
        QFrame#contentCard {
            background: rgba(255, 255, 255, 205); border: 1px solid rgba(255, 255, 255, 170);
            border-radius: 8px;
        }
        QFrame#metricCard {
            background: rgba(255, 255, 255, 185); border: 1px solid rgba(255, 255, 255, 160);
            border-radius: 8px; min-height: 96px;
        }
        QLabel#metricTitle { color: @text3@; font-size: 10pt; font-weight: 600; }
        QLabel#metricValue { color: @text1@; font-size: 21pt; font-weight: 700; }
        QLabel#metricHint { color: @text4@; font-size: 9pt; }
        QLabel#cardTitle { color: @text1@; font-size: 12pt; font-weight: 700; }
        QLabel#mapLegend { color: @text3@; font-size: 9pt; }
        QLabel#mapSummary { color: @text2@; font-size: 10pt; font-weight: 600; }
        QLabel#sectionHint { color: @text3@; font-size: 10pt; }
        QLabel#statusInfo { color: @text2@; font-size: 10pt; }
        QLineEdit, QComboBox, QDateTimeEdit {
            background: rgba(255, 255, 255, 235); border: 1px solid @inputBorder@; border-radius: 4px;
            min-height: 28px; padding: 0 8px; color: @text1@;
        }
        QLineEdit:focus, QComboBox:focus, QDateTimeEdit:focus {
            border: 2px solid @primary@; padding: 0 7px;
        }
        QPushButton {
            color: @text2@; background: rgba(255, 255, 255, 200); border: 1px solid @inputBorder@;
            border-radius: 4px; min-height: 30px; padding: 0 12px; font-weight: 600;
        }
        QPushButton:hover { background: rgba(255, 255, 255, 240); border-color: @primary@; }
        QPushButton:focus { border: 1px solid @primary@; }
        QPushButton[variant="primary"] { color: #FFFFFF; background: @primary@; border-color: @primary@; }
        QPushButton[variant="primary"]:hover { background: @primaryDark@; border-color: @primaryDark@; }
        QPushButton[variant="primary"]:focus { border: 1px solid @text1@; }
        QPushButton[variant="danger"] { color: @danger@; border-color: @dangerBorder@; }
        QPushButton[variant="danger"]:hover { color: #FFFFFF; background: @danger@; border-color: @danger@; }
        QPushButton[variant="quiet"] { color: @text3@; background: transparent; border-color: transparent; }
        QPushButton[variant="quiet"]:hover { background: rgba(255, 255, 255, 140); border-color: transparent; color: @text1@; }
        QTableWidget {
            background: rgba(255, 255, 255, 235); alternate-background-color: @surfaceAlt@;
            selection-background-color: @primarySoft@; selection-color: @text1@;
            border: 1px solid rgba(255, 255, 255, 170); border-radius: 4px; gridline-color: @gridLine@;
        }
        QHeaderView::section {
            background: rgba(245, 246, 247, 220); color: @text2@; border: 0;
            border-bottom: 1px solid @border@; padding: 7px 8px; font-weight: 700;
        }
        QTableWidget::item { padding: 5px 6px; }
        QGraphicsView { background: rgba(250, 251, 252, 225); border: 1px solid rgba(255, 255, 255, 160); border-radius: 4px; }
        QStatusBar { background: rgba(255, 255, 255, 150); color: @text3@; border-top: 1px solid rgba(255, 255, 255, 140); }
        QStatusBar::item { border: 0; }
        QToolBar { background: rgba(255, 255, 255, 165); border-bottom: 1px solid rgba(255, 255, 255, 150); spacing: 5px; padding: 4px 10px; }
        QToolButton { color: @text2@; border-radius: 4px; padding: 5px 9px; }
        QToolButton:hover { background: rgba(255, 255, 255, 180); color: @text1@; }
        QMenuBar { background: rgba(255, 255, 255, 150); color: @text2@; border-bottom: 1px solid rgba(255, 255, 255, 140); }
        QMenuBar::item:selected { background: rgba(255, 255, 255, 160); }
        QMenu { background: rgba(255, 255, 255, 245); color: @text1@; border: 1px solid @border@; }
        QMenu::item:selected { background: @primarySoft@; }
        QScrollArea { background: transparent; border: 0; }
        QScrollArea > QWidget > QWidget { background: transparent; }
    )"));
}

// 登录 / 注册对话框样式表。translucentBackground=true 时窗口背景透明，
// 透出原生毛玻璃；默认铺浅灰底由光斑背景装饰。
inline QString authDialogStyleSheet(bool translucentBackground = false){
    return applyPalette(QStringLiteral(R"(
        LoginDialog, RegisterDialog { background: @authBg@; }
        QLabel#loginBrandMark {
            color: #FFFFFF; background: @primary@; border-radius: 4px;
            font-size: 16pt; font-weight: 700;
        }
        QLabel#loginTitle { color: @text1@; }
        QLabel#loginSubtitle, QLabel#loginInstruction, QLabel#loginFooter,
        QLabel#strengthLabel {
            color: @text3@; font-size: 10pt;
        }
        QFrame#loginCard {
            background: @surface@; border: 1px solid @border@; border-radius: 6px;
        }
        QLabel#loginCardTitle { color: @text1@; }
        QLineEdit {
            background: @surface@; border: 1px solid @inputBorder@; border-radius: 4px;
            min-height: 30px; padding: 0 9px; color: @text1@;
        }
        QLineEdit:focus { border: 2px solid @primary@; padding: 0 8px; }
        QLineEdit[invalid="true"] { border: 2px solid @danger@; padding: 0 8px; }
        QCheckBox { color: @text3@; }
        QLabel#loginErrorLabel { color: @danger@; font-weight: 600; }
        QPushButton#loginButton {
            color: #FFFFFF; background: @primary@; border: 1px solid @primary@;
            border-radius: 4px; font-weight: 700; padding: 0 14px;
        }
        QPushButton#loginButton:hover { background: @primaryDark@; }
        QPushButton#loginButton:disabled { background: #E4E7EB; color: @text4@; border-color: transparent; }
        QPushButton#registerLink, QPushButton#backToLoginLink {
            color: @primary@; background: transparent; border: 0; font-weight: 600;
            min-height: 26px; padding: 2px 6px;
        }
        QPushButton#registerLink:hover, QPushButton#backToLoginLink:hover {
            color: @primaryDark@; text-decoration: underline;
        }
        QLabel#demoAccountHint {
            color: @text3@; background: @primaryFaint@; border: 1px solid @border@;
            border-radius: 4px; padding: 9px; font-size: 9pt;
        }
    )"));
}

// 非 macOS 平台（或关闭原生毛玻璃）时的认证页背景：浅灰底上低饱和
// 蓝灰色光斑，经 QGraphicsBlurEffect 高斯模糊后铺底。
inline QPixmap auroraBackdrop(const QSize &size){
    QPixmap source(240, 160);
    source.fill(Qt::transparent);
    {
        QPainter painter(&source);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(30, 90, 168, 55));
        painter.drawEllipse(QRectF(120, -50, 190, 150));
        painter.setBrush(QColor(84, 110, 122, 60));
        painter.drawEllipse(QRectF(-60, 80, 170, 140));
        painter.setBrush(QColor(96, 116, 148, 45));
        painter.drawEllipse(QRectF(150, 90, 140, 110));
    }

    QGraphicsScene scene;
    QGraphicsPixmapItem *item = scene.addPixmap(source);
    auto *blur = new QGraphicsBlurEffect;
    blur->setBlurRadius(28);
    item->setGraphicsEffect(blur);

    QPixmap blurred(size);
    blurred.fill(QColor(Window0));
    QPainter compositor(&blurred);
    compositor.setRenderHint(QPainter::SmoothPixmapTransform);
    scene.render(&compositor, QRectF(blurred.rect()),
                 source.rect().adjusted(-40, -40, 40, 40));
    compositor.end();
    return blurred;
}

} // namespace theme
