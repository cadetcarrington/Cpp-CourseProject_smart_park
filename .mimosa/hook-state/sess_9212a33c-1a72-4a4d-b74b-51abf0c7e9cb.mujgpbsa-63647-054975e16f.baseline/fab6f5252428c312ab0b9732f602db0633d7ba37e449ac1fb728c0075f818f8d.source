#include "MainWindow.h"
#include "Theme.h"

#include "core/service/AnalyticsEngine.h"
#include "core/service/AuditLogService.h"
#include "core/service/RemoteAnalystClient.h"
#include "MacSystemBridge.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QStandardPaths>
#include <QTextEdit>
#include <QTimer>
#include <QBrush>
#include <QComboBox>
#include <QCoreApplication>
#include <QColor>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QFile>
#include <QFont>
#include <QGraphicsTextItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QShowEvent>
#include <QSplitter>
#include <QTableWidget>
#include <QToolBar>
#include <QTransform>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace{
const QString kDefaultLayoutText =
    QString::fromUtf8(smartpark::ParkingLayout::garageDescription());

smartpark::VehicleType vehicleTypeFromIndex(int index){
    switch (index){
    case 1: return smartpark::VehicleType::Motorcycle;
    case 2: return smartpark::VehicleType::Truck;
    case 3: return smartpark::VehicleType::Electric;
    default: return smartpark::VehicleType::Car;
    }
}

const char * vehicleTypeText(smartpark::VehicleType type){
    switch (type){
    case smartpark::VehicleType::Motorcycle:
        return "摩托车";
    case smartpark::VehicleType::Truck:
        return "卡车";
    case smartpark::VehicleType::Electric:
        return "电动车";
    case smartpark::VehicleType::Car:
    default:
        return "轿车";
    }
}

const char *spotTypeText(smartpark::SpotType type){
    switch (type){
    case smartpark::SpotType::Accessible:
        return "无障碍";
    case smartpark::SpotType::Charging:
        return "充电";
    case smartpark::SpotType::Vip:
        return "VIP";
    case smartpark::SpotType::Normal:
    default:
        return "普通";
    }
}

QString statusText(smartpark::SpotStatus status){
    switch (status){
    case smartpark::SpotStatus::Reserved:
        return QStringLiteral("预订");
    case smartpark::SpotStatus::Occupied:
        return QStringLiteral("占用");
    case smartpark::SpotStatus::Disabled:
        return QStringLiteral("停用");
    case smartpark::SpotStatus::Available:
    default:
        return QStringLiteral("空闲");
    }
}

QColor stallFillColor(const smartpark::ParkingSpot &spot){
    switch (spot.status()){
    case smartpark::SpotStatus::Reserved:
        return QColor(181, 71, 8);
    case smartpark::SpotStatus::Occupied:
        return QColor(180, 35, 24);
    case smartpark::SpotStatus::Disabled:
        return QColor(102, 112, 133);
    case smartpark::SpotStatus::Available:
    default:
        break;
    }
    switch (spot.type()){
    case smartpark::SpotType::Accessible:
        return QColor(79, 70, 229);
    case smartpark::SpotType::Charging:
        return QColor(2, 106, 162);
    case smartpark::SpotType::Vip:
        return QColor(124, 58, 237);
    case smartpark::SpotType::Normal:
    default:
        return QColor(15, 118, 110);
    }
}

bool isGarageFloorplan(const smartpark::ParkingLayout &layout){
    return std::abs(layout.siteWidth() - 58.0) < 0.25
        && std::abs(layout.siteHeight() - 42.4) < 0.25;
}

void addWallWithGaps(QGraphicsScene *scene, QPointF start, QPointF end,
                     const std::vector<smartpark::Point> &gates, double gapWidth,
                     const QPen &pen){
    const bool horizontal = std::abs(start.y() - end.y()) < 1e-6;
    const double wallStart = horizontal ? std::min(start.x(), end.x())
                                        : std::min(start.y(), end.y());
    const double wallEnd = horizontal ? std::max(start.x(), end.x())
                                      : std::max(start.y(), end.y());
    const double axis = horizontal ? start.y() : start.x();
    std::vector<std::pair<double, double>> gaps;
    for (const smartpark::Point &gate : gates){
        const double gateAxis = horizontal ? gate.y : gate.x;
        const double gatePos = horizontal ? gate.x : gate.y;
        if (std::abs(gateAxis - axis) > 0.6){
            continue;
        }
        const double from = std::max(wallStart, gatePos - gapWidth / 2.0);
        const double to = std::min(wallEnd, gatePos + gapWidth / 2.0);
        if (to > from + 0.2){
            gaps.push_back({from, to});
        }
    }
    std::sort(gaps.begin(), gaps.end());
    std::vector<std::pair<double, double>> merged;
    for (const auto &gap : gaps){
        if (merged.empty() || gap.first > merged.back().second + 0.05){
            merged.push_back(gap);
        } else{
            merged.back().second = std::max(merged.back().second, gap.second);
        }
    }
    double cursor = wallStart;
    auto addSegment = [&](double from, double to){
        if (to - from < 0.15){
            return;
        }
        if (horizontal){
            scene->addLine(from, axis, to, axis, pen);
        } else{
            scene->addLine(axis, from, axis, to, pen);
        }
    };
    for (const auto &gap : merged){
        addSegment(cursor, gap.first);
        cursor = gap.second;
    }
    addSegment(cursor, wallEnd);
}

QString bookingStatusText(smartpark::BookingStatus status){
    switch (status){
    case smartpark::BookingStatus::CheckedIn:
        return QStringLiteral("已到场");
    case smartpark::BookingStatus::NoShow:
        return QStringLiteral("爽约");
    case smartpark::BookingStatus::Cancelled:
        return QStringLiteral("已取消");
    case smartpark::BookingStatus::Booked:
    default:
        return QStringLiteral("已预约");
    }
}

QString formatTime(smartpark::ParkingRecord::TimePoint time){
    const std::time_t epoch = smartpark::ParkingRecord::Clock::to_time_t(time);
    std::tm *parts = std::localtime(&epoch);
    if (parts == nullptr){
        return QStringLiteral("invalid-time");
    }
    std::ostringstream stream;
    stream << std::put_time(parts, "%Y-%m-%d %H:%M");
    return QString::fromStdString(stream.str());
}

smartpark::ParkingRecord::TimePoint fromDateTime(const QDateTime &dateTime){
    return smartpark::ParkingRecord::TimePoint(
        std::chrono::seconds(dateTime.toSecsSinceEpoch()));
}

QPolygonF routePolygon(const smartpark::Route &route){
    QPolygonF polygon;
    for (const smartpark::Point &point : route.points){
        polygon << QPointF(point.x, point.y);
    }
    return polygon;
}

QColor labelColor(smartpark::SpotStatus status){
    return status == smartpark::SpotStatus::Occupied ? Qt::white : Qt::black;
}

void addFittedText(QGraphicsScene *scene, const QRectF &bounds, const QString &text,
                   const QColor &color, bool allowRotate = true){
    auto *item = scene->addText(text);
    item->document()->setDocumentMargin(0.4);
    item->setDefaultTextColor(color);
    QFont font;
    font.setBold(true);
    item->setFont(font);
    const QRectF textRect = item->boundingRect();
    if (textRect.width() <= 0.0 || textRect.height() <= 0.0){
        return;
    }
    const bool rotate = allowRotate && bounds.height() > bounds.width() * 1.3;
    const double availableWidth = rotate ? bounds.height() : bounds.width();
    const double availableHeight = rotate ? bounds.width() : bounds.height();
    const double scale = std::min(availableWidth / textRect.width(),
                                  availableHeight / textRect.height()) * 0.86;
    QTransform transform;
    transform.translate(bounds.center().x(), bounds.center().y());
    if (rotate){
        transform.rotate(-90.0);
    }
    transform.scale(scale, scale);
    transform.translate(-textRect.center().x(), -textRect.center().y());
    item->setTransform(transform);
}

bool recordOverlapsRange(const smartpark::ParkingRecord &record,
                         const smartpark::ParkingRecord::TimePoint &from,
                         const smartpark::ParkingRecord::TimePoint &to){
    const auto exitTime = record.exitTime().value_or(
        smartpark::ParkingRecord::Clock::now());
    return record.entryTime() <= to && exitTime >= from;
}
} // namespace

MainWindow::MainWindow(QString databasePath, QWidget *parent)
    : MainWindow(std::move(databasePath), QStringLiteral("admin"), parent){
}

MainWindow::MainWindow(QString databasePath, QString currentUser, QWidget *parent)
    : QMainWindow(parent)
    , databasePath_(std::move(databasePath))
    , layoutText_(kDefaultLayoutText)
    , currentUser_(std::move(currentUser)){
    const bool persistenceActive = !databasePath_.isEmpty()
        && applyService(smartpark::ParkingLayout::garageLayout(),
                        smartpark::AllocationStrategy::WeightedCost);
    if (!persistenceActive){
        service_.reset();
        persistence_.reset();
        service_ = std::make_unique<smartpark::ParkingService>(
            smartpark::ParkingLayout::garageLayout());
    }
    buildUi();

    QSettings settings;
    const int savedStrategy = settings.value(QStringLiteral("Operations/strategy"), 0).toInt();
    strategyInput_->setCurrentIndex(savedStrategy == 1 ? 1 : 0);
    updateStrategy();
    if (settings.contains(QStringLiteral("MainWindow/geometry"))){
        restoreGeometry(settings.value(QStringLiteral("MainWindow/geometry")).toByteArray());
    }
    if (settings.contains(QStringLiteral("MainWindow/splitter"))){
        shellSplitter_->restoreState(settings.value(QStringLiteral("MainWindow/splitter")).toByteArray());
    }

    refreshScene();
    refreshBookings();
    refreshRecords();
    refreshOccupancy();
    refreshDashboard();

    const int savedPage = settings.value(QStringLiteral("Navigation/lastPage"), 0).toInt();
    navigation_->setCurrentRow(std::clamp(savedPage, 0, navigation_->count() - 1));
    if (!persistenceActive){
        statusLabel_->setText(
            databaseFailed_
                ? tr("数据库重置失败，已降级为内存模式：重启后数据不会保留。")
                : tr("数据库未启用或恢复被取消，当前为内存模式，重启后数据不会保留。"));
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::paintEvent(QPaintEvent *){
    // 毛玻璃模式的整窗背景：低饱和蓝灰色光斑经高斯模糊后铺满窗口，
    // 侧边栏/顶栏/卡片以半透明材质覆盖其上。按尺寸缓存，仅在尺寸变化时重绘。
    if (!glassMode_){
        return;
    }
    if (glassBackdrop_.isNull() || glassBackdrop_.size() != size()){
        glassBackdrop_ = theme::auroraBackdrop(size());
    }
    QPainter painter(this);
    painter.drawPixmap(0, 0, glassBackdrop_);
}

void MainWindow::buildUi(){
    setWindowTitle(tr("智能停车系统管理员平台"));
    resize(1440, 930);
    setMinimumSize(1080, 720);

    // 主题样式集中在 Theme.h。默认毛玻璃模式：整窗高斯模糊光斑 +
    // 半透明玻璃面板；SMARTPARK_NO_GLASS=1 回退纯色主题。
    glassMode_ = !qEnvironmentVariableIsSet("SMARTPARK_NO_GLASS");
    setStyleSheet(glassMode_ ? theme::glassMainWindowStyleSheet()
                             : theme::solidMainWindowStyleSheet());

    auto *fileMenu = menuBar()->addMenu(tr("文件"));
    auto *logoutAction = fileMenu->addAction(tr("退出登录"));
    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(tr("退出程序"));
    auto *viewMenu = menuBar()->addMenu(tr("查看"));
    auto *fitMapAction = viewMenu->addAction(tr("适应车位图窗口"));

    auto *toolBar = addToolBar(tr("常用操作"));
    toolBar->setObjectName("mainToolBar");
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto *overviewAction = toolBar->addAction(tr("总览"));
    auto *operationsAction = toolBar->addAction(tr("车辆作业"));
    auto *refreshAction = toolBar->addAction(tr("刷新数据"));

    auto *centralWidget = new QWidget(this);
    centralWidget->setObjectName("adminShell");
    auto *shellLayout = new QHBoxLayout(centralWidget);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);

    shellSplitter_ = new QSplitter(Qt::Horizontal, centralWidget);
    shellSplitter_->setChildrenCollapsible(false);

    auto *sideBar = new QFrame(shellSplitter_);
    sideBar->setObjectName("sideBar");
    auto *sideLayout = new QVBoxLayout(sideBar);
    sideLayout->setContentsMargins(16, 20, 16, 16);
    sideLayout->setSpacing(8);

    auto *brandLayout = new QHBoxLayout;
    auto *brandMark = new QLabel(QStringLiteral("SP"), sideBar);
    brandMark->setObjectName("brandMark");
    brandMark->setAlignment(Qt::AlignCenter);
    brandMark->setFixedSize(38, 38);
    auto *brandTextLayout = new QVBoxLayout;
    brandTextLayout->setSpacing(0);
    auto *brandName = new QLabel(tr("SmartPark"), sideBar);
    brandName->setObjectName("brandName");
    auto *brandCaption = new QLabel(tr("运营管理平台"), sideBar);
    brandCaption->setObjectName("brandCaption");
    brandTextLayout->addWidget(brandName);
    brandTextLayout->addWidget(brandCaption);
    brandLayout->addWidget(brandMark);
    brandLayout->addLayout(brandTextLayout, 1);
    sideLayout->addLayout(brandLayout);
    sideLayout->addSpacing(22);

    auto *navCaption = new QLabel(tr("工作台"), sideBar);
    navCaption->setObjectName("sideSection");
    sideLayout->addWidget(navCaption);
    navigation_ = new QListWidget(sideBar);
    navigation_->setObjectName("sideNavigation");
    navigation_->setFrameShape(QFrame::NoFrame);
    navigation_->setSpacing(1);
    navigation_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    const std::vector<std::pair<QString, QString>> pages = {
        {tr("总览"), tr("实时运营概况")},
        {tr("实时车位"), tr("车库建筑图、车位与路线")},
        {tr("车辆作业"), tr("入库、出库与车型更正")},
        {tr("当前车位"), tr("全量车位与现场状态")},
        {tr("预约管理"), tr("预约、到场确认与取消")},
        {tr("停车记录"), tr("记录查询与收费汇总")},
        {tr("设施配置"), tr("停车场布局与计费规则")},
    };
    for (const auto &page : pages){
        auto *item = new QListWidgetItem(page.first, navigation_);
        item->setData(Qt::UserRole, page.first);
        item->setData(Qt::UserRole + 1, page.second);
        item->setToolTip(page.second);
    }
    sideLayout->addWidget(navigation_, 1);
    sideLayout->addSpacing(8);
    auto *sideBarCaption = new QLabel(tr("本地数据模式 · 数据自动持久化"), sideBar);
    sideBarCaption->setObjectName("sideBarCaption");
    sideBarCaption->setWordWrap(true);
    sideLayout->addWidget(sideBarCaption);

    auto *contentArea = new QWidget(shellSplitter_);
    contentArea->setObjectName("contentArea");
    auto *contentLayout = new QVBoxLayout(contentArea);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto *topHeader = new QFrame(contentArea);
    topHeader->setObjectName("topHeader");
    auto *topHeaderLayout = new QHBoxLayout(topHeader);
    topHeaderLayout->setContentsMargins(26, 16, 26, 15);
    topHeaderLayout->setSpacing(12);
    auto *headerText = new QVBoxLayout;
    headerText->setSpacing(3);
    pageTitleLabel_ = new QLabel(topHeader);
    pageTitleLabel_->setObjectName("pageTitle");
    pageSubtitleLabel_ = new QLabel(topHeader);
    pageSubtitleLabel_->setObjectName("pageSubtitle");
    headerText->addWidget(pageTitleLabel_);
    headerText->addWidget(pageSubtitleLabel_);
    topHeaderLayout->addLayout(headerText, 1);
    connectionLabel_ = new QLabel(tr("● 数据已连接"), topHeader);
    connectionLabel_->setObjectName("connectionBadge");
    userLabel_ = new QLabel(tr("管理员：%1").arg(currentUser_), topHeader);
    userLabel_->setObjectName("userBadge");
    auto *logoutButton = new QPushButton(tr("退出登录"), topHeader);
    logoutButton->setProperty("variant", "danger");
    topHeaderLayout->addWidget(connectionLabel_);
    topHeaderLayout->addWidget(userLabel_);
    topHeaderLayout->addWidget(logoutButton);
    contentLayout->addWidget(topHeader);

    pages_ = new QStackedWidget(contentArea);
    pages_->setObjectName("contentPages");
    contentLayout->addWidget(pages_, 1);

    auto createCard = [](QWidget *parent){
        auto *card = new QFrame(parent);
        card->setObjectName("contentCard");
        return card;
    };
    auto createMetricCard = [](QWidget *parent, const QString &title, const QString &hint,
                               QLabel **value){
        auto *card = new QFrame(parent);
        card->setObjectName("metricCard");
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(4);
        auto *titleLabel = new QLabel(title, card);
        titleLabel->setObjectName("metricTitle");
        *value = new QLabel(QStringLiteral("—"), card);
        (*value)->setObjectName("metricValue");
        auto *hintLabel = new QLabel(hint, card);
        hintLabel->setObjectName("metricHint");
        hintLabel->setWordWrap(true);
        layout->addWidget(titleLabel);
        layout->addWidget(*value);
        layout->addWidget(hintLabel);
        return card;
    };

    // 0: 总览
    auto *dashboardPage = new QWidget(pages_);
    auto *dashboardPageLayout = new QVBoxLayout(dashboardPage);
    dashboardPageLayout->setContentsMargins(0, 0, 0, 0);
    auto *dashboardScroll = new QScrollArea(dashboardPage);
    dashboardScroll->setWidgetResizable(true);
    dashboardScroll->setFrameShape(QFrame::NoFrame);
    dashboardScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *dashboardBody = new QWidget;
    dashboardBody->setObjectName("dashboardBody");
    dashboardScroll->setWidget(dashboardBody);
    dashboardPageLayout->addWidget(dashboardScroll);
    auto *dashboardLayout = new QVBoxLayout(dashboardBody);
    dashboardLayout->setContentsMargins(24, 22, 24, 24);
    dashboardLayout->setSpacing(16);
    auto *metricsLayout = new QGridLayout;
    metricsLayout->setHorizontalSpacing(12);
    metricsLayout->setVerticalSpacing(12);
    metricsLayout->addWidget(createMetricCard(dashboardPage, tr("总车位"), tr("当前配置可用车位"), &kpiTotalLabel_), 0, 0);
    metricsLayout->addWidget(createMetricCard(dashboardPage, tr("空闲车位"), tr("可立即安排入场"), &kpiAvailableLabel_), 0, 1);
    metricsLayout->addWidget(createMetricCard(dashboardPage, tr("已占用"), tr("正在停车的车辆"), &kpiOccupiedLabel_), 0, 2);
    metricsLayout->addWidget(createMetricCard(dashboardPage, tr("有效预约"), tr("待到场、保留的车位"), &kpiReservedLabel_), 0, 3);
    for (int column = 0; column < 4; ++column){
        metricsLayout->setColumnStretch(column, 1);
    }
    dashboardLayout->addLayout(metricsLayout);

    auto *dashboardChartsTop = new QHBoxLayout;
    dashboardChartsTop->setSpacing(16);

    auto *compositionCard = createCard(dashboardPage);
    auto *compositionLayout = new QVBoxLayout(compositionCard);
    compositionLayout->setContentsMargins(20, 18, 20, 18);
    compositionLayout->setSpacing(6);
    auto *compositionTitle = new QLabel(tr("车位构成"), compositionCard);
    compositionTitle->setObjectName("cardTitle");
    compositionChart_ = new DonutChartWidget(compositionCard);
    compositionLayout->addWidget(compositionTitle);
    compositionLayout->addWidget(compositionChart_, 1);
    dashboardChartsTop->addWidget(compositionCard, 1);

    auto *typeCard = createCard(dashboardPage);
    auto *typeLayout = new QVBoxLayout(typeCard);
    typeLayout->setContentsMargins(20, 18, 20, 18);
    typeLayout->setSpacing(6);
    auto *typeTitle = new QLabel(tr("各类型车位占比"), typeCard);
    typeTitle->setObjectName("cardTitle");
    typeChart_ = new DonutChartWidget(typeCard);
    typeLayout->addWidget(typeTitle);
    typeLayout->addWidget(typeChart_, 1);
    dashboardChartsTop->addWidget(typeCard, 1);

    auto *forecastCard = createCard(dashboardPage);
    auto *forecastLayout = new QVBoxLayout(forecastCard);
    forecastLayout->setContentsMargins(20, 18, 20, 18);
    forecastLayout->setSpacing(6);
    auto *forecastTitle = new QLabel(tr("预测占用率趋势"), forecastCard);
    forecastTitle->setObjectName("cardTitle");
    forecastChart_ = new LineChartWidget(forecastCard);
    forecastChart_->setUnit(QStringLiteral("%"));
    forecastChart_->setYRange(0.0, 100.0);
    forecastLayout->addWidget(forecastTitle);
    forecastLayout->addWidget(forecastChart_, 1);
    dashboardChartsTop->addWidget(forecastCard, 1);

    dashboardLayout->addLayout(dashboardChartsTop);

    auto *dashboardChartsBottom = new QHBoxLayout;
    dashboardChartsBottom->setSpacing(16);

    auto *flowCard = createCard(dashboardPage);
    auto *flowLayout = new QVBoxLayout(flowCard);
    flowLayout->setContentsMargins(20, 18, 20, 18);
    flowLayout->setSpacing(6);
    auto *flowTitle = new QLabel(tr("近期流量"), flowCard);
    flowTitle->setObjectName("cardTitle");
    flowChart_ = new BarChartWidget(flowCard);
    flowChart_->setUnit(QStringLiteral(" 辆"));
    flowLayout->addWidget(flowTitle);
    flowLayout->addWidget(flowChart_, 1);
    dashboardChartsBottom->addWidget(flowCard, 1);

    auto *revenueCard = createCard(dashboardPage);
    auto *revenueLayout = new QVBoxLayout(revenueCard);
    revenueLayout->setContentsMargins(20, 18, 20, 18);
    revenueLayout->setSpacing(6);
    auto *revenueTitle = new QLabel(tr("近 7 天收入趋势"), revenueCard);
    revenueTitle->setObjectName("cardTitle");
    sevenDayRevenueChart_ = new LineChartWidget(revenueCard);
    sevenDayRevenueChart_->setUnit(QStringLiteral(" 元"));
    revenueLayout->addWidget(revenueTitle);
    revenueLayout->addWidget(sevenDayRevenueChart_, 1);
    dashboardChartsBottom->addWidget(revenueCard, 1);

    auto *flow7Card = createCard(dashboardPage);
    auto *flow7Layout = new QVBoxLayout(flow7Card);
    flow7Layout->setContentsMargins(20, 18, 20, 18);
    flow7Layout->setSpacing(6);
    auto *flow7Title = new QLabel(tr("近 7 天流量趋势"), flow7Card);
    flow7Title->setObjectName("cardTitle");
    sevenDayFlowChart_ = new LineChartWidget(flow7Card);
    sevenDayFlowChart_->setUnit(QStringLiteral(" 辆"));
    flow7Layout->addWidget(flow7Title);
    flow7Layout->addWidget(sevenDayFlowChart_, 1);
    dashboardChartsBottom->addWidget(flow7Card, 1);

    dashboardLayout->addLayout(dashboardChartsBottom);

    auto *dashboardLower = new QHBoxLayout;
    dashboardLower->setSpacing(16);
    auto *quickCard = createCard(dashboardPage);
    auto *quickLayout = new QVBoxLayout(quickCard);
    quickLayout->setContentsMargins(20, 18, 20, 18);
    auto *quickTitle = new QLabel(tr("快捷操作"), quickCard);
    quickTitle->setObjectName("cardTitle");
    auto *quickHint = new QLabel(tr("常用车辆操作入口。"), quickCard);
    quickHint->setObjectName("sectionHint");
    quickHint->setWordWrap(true);
    auto *goOperationsButton = new QPushButton(tr("办理车辆入库 / 出库"), quickCard);
    goOperationsButton->setProperty("variant", "primary");
    auto *goMapButton = new QPushButton(tr("查看实时车位图"), quickCard);
    auto *goBookingsButton = new QPushButton(tr("管理车辆预约"), quickCard);
    auto *analysisReportButton = new QPushButton(tr("数据分析报告"), quickCard);
    quickLayout->addWidget(quickTitle);
    quickLayout->addWidget(quickHint);
    quickLayout->addSpacing(8);
    quickLayout->addWidget(goOperationsButton);
    quickLayout->addWidget(goMapButton);
    quickLayout->addWidget(goBookingsButton);
    quickLayout->addWidget(analysisReportButton);
    quickLayout->addStretch(1);

    auto *activityCard = createCard(dashboardPage);
    auto *activityLayout = new QVBoxLayout(activityCard);
    activityLayout->setContentsMargins(20, 18, 20, 18);
    auto *activityTitle = new QLabel(tr("运营状态"), activityCard);
    activityTitle->setObjectName("cardTitle");
    dashboardActivityLabel_ = new QLabel(activityCard);
    dashboardActivityLabel_->setObjectName("statusInfo");
    dashboardActivityLabel_->setWordWrap(true);
    dashboardInsightLabel_ = new QLabel(activityCard);
    dashboardInsightLabel_->setObjectName("statusInfo");
    dashboardInsightLabel_->setWordWrap(true);
    auto *activityTip = new QLabel(
        tr("提示：可在“当前车位”中选择车辆后直接出库。"),
        activityCard);
    activityTip->setObjectName("sectionHint");
    activityTip->setWordWrap(true);
    activityLayout->addWidget(activityTitle);
    activityLayout->addSpacing(8);
    activityLayout->addWidget(dashboardActivityLabel_);
    activityLayout->addWidget(dashboardInsightLabel_);
    activityLayout->addStretch(1);
    activityLayout->addWidget(activityTip);
    dashboardLower->addWidget(quickCard, 1);
    dashboardLower->addWidget(activityCard, 2);
    dashboardLayout->addLayout(dashboardLower, 1);
    pages_->addWidget(dashboardPage);

    // 1: 实时车位
    auto *mapPage = new QWidget(pages_);
    auto *mapPageLayout = new QVBoxLayout(mapPage);
    mapPageLayout->setContentsMargins(24, 22, 24, 24);
    mapPageLayout->setSpacing(12);
    auto *mapToolbar = new QHBoxLayout;
    mapSummaryLabel_ = new QLabel(mapPage);
    mapSummaryLabel_->setObjectName("mapSummary");
    auto *fitMapButton = new QPushButton(tr("适应窗口"), mapPage);
    mapToolbar->addWidget(mapSummaryLabel_, 1);
    mapToolbar->addWidget(fitMapButton);
    mapPageLayout->addLayout(mapToolbar);
    auto *mapBody = new QHBoxLayout;
    mapBody->setSpacing(12);
    auto *mapCard = createCard(mapPage);
    auto *mapLayout = new QVBoxLayout(mapCard);
    mapLayout->setContentsMargins(16, 16, 16, 14);
    mapLayout->setSpacing(10);
    scene_ = new QGraphicsScene(mapCard);
    mapView_ = new QGraphicsView(scene_, mapCard);
    mapView_->setRenderHint(QPainter::Antialiasing);
    mapView_->setMinimumHeight(500);
    mapView_->setDragMode(QGraphicsView::ScrollHandDrag);
    mapView_->setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    mapView_->setAccessibleName(tr("实时车位建筑图"));
    auto *legend = new QLabel(
        tr("状态：■ 占用  ◐ 预留  ● 空闲  — 停用　|　空闲车位类型：普通（青绿） 无障碍（靛蓝） 充电（蓝） VIP（紫）\n"
           "图纸方向：北在上；蓝线为入场路径，橙色虚线为离场路径。拖拽可查看细节。"), mapCard);
    legend->setObjectName("mapLegend");
    legend->setWordWrap(true);
    mapLayout->addWidget(mapView_, 1);
    mapLayout->addWidget(legend);
    mapBody->addWidget(mapCard, 3);

    auto *zonePanel = createCard(mapPage);
    auto *zonePanelLayout = new QVBoxLayout(zonePanel);
    zonePanelLayout->setContentsMargins(18, 18, 18, 18);
    zonePanelLayout->setSpacing(8);
    auto *zonePanelTitle = new QLabel(tr("分区压力统计"), zonePanel);
    zonePanelTitle->setObjectName("cardTitle");
    zonePressureChart_ = new BarChartWidget(zonePanel);
    zonePressureChart_->setUnit(QStringLiteral("%"));
    zoneInsightLabel_ = new QLabel(zonePanel);
    zoneInsightLabel_->setObjectName("statusInfo");
    zoneInsightLabel_->setWordWrap(true);
    zonePanelLayout->addWidget(zonePanelTitle);
    zonePanelLayout->addWidget(zonePressureChart_, 1);
    zonePanelLayout->addWidget(zoneInsightLabel_);
    mapBody->addWidget(zonePanel, 1);

    mapPageLayout->addLayout(mapBody, 1);
    pages_->addWidget(mapPage);

    // 2: 车辆作业
    auto *operationsPage = new QWidget(pages_);
    auto *operationsLayout = new QHBoxLayout(operationsPage);
    operationsLayout->setContentsMargins(24, 22, 24, 24);
    operationsLayout->setSpacing(16);
    auto *operationCard = createCard(operationsPage);
    auto *operationCardLayout = new QVBoxLayout(operationCard);
    operationCardLayout->setContentsMargins(20, 18, 20, 20);
    operationCardLayout->setSpacing(14);
    auto *operationTitle = new QLabel(tr("车辆入离场"), operationCard);
    operationTitle->setObjectName("cardTitle");
    auto *operationHint = new QLabel(tr("输入车牌与车型后自动分配；出库可输入车牌，或先在“当前车位”选中占用车辆。"), operationCard);
    operationHint->setObjectName("sectionHint");
    operationHint->setWordWrap(true);
    auto *operationForm = new QFormLayout;
    operationForm->setHorizontalSpacing(16);
    operationForm->setVerticalSpacing(12);
    plateInput_ = new QLineEdit(operationCard);
    plateInput_->setPlaceholderText(tr("例如：晋A12345"));
    plateInput_->setObjectName("plateInput");
    plateInput_->setClearButtonEnabled(true);
    vehicleTypeInput_ = new QComboBox(operationCard);
    vehicleTypeInput_->addItems({tr("轿车"), tr("摩托车"), tr("卡车"), tr("电动车")});
    vehicleTypeInput_->setObjectName("vehicleTypeInput");
    strategyInput_ = new QComboBox(operationCard);
    strategyInput_->addItems({tr("加权代价（推荐）"), tr("最近车位")});
    strategyInput_->setObjectName("strategyInput");
    operationForm->addRow(tr("车牌"), plateInput_);
    operationForm->addRow(tr("车辆类型"), vehicleTypeInput_);
    operationForm->addRow(tr("分配策略"), strategyInput_);
    allocateButton_ = new QPushButton(tr("自动分配车位"), operationCard);
    allocateButton_->setObjectName("allocateButton");
    allocateButton_->setProperty("variant", "primary");
    updateVehicleTypeButton_ = new QPushButton(tr("修改车辆类型"), operationCard);
    updateVehicleTypeButton_->setObjectName("updateVehicleTypeButton");
    updateVehicleTypeButton_->setToolTip(tr("更新当前占用或预留车辆的车型，不改变其车位或预约时间。"));
    releaseButton_ = new QPushButton(tr("车辆出库"), operationCard);
    releaseButton_->setObjectName("releaseButton");
    releaseButton_->setProperty("variant", "danger");
    releaseButton_->setToolTip(tr("输入在场车牌，或在当前车位表中选中占用车辆后出库。"));
    emergencyButton_ = new QPushButton(tr("应急生命通道入场"), operationCard);
    emergencyButton_->setObjectName("emergencyButton");
    emergencyButton_->setProperty("variant", "danger");
    emergencyButton_->setToolTip(tr("救护车/消防车优先入场：优先出口最近车位，满场时最近车辆自动结算让位。"));
    emergencyBanner_ = new QLabel(operationCard);
    emergencyBanner_->setObjectName("emergencyBanner");
    emergencyBanner_->setWordWrap(true);
    emergencyBanner_->setVisible(false);
    auto *operationButtons = new QGridLayout;
    operationButtons->setHorizontalSpacing(10);
    operationButtons->setVerticalSpacing(10);
    operationButtons->addWidget(allocateButton_, 0, 0, 1, 2);
    operationButtons->addWidget(updateVehicleTypeButton_, 1, 0);
    operationButtons->addWidget(releaseButton_, 1, 1);
    operationButtons->addWidget(emergencyButton_, 2, 0, 1, 2);
    operationCardLayout->addWidget(emergencyBanner_);
    operationCardLayout->addWidget(operationTitle);
    operationCardLayout->addWidget(operationHint);
    operationCardLayout->addSpacing(6);
    operationCardLayout->addLayout(operationForm);
    operationCardLayout->addLayout(operationButtons);
    operationCardLayout->addStretch(1);

    auto *guideCard = createCard(operationsPage);
    auto *guideLayout = new QVBoxLayout(guideCard);
    guideLayout->setContentsMargins(20, 18, 20, 20);
    auto *guideTitle = new QLabel(tr("作业指引"), guideCard);
    guideTitle->setObjectName("cardTitle");
    auto *guideText = new QLabel(
        tr("1. 入库：填写车牌和类型，选择策略后分配车位。\n\n"
           "2. 更正：当识别车型有误时，保留车位与预约，直接更新车辆类型。\n\n"
           "3. 出库：输入车牌；若从车位列表操作，先选中“占用”行，再点击出库。\n\n"
           "所有操作完成后，底部状态栏会给出结果、费用或失败原因。"), guideCard);
    guideText->setObjectName("sectionHint");
    guideText->setWordWrap(true);
    guideLayout->addWidget(guideTitle);
    guideLayout->addSpacing(10);
    guideLayout->addWidget(guideText);
    recommendationLabel_ = new QLabel(guideCard);
    recommendationLabel_->setObjectName("statusInfo");
    recommendationLabel_->setWordWrap(true);
    recommendationLabel_->setText(
        tr("完成一次入库后，这里会显示推荐车位的分项解释，以及与“最近车位”的 A/B 对比。"));
    guideLayout->addSpacing(10);
    guideLayout->addWidget(recommendationLabel_);
    guideLayout->addStretch(1);
    operationsLayout->addWidget(operationCard, 3);
    operationsLayout->addWidget(guideCard, 2);
    pages_->addWidget(operationsPage);

    // 3: 当前车位
    auto *occupancyPage = new QWidget(pages_);
    auto *occupancyLayout = new QVBoxLayout(occupancyPage);
    occupancyLayout->setContentsMargins(24, 22, 24, 24);
    occupancyLayout->setSpacing(12);
    auto *occupancyCard = createCard(occupancyPage);
    auto *occupancyCardLayout = new QVBoxLayout(occupancyCard);
    occupancyCardLayout->setContentsMargins(18, 18, 18, 18);
    auto *occupancyHint = new QLabel(
        tr("选择一条“占用”记录后，可前往“车辆作业”点击“车辆出库”；车牌和车辆类型随状态实时刷新。"),
        occupancyCard);
    occupancyHint->setObjectName("sectionHint");
    occupancyHint->setWordWrap(true);
    occupancyTable_ = new QTableWidget(occupancyCard);
    occupancyTable_->setObjectName("occupancyTable");
    occupancyTable_->setColumnCount(5);
    occupancyTable_->setHorizontalHeaderLabels({tr("车位"), tr("类型"), tr("状态"), tr("车牌"), tr("车辆类型")});
    occupancyTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    occupancyTable_->verticalHeader()->setVisible(false);
    occupancyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    occupancyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    occupancyTable_->setAlternatingRowColors(true);
    occupancyTable_->setMinimumHeight(440);
    occupancyCardLayout->addWidget(occupancyHint);
    occupancyCardLayout->addWidget(occupancyTable_, 1);
    occupancyLayout->addWidget(occupancyCard, 1);
    pages_->addWidget(occupancyPage);

    // 4: 预约管理
    auto *bookingPage = new QWidget(pages_);
    auto *bookingLayout = new QVBoxLayout(bookingPage);
    bookingLayout->setContentsMargins(24, 22, 24, 24);
    bookingLayout->setSpacing(12);
    auto *bookingFormCard = createCard(bookingPage);
    auto *bookingFormLayout = new QGridLayout(bookingFormCard);
    bookingFormLayout->setContentsMargins(18, 18, 18, 18);
    bookingFormLayout->setHorizontalSpacing(12);
    bookingFormLayout->setVerticalSpacing(9);
    auto *bookingTitle = new QLabel(tr("预约作业"), bookingFormCard);
    bookingTitle->setObjectName("cardTitle");
    bookingPlateInput_ = new QLineEdit(bookingFormCard);
    bookingPlateInput_->setObjectName("bookingPlateInput");
    bookingPlateInput_->setPlaceholderText(tr("预约车牌"));
    bookingPlateInput_->setClearButtonEnabled(true);
    bookingVehicleTypeInput_ = new QComboBox(bookingFormCard);
    bookingVehicleTypeInput_->setObjectName("bookingVehicleTypeInput");
    bookingVehicleTypeInput_->addItems({tr("轿车"), tr("摩托车"), tr("卡车"), tr("电动车")});
    arrivalInput_ = new QDateTimeEdit(bookingFormCard);
    arrivalInput_->setObjectName("arrivalInput");
    arrivalInput_->setDateTime(QDateTime::currentDateTime().addSecs(60 * 60));
    arrivalInput_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    arrivalInput_->setCalendarPopup(true);
    arrivalInput_->setMinimumDateTime(QDateTime::currentDateTime());
    arrivalInput_->setToolTip(tr("预约到场时间：当前时间之后、最多提前 7 天"));
    bookButton_ = new QPushButton(tr("预约车位"), bookingFormCard);
    bookButton_->setObjectName("bookButton");
    bookButton_->setProperty("variant", "primary");
    checkInButton_ = new QPushButton(tr("到场确认"), bookingFormCard);
    checkInButton_->setObjectName("checkInButton");
    cancelBookingButton_ = new QPushButton(tr("取消预约"), bookingFormCard);
    cancelBookingButton_->setObjectName("cancelBookingButton");
    cancelBookingButton_->setProperty("variant", "danger");
    bookingFormLayout->addWidget(bookingTitle, 0, 0, 1, 6);
    bookingFormLayout->addWidget(new QLabel(tr("车牌"), bookingFormCard), 1, 0);
    bookingFormLayout->addWidget(bookingPlateInput_, 1, 1);
    bookingFormLayout->addWidget(new QLabel(tr("车型"), bookingFormCard), 1, 2);
    bookingFormLayout->addWidget(bookingVehicleTypeInput_, 1, 3);
    bookingFormLayout->addWidget(new QLabel(tr("到场时间"), bookingFormCard), 1, 4);
    bookingFormLayout->addWidget(arrivalInput_, 1, 5);
    bookingFormLayout->addWidget(bookButton_, 2, 1);
    bookingFormLayout->addWidget(checkInButton_, 2, 3);
    bookingFormLayout->addWidget(cancelBookingButton_, 2, 5);
    for (int column : {1, 3, 5}){
        bookingFormLayout->setColumnStretch(column, 1);
    }
    bookingLayout->addWidget(bookingFormCard);

    auto *bookingTableCard = createCard(bookingPage);
    auto *bookingTableLayout = new QVBoxLayout(bookingTableCard);
    bookingTableLayout->setContentsMargins(18, 14, 18, 18);
    const smartpark::BookingPolicy bookingPolicy = service_->bookingPolicy();
    auto *bookingPolicyLabel = new QLabel(
        tr("预约规则：定金 %1 元 | 最多提前 %2 天 | 到场宽限期 %3 分钟。")
            .arg(bookingPolicy.deposit, 0, 'f', 2)
            .arg(bookingPolicy.advanceDays)
            .arg(bookingPolicy.gracePeriod.count()),
        bookingTableCard);
    bookingPolicyLabel->setObjectName("sectionHint");
    bookingsTable_ = new QTableWidget(bookingTableCard);
    bookingsTable_->setObjectName("bookingsTable");
    bookingsTable_->setColumnCount(8);
    bookingsTable_->setHorizontalHeaderLabels(
        {tr("编号"), tr("车牌"), tr("车位"), tr("创建时间"), tr("到场时间"), tr("宽限截止"), tr("定金(元)"), tr("状态")});
    bookingsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    bookingsTable_->verticalHeader()->setVisible(false);
    bookingsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bookingsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    bookingsTable_->setAlternatingRowColors(true);
    bookingsTable_->setMinimumHeight(260);
    depositLabel_ = new QLabel(bookingTableCard);
    depositLabel_->setObjectName("statusInfo");
    depositLabel_->setWordWrap(true);
    bookingTableLayout->addWidget(bookingPolicyLabel);
    bookingTableLayout->addWidget(bookingsTable_, 1);
    bookingTableLayout->addWidget(depositLabel_);
    bookingImpactLabel_ = new QLabel(bookingTableCard);
    bookingImpactLabel_->setObjectName("sectionHint");
    bookingImpactLabel_->setWordWrap(true);
    bookingTableLayout->addWidget(bookingImpactLabel_);
    bookingLayout->addWidget(bookingTableCard, 1);
    pages_->addWidget(bookingPage);

    // 5: 停车记录
    auto *recordsPage = new QWidget(pages_);
    auto *recordsLayout = new QVBoxLayout(recordsPage);
    recordsLayout->setContentsMargins(24, 22, 24, 24);
    recordsLayout->setSpacing(12);
    auto *recordsCard = createCard(recordsPage);
    auto *recordsCardLayout = new QVBoxLayout(recordsCard);
    recordsCardLayout->setContentsMargins(18, 18, 18, 18);
    auto *filterLayout = new QHBoxLayout;
    filterLayout->setSpacing(10);
    recordFromInput_ = new QDateTimeEdit(recordsCard);
    recordFromInput_->setObjectName("recordFromInput");
    recordFromInput_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    recordFromInput_->setCalendarPopup(true);
    recordFromInput_->setDateTime(QDateTime::currentDateTime().addDays(-7));
    recordToInput_ = new QDateTimeEdit(recordsCard);
    recordToInput_->setObjectName("recordToInput");
    recordToInput_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    recordToInput_->setCalendarPopup(true);
    recordToInput_->setDateTime(QDateTime::currentDateTime().addDays(1));
    auto *queryButton = new QPushButton(tr("查询"), recordsCard);
    queryButton->setObjectName("queryRecordsButton");
    queryButton->setProperty("variant", "primary");
    auto *resetFilterButton = new QPushButton(tr("显示全部"), recordsCard);
    resetFilterButton->setObjectName("resetRecordFilterButton");
    filterLayout->addWidget(new QLabel(tr("时间范围"), recordsCard));
    filterLayout->addWidget(recordFromInput_);
    filterLayout->addWidget(new QLabel(tr("至"), recordsCard));
    filterLayout->addWidget(recordToInput_);
    filterLayout->addWidget(queryButton);
    filterLayout->addWidget(resetFilterButton);
    filterLayout->addStretch(1);
    recordsTable_ = new QTableWidget(recordsCard);
    recordsTable_->setObjectName("recordsTable");
    recordsTable_->setColumnCount(7);
    recordsTable_->setHorizontalHeaderLabels(
        {tr("车牌"), tr("车位"), tr("入场时间"), tr("离场时间"), tr("时长(分钟)"), tr("费用(元)"), tr("状态")});
    recordsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    recordsTable_->verticalHeader()->setVisible(false);
    recordsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recordsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    recordsTable_->setAlternatingRowColors(true);
    recordsTable_->setMinimumHeight(400);
    recordsLabel_ = new QLabel(recordsCard);
    recordsLabel_->setObjectName("statusInfo");
    recordsLabel_->setWordWrap(true);
    recordsCardLayout->addLayout(filterLayout);
    recordsCardLayout->addWidget(recordsTable_, 1);
    recordsCardLayout->addWidget(recordsLabel_);
    recordsTrendLabel_ = new QLabel(recordsCard);
    recordsTrendLabel_->setObjectName("sectionHint");
    recordsTrendLabel_->setWordWrap(true);
    recordsCardLayout->addWidget(recordsTrendLabel_);
    recordsLayout->addWidget(recordsCard, 1);
    pages_->addWidget(recordsPage);

    // 6: 设施配置
    auto *settingsPage = new QWidget(pages_);
    auto *settingsLayout = new QVBoxLayout(settingsPage);
    settingsLayout->setContentsMargins(24, 22, 24, 24);
    settingsLayout->setSpacing(16);
    auto *layoutCard = createCard(settingsPage);
    auto *layoutCardLayout = new QVBoxLayout(layoutCard);
    layoutCardLayout->setContentsMargins(20, 18, 20, 20);
    auto *layoutTitle = new QLabel(tr("停车场布局"), layoutCard);
    layoutTitle->setObjectName("cardTitle");
    auto *layoutHint = new QLabel(
        tr("编辑 site、entrance、exit、region 与 obstacle 指令后会重建车位数据。建议先备份数据库，并仅在设施调整时使用。"),
        layoutCard);
    layoutHint->setObjectName("sectionHint");
    layoutHint->setWordWrap(true);
    layoutButton_ = new QPushButton(tr("打开自定义布局编辑器"), layoutCard);
    layoutButton_->setObjectName("layoutButton");
    layoutButton_->setProperty("variant", "primary");
    layoutCardLayout->addWidget(layoutTitle);
    layoutCardLayout->addWidget(layoutHint);
    layoutCardLayout->addSpacing(6);
    layoutCardLayout->addWidget(layoutButton_, 0, Qt::AlignLeft);
    settingsLayout->addWidget(layoutCard);
    auto *billingCard = createCard(settingsPage);
    auto *billingCardLayout = new QVBoxLayout(billingCard);
    billingCardLayout->setContentsMargins(20, 18, 20, 20);
    auto *billingTitle = new QLabel(tr("计费规则"), billingCard);
    billingTitle->setObjectName("cardTitle");
    billingLabel_ = new QLabel(billingCard);
    billingLabel_->setObjectName("sectionHint");
    billingLabel_->setWordWrap(true);
    const smartpark::BillingRule rule = service_->billing().rule();
    billingLabel_->setText(
        tr("免费 %1 分钟，之后每 %2 分钟计费一次；首单元 %3 元，后续每单元 %4 元，单次封顶 %5 元。")
            .arg(rule.freeDuration.count())
            .arg(rule.billingUnit.count())
            .arg(rule.minimumFee, 0, 'f', 2)
            .arg(rule.unitFee, 0, 'f', 2)
            .arg(rule.dailyCap, 0, 'f', 2));
    billingCardLayout->addWidget(billingTitle);
    billingCardLayout->addWidget(billingLabel_);
    settingsLayout->addWidget(billingCard);
    settingsLayout->addStretch(1);
    pages_->addWidget(settingsPage);

    shellSplitter_->addWidget(sideBar);
    shellSplitter_->addWidget(contentArea);
    shellSplitter_->setStretchFactor(0, 0);
    shellSplitter_->setStretchFactor(1, 1);
    shellSplitter_->setSizes({225, 1215});
    shellLayout->addWidget(shellSplitter_);
    setCentralWidget(centralWidget);

    auto *status = new QStatusBar(this);
    statusLabel_ = new QLabel(tr("就绪：可从左侧选择业务页面。"), status);
    statusLabel_->setObjectName("statusLabel");
    statusLabel_->setMinimumWidth(420);
    auto *databaseLabel = new QLabel(
        databasePath_.isEmpty() ? tr("数据：内存模式") : tr("数据：SQLite 已连接"), status);
    databaseLabel->setObjectName("mutedText");
    status->addWidget(statusLabel_, 1);
    status->addPermanentWidget(databaseLabel);
    setStatusBar(status);

    connect(navigation_, &QListWidget::currentRowChanged, this, &MainWindow::changePage);
    connect(overviewAction, &QAction::triggered, this, [this]{ navigation_->setCurrentRow(0); });
    connect(operationsAction, &QAction::triggered, this, [this]{ navigation_->setCurrentRow(2); });
    connect(refreshAction, &QAction::triggered, this, [this]{
        refreshScene();
        refreshBookings();
        refreshRecords();
        refreshOccupancy();
        refreshDashboard();
        statusLabel_->setText(tr("数据已刷新。"));
    });
    connect(fitMapAction, &QAction::triggered, this, [this]{
        navigation_->setCurrentRow(1);
        fitMapView();
    });
    connect(fitMapButton, &QPushButton::clicked, this, &MainWindow::fitMapView);
    connect(goOperationsButton, &QPushButton::clicked, this, [this]{ navigation_->setCurrentRow(2); });
    connect(goMapButton, &QPushButton::clicked, this, [this]{ navigation_->setCurrentRow(1); });
    connect(goBookingsButton, &QPushButton::clicked, this, [this]{ navigation_->setCurrentRow(4); });
    connect(analysisReportButton, &QPushButton::clicked, this, [this]{
        if (!service_){
            return;
        }
        smartpark::AnalyticsEngine engine(*service_);
        const auto report = engine.analyze();
        QString text = QString::fromStdString(report.summary) + QStringLiteral("\n\n发现：\n");
        for (const smartpark::AnalysisFinding &finding : report.findings){
            text += QStringLiteral("  · [%1] %2：%3\n")
                        .arg(QString::fromLatin1(smartpark::AnalysisReport::categoryText(finding.category)),
                             QString::fromStdString(finding.title),
                             QString::fromStdString(finding.detail));
        }
        text += QStringLiteral("\n建议：\n");
        int index = 1;
        for (const std::string &recommendation : report.recommendations){
            text += QStringLiteral("  %1. %2\n").arg(index++).arg(
                QString::fromStdString(recommendation));
        }
        text += QStringLiteral("\n占用率预测：");
        for (const auto &point : report.forecast){
            text += QStringLiteral("+%1h %2%  ").arg(point.first).arg(
                QString::number(point.second, 'f', 0));
        }
        QDialog dialog(this);
        dialog.setWindowTitle(tr("数据分析报告（%1）").arg(
            QString::fromStdString(report.model)));
        dialog.resize(560, 520);
        auto *dialogLayout = new QVBoxLayout(&dialog);
        auto *textView = new QTextEdit(&dialog);
        textView->setPlainText(text);
        textView->setReadOnly(true);
        dialogLayout->addWidget(textView);

        // 远程模型分析（预留接口 + macOS NSURLSession 传输）：
        // 设置 SMARTPARK_ANALYST_ENDPOINT 后可请求 LLM 生成自然语言结论。
        const QByteArray endpoint =
            qgetenv("SMARTPARK_ANALYST_ENDPOINT");
        if (!endpoint.isEmpty()){
            auto *remoteButton = new QPushButton(tr("请求远程模型分析"), &dialog);
            remoteButton->setProperty("variant", "primary");
            dialogLayout->addWidget(remoteButton);
            connect(remoteButton, &QPushButton::clicked, &dialog,
                    [this, &textView, &dialog, remoteButton, endpoint]{
                remoteButton->setEnabled(false);
                textView->append(QStringLiteral("\n远程模型分析中（最长 20 秒）…"));
                smartpark::AnalyticsEngine engine(*service_);
                const auto snapshot = engine.snapshot(
                    smartpark::ParkingRecord::Clock::now());
                auto *state = new std::shared_ptr<std::optional<
                    smartpark::AnalysisReport>>(
                    std::make_shared<std::optional<smartpark::AnalysisReport>>());
                auto *done = new std::shared_ptr<std::atomic_bool>(
                    std::make_shared<std::atomic_bool>(false));
                std::thread([state, done, endpoint, snapshot]() mutable{
                    smartpark::RemoteAnalystConfig config;
                    config.endpoint = QString::fromUtf8(endpoint).toStdString();
                    smartpark::RemoteAnalystClient client(config);
                    client.setTransport(
                        [](const std::string &url, const std::string &apiKey,
                           const std::string &requestJson)
                            -> std::optional<std::string> {
                            return macbridge::httpPostJson(url, apiKey,
                                                           requestJson, 20);
                        });
                    **state = client.analyze(snapshot);
                    **done = true;
                }).detach();
                auto *poller = new QTimer(&dialog);
                connect(poller, &QTimer::timeout, &dialog,
                        [this, textView, remoteButton, state, done, poller]{
                    if (!done->get()->load()){
                        return;
                    }
                    poller->stop();
                    remoteButton->setEnabled(true);
                    if ((*state)->has_value()){
                        textView->append(
                            QStringLiteral("\n远程结论（%1）：\n%2").arg(
                                QString::fromStdString((*(**state)).model),
                                QString::fromStdString((*(**state)).summary)));
                    } else{
                        textView->append(QStringLiteral(
                            "\n远程分析失败：请检查端点/密钥与网络（详情见日志）。"));
                    }
                });
                poller->start(200);
            });
        }

        auto *dialogButtons = new QHBoxLayout;
        auto *exportPdfButton = new QPushButton(tr("导出 PDF"), &dialog);
        dialogButtons->addWidget(exportPdfButton);
        dialogButtons->addStretch(1);
        auto *closeButton = new QPushButton(tr("关闭"), &dialog);
        closeButton->setProperty("variant", "primary");
        dialogButtons->addWidget(closeButton);
        dialogLayout->addLayout(dialogButtons);
        connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
        connect(exportPdfButton, &QPushButton::clicked, this,
                [&dialog, text, title = dialog.windowTitle()]{
            const QString pdfPath = QStringLiteral("%1/SmartPark数据分析报告-%2.pdf")
                .arg(QStandardPaths::writableLocation(
                         QStandardPaths::DesktopLocation),
                     QDateTime::currentDateTime().toString(
                         QStringLiteral("yyyyMMdd-HHmmss")));
            if (macbridge::exportTextToPdf(title, text, pdfPath)){
                macbridge::revealInFinder(pdfPath);
                QMessageBox::information(&dialog, QStringLiteral("导出成功"),
                                         QStringLiteral("报告已保存：\n%1").arg(pdfPath));
            } else{
                QMessageBox::warning(&dialog, QStringLiteral("导出失败"),
                                     QStringLiteral("当前平台不支持 PDF 导出。"));
            }
        });
        dialog.exec();
    });
    connect(logoutAction, &QAction::triggered, this, &MainWindow::requestLogout);
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);
    connect(logoutButton, &QPushButton::clicked, this, &MainWindow::requestLogout);
    connect(allocateButton_, &QPushButton::clicked, this, &MainWindow::allocateVehicle);
    connect(updateVehicleTypeButton_, &QPushButton::clicked,
            this, &MainWindow::updateVehicleType);
    connect(releaseButton_, &QPushButton::clicked, this, &MainWindow::releaseVehicle);
    connect(layoutButton_, &QPushButton::clicked, this, &MainWindow::editLayout);
    connect(bookButton_, &QPushButton::clicked, this, &MainWindow::bookVehicle);
    connect(checkInButton_, &QPushButton::clicked, this, &MainWindow::checkInBooking);
    connect(cancelBookingButton_, &QPushButton::clicked, this, &MainWindow::cancelActiveBooking);
    connect(queryButton, &QPushButton::clicked, this, &MainWindow::queryRecords);
    connect(resetFilterButton, &QPushButton::clicked, this, &MainWindow::resetRecordFilter);
    connect(strategyInput_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::updateStrategy);
    connect(emergencyButton_, &QPushButton::clicked, this, [this]{
        const QString plate = plateInput_->text().trimmed();
        if (plate.isEmpty()){
            QMessageBox::information(this, QStringLiteral("请输入车牌"),
                                     QStringLiteral("应急入场前请输入应急车辆车牌。"));
            return;
        }
        const smartpark::Vehicle vehicle(
            plate.toStdString(),
            vehicleTypeFromIndex(vehicleTypeInput_->currentIndex()));
        const auto result = service_->emergencyEnter(vehicle, true);
        emergencyBanner_->setVisible(true);
        if (result){
            const QString banner = QStringLiteral(
                "应急生命通道已开启：%1 占用 %2（出口 %3m），让位车辆已自动结算出场。")
                .arg(plate, QString::fromStdString(result->spotId))
                .arg(QString::number(result->exitRoute.distance, 'f', 1));
            emergencyBanner_->setText(QStringLiteral("🚨 ") + banner);
            macbridge::postNotification(QStringLiteral("应急生命通道"),
                                        banner);
            macbridge::speakChinese(banner);
            refreshScene();
            refreshOccupancy();
            refreshDashboard();
        } else{
            emergencyBanner_->setText(
                QStringLiteral("应急入场失败：车辆已在场内且无可让位车位，请人工处置。"));
        }
    });
}

void MainWindow::showEvent(QShowEvent *event){
    QMainWindow::showEvent(event);
    if (pages_ != nullptr && pages_->currentIndex() == 1){
        fitMapView();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event){
    QMainWindow::resizeEvent(event);
    if (pages_ != nullptr && pages_->currentIndex() == 1){
        fitMapView();
    }
}

void MainWindow::closeEvent(QCloseEvent *event){
    QSettings settings;
    settings.setValue(QStringLiteral("MainWindow/geometry"), saveGeometry());
    if (shellSplitter_ != nullptr){
        settings.setValue(QStringLiteral("MainWindow/splitter"), shellSplitter_->saveState());
    }
    if (navigation_ != nullptr){
        settings.setValue(QStringLiteral("Navigation/lastPage"), navigation_->currentRow());
    }
    if (strategyInput_ != nullptr){
        settings.setValue(QStringLiteral("Operations/strategy"), strategyInput_->currentIndex());
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::changePage(int index){
    if (index < 0 || index >= pages_->count()){
        return;
    }
    pages_->setCurrentIndex(index);
    const auto *item = navigation_->item(index);
    pageTitleLabel_->setText(item->data(Qt::UserRole).toString());
    pageSubtitleLabel_->setText(item->data(Qt::UserRole + 1).toString());
    if (index == 1){
        fitMapView();
    }
}

void MainWindow::requestLogout(){
    const auto choice = QMessageBox::question(
        this, tr("退出登录"), tr("确认退出当前管理员会话并返回登录界面吗？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice == QMessageBox::Yes){
        emit logoutRequested();
    }
}

void MainWindow::fitMapView(){
    if (mapView_ == nullptr || scene_ == nullptr || scene_->sceneRect().isEmpty()){
        return;
    }
    mapView_->fitInView(scene_->sceneRect().adjusted(-2.0, -2.0, 2.0, 2.0),
                        Qt::KeepAspectRatio);
}

smartpark::AllocationStrategy MainWindow::currentStrategy() const{
    return strategyInput_->currentIndex() == 1
        ? smartpark::AllocationStrategy::Nearest
        : smartpark::AllocationStrategy::WeightedCost;
}

bool MainWindow::applyService(const smartpark::ParkingLayout &layout,
                              smartpark::AllocationStrategy strategy){
    if (databasePath_.isEmpty()){
        service_ = std::make_unique<smartpark::ParkingService>(layout, strategy);
        return true;
    }
    if (databaseFailed_){
        service_ = std::make_unique<smartpark::ParkingService>(layout, strategy);
        return false;
    }
    try{
        if (!persistence_){
            persistence_ = std::make_unique<smartpark::Persistence>(databasePath_);
        }
        service_ = std::make_unique<smartpark::ParkingService>(
            layout, strategy, &persistence_->repository());
        if (!auditService_){
            auditService_ = std::make_unique<smartpark::AuditLogService>(
                persistence_->databaseManager().database());
        }
        service_->setAuditLog(auditService_.get());
        return true;
    } catch (const std::exception &error){
        const QString message = QString::fromUtf8(error.what());
        const auto choice = QMessageBox::question(
            this, "无法恢复停车数据",
            QString("数据库中的停车数据无法用于当前布局：\n%1\n\n"
                    "是否重置数据库（清空历史停车记录）并应用当前布局？")
                .arg(message),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (choice != QMessageBox::Yes){
            return false;
        }
        try{
            service_.reset();
            if (!resetDatabase()){
                throw std::runtime_error("cannot remove database files while resetting");
            }
            persistence_ = std::make_unique<smartpark::Persistence>(databasePath_);
            service_ = std::make_unique<smartpark::ParkingService>(
                layout, strategy, &persistence_->repository());
            return true;
        } catch (const std::exception &resetError){
            QMessageBox::warning(this, "数据库重置失败",
                                 QString::fromUtf8(resetError.what()));
            databaseFailed_ = true;
            service_.reset();
            persistence_.reset();
            service_ = std::make_unique<smartpark::ParkingService>(layout, strategy);
            return false;
        }
    }
}

bool MainWindow::resetDatabase(){
    persistence_.reset();
    if (databasePath_.isEmpty()){
        return true;
    }
    bool removed = true;
    const QStringList databaseFiles = {
        databasePath_,
        databasePath_ + QStringLiteral("-wal"),
        databasePath_ + QStringLiteral("-shm")
    };
    for (const QString &path : databaseFiles){
        QFile file(path);
        if (file.exists() && !file.remove()){
            removed = false;
        }
    }
    return removed;
}

void MainWindow::refreshDashboard(){
    if (!service_){
        return;
    }
    const int total = static_cast<int>(service_->spots().size());
    const int available = service_->remainingSpots();
    const int occupied = service_->occupiedSpots();
    const int reserved = service_->reservedSpots();
    if (!qEnvironmentVariableIsSet("SMARTPARK_NO_MENU_BAR")){
        macbridge::setMenuBarStatus(
            QStringLiteral("SmartPark 余位 %1").arg(available));
    }
    if (kpiTotalLabel_ != nullptr){
        kpiTotalLabel_->setText(QString::number(total));
        kpiAvailableLabel_->setText(QString::number(available));
        kpiOccupiedLabel_->setText(QString::number(occupied));
        kpiReservedLabel_->setText(QString::number(reserved));
    }
    const double occupancyRate = total == 0 ? 0.0 : occupied * 100.0 / total;
    if (mapSummaryLabel_ != nullptr){
        mapSummaryLabel_->setText(
            tr("共 %1 个车位 · 空闲 %2 · 占用 %3 · 预留 %4 · 占用率 %5%")
                .arg(total).arg(available).arg(occupied).arg(reserved)
                .arg(occupancyRate, 0, 'f', 1));
    }
    if (dashboardActivityLabel_ != nullptr){
        dashboardActivityLabel_->setText(
            tr("当前占用率 %1%（%2 / %3）。累计停车记录 %4 条，累计已结算费用 %5 元。\n"
               "待结算预约定金 %6 元；爽约没收定金 %7 元。")
                .arg(occupancyRate, 0, 'f', 1)
                .arg(occupied)
                .arg(total)
                .arg(static_cast<int>(service_->records().size()))
                .arg(service_->totalRevenue(), 0, 'f', 2)
                .arg(service_->pendingDeposits(), 0, 'f', 2)
                .arg(service_->forfeitedDeposits(), 0, 'f', 2));
    }
    refreshInsights();
}

smartpark::ParkingInsights MainWindow::computeInsights() const{
    if (!service_){
        return smartpark::ParkingInsights{};
    }
    return smartpark::ParkingInsightEngine::analyze(
        service_->spots(), service_->records(), service_->bookings());
}

void MainWindow::refreshInsights(){
    if (!service_){
        return;
    }
    const smartpark::ParkingInsights insights = computeInsights();

    const smartpark::OccupancyForecast *forecast30 = nullptr;
    const smartpark::OccupancyForecast *forecast60 = nullptr;
    const smartpark::OccupancyForecast *forecast120 = nullptr;
    for (const smartpark::OccupancyForecast &forecast : insights.forecasts){
        if (forecast.minutes == 30){
            forecast30 = &forecast;
        } else if (forecast.minutes == 60){
            forecast60 = &forecast;
        } else if (forecast.minutes == 120){
            forecast120 = &forecast;
        }
    }

    QString highestZone = QStringLiteral("-");
    double highestPressure = 0.0;
    for (const smartpark::ZoneInsight &zone : insights.zones){
        if (zone.pressure > highestPressure){
            highestPressure = zone.pressure;
            highestZone = QString::fromStdString(zone.zone);
        }
    }

    int warningCount = 0;
    int criticalCount = 0;
    QStringList riskTitles;
    for (const smartpark::RiskAlert &alert : insights.alerts){
        if (alert.severity == smartpark::RiskAlert::Severity::Critical){
            ++criticalCount;
            riskTitles << QString::fromStdString(alert.title);
        } else if (alert.severity == smartpark::RiskAlert::Severity::Warning){
            ++warningCount;
            riskTitles << QString::fromStdString(alert.title);
        }
    }

    if (dashboardInsightLabel_ != nullptr){
        const QString forecast60Text = forecast60
            ? QString::number(forecast60->predictedRate, 'f', 0) + QStringLiteral("%")
            : QStringLiteral("-");
        const QString confidenceText = forecast60
            ? QString::number(static_cast<int>(forecast60->confidence * 100.0))
            : QStringLiteral("0");
        QStringList lines;
        lines << tr("预测：60 分钟后占用率 %1（置信度 %2%）")
                     .arg(forecast60Text).arg(confidenceText);
        lines << tr("最高压力分区 %1 · 风险告警 %2 条（严重 %3 条）")
                     .arg(highestZone)
                     .arg(warningCount + criticalCount)
                     .arg(criticalCount);
        if (!riskTitles.isEmpty()){
            lines << tr("重点：%1").arg(riskTitles.join(QStringLiteral("；")));
        }
        lines << tr("趋势推演基于最近 3 小时入场 %1 辆、离场 %2 辆。")
                     .arg(insights.arrivals180)
                     .arg(insights.departures180);
        dashboardInsightLabel_->setText(lines.join(QStringLiteral("\n")));
    }

    if (zoneInsightLabel_ != nullptr){
        QStringList topZones;
        const int zoneLimit = std::min<int>(3, static_cast<int>(insights.zones.size()));
        for (int index = 0; index < zoneLimit; ++index){
            const smartpark::ZoneInsight &zone = insights.zones[index];
            topZones << QStringLiteral("%1 %2%")
                            .arg(QString::fromStdString(zone.zone))
                            .arg(zone.pressure * 100.0, 0, 'f', 0);
        }
        zoneInsightLabel_->setText(
            tr("分区压力：%1 ｜ 压力最高：%2 ｜ 建议优先把新入场车辆引导至压力较低的分区。")
                .arg(topZones.join(QStringLiteral(" · ")))
                .arg(highestZone));
    }

    if (bookingImpactLabel_ != nullptr){
        const QString rate30Text = forecast30
            ? QString::number(forecast30->predictedRate, 'f', 0) + QStringLiteral("%")
            : QStringLiteral("-");
        const int upcoming30 = forecast30 ? forecast30->upcomingBookings : 0;
        const int upcoming60 = forecast60 ? forecast60->upcomingBookings : 0;
        bookingImpactLabel_->setText(
            tr("预约影响：未来 30 分钟新增 %1 个到场，预计占用率升至 %2%；未来 60 分钟新增 %3 个到场。")
                .arg(upcoming30).arg(rate30Text).arg(upcoming60));
    }

    if (recordsTrendLabel_ != nullptr){
        const QString confidenceText = forecast60
            ? QString::number(static_cast<int>(forecast60->confidence * 100.0))
            : QStringLiteral("0");
        recordsTrendLabel_->setText(
            tr("趋势摘要：最近 60 分钟入场 %1 / 离场 %2；最近 180 分钟入场 %3 / 离场 %4。"
               "预测置信度 %5% · 120 分钟预测占用率 %6。")
                .arg(insights.arrivals60).arg(insights.departures60)
                .arg(insights.arrivals180).arg(insights.departures180)
                .arg(confidenceText)
                .arg(forecast120
                         ? QString::number(forecast120->predictedRate, 'f', 0) + QStringLiteral("%")
                         : QStringLiteral("-")));
    }

    if (compositionChart_ != nullptr){
        const int totalSpots = static_cast<int>(service_->spots().size());
        const int occupied = std::max(0, insights.currentOccupied);
        const int reserved = std::max(0, insights.currentReserved);
        const int disabled = std::max(0, insights.currentDisabled);
        const int available = std::max(0, totalSpots - occupied - reserved - disabled);

        QVector<DonutSlice> slices;
        slices.push_back({tr("占用"), static_cast<double>(occupied), QColor(180, 35, 24)});
        slices.push_back({tr("预留"), static_cast<double>(reserved), QColor(181, 71, 8)});
        slices.push_back({tr("空闲"), static_cast<double>(available), QColor(15, 118, 110)});
        slices.push_back({tr("停用"), static_cast<double>(disabled), QColor(102, 112, 133)});
        compositionChart_->setCenterTitle(tr("总车位"));
        compositionChart_->setCenterValue(QString::number(totalSpots));
        compositionChart_->setSlices(slices);
    }

    if (typeChart_ != nullptr){
        int normal = 0;
        int accessible = 0;
        int charging = 0;
        int vip = 0;
        for (const smartpark::ParkingSpot &spot : service_->spots()){
            switch (spot.type()){
            case smartpark::SpotType::Accessible:
                ++accessible;
                break;
            case smartpark::SpotType::Charging:
                ++charging;
                break;
            case smartpark::SpotType::Vip:
                ++vip;
                break;
            case smartpark::SpotType::Normal:
            default:
                ++normal;
                break;
            }
        }

        QVector<DonutSlice> typeSlices;
        typeSlices.push_back({tr("普通"), static_cast<double>(normal), QColor(15, 118, 110)});
        typeSlices.push_back({tr("无障碍"), static_cast<double>(accessible), QColor(79, 70, 229)});
        typeSlices.push_back({tr("充电"), static_cast<double>(charging), QColor(2, 106, 162)});
        typeSlices.push_back({tr("VIP"), static_cast<double>(vip), QColor(124, 58, 237)});
        typeChart_->setCenterTitle(tr("车位类型"));
        typeChart_->setCenterValue(QString::number(normal + accessible + charging + vip));
        typeChart_->setSlices(typeSlices);
    }

    if (forecastChart_ != nullptr){
        LineSeries series;
        series.name = tr("预测占用率");
        series.color = theme::chartPrimary();
        series.points.push_back({tr("当前"), insights.currentRate});
        if (forecast30){
            series.points.push_back({tr("30分"), forecast30->predictedRate});
        }
        if (forecast60){
            series.points.push_back({tr("60分"), forecast60->predictedRate});
        }
        if (forecast120){
            series.points.push_back({tr("120分"), forecast120->predictedRate});
        }
        forecastChart_->setSeries({series});
    }

    if (sevenDayRevenueChart_ != nullptr || sevenDayFlowChart_ != nullptr){
        QVector<QString> dayLabels;
        const QDate today = QDate::currentDate();
        for (int i = 0; i < 7; ++i){
            dayLabels << today.addDays(i - 6).toString(QStringLiteral("MM-dd"));
        }

        QVector<double> revenue(7, 0.0);
        QVector<double> arrivals(7, 0.0);
        QVector<double> departures(7, 0.0);
        const auto dayIndex = [&](const smartpark::ParkingRecord::TimePoint &timePoint){
            const std::time_t raw = smartpark::ParkingRecord::Clock::to_time_t(timePoint);
            const QDate date = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(raw)).date();
            const qint64 delta = date.daysTo(today);
            if (delta < 0 || delta > 6){
                return -1;
            }
            return static_cast<int>(6 - delta);
        };

        for (const smartpark::ParkingRecord &record : service_->records()){
            const int entryIndex = dayIndex(record.entryTime());
            if (entryIndex >= 0){
                arrivals[entryIndex] += 1.0;
            }
            const std::optional<smartpark::ParkingRecord::TimePoint> exit = record.exitTime();
            if (exit){
                const int exitIndex = dayIndex(*exit);
                if (exitIndex >= 0){
                    departures[exitIndex] += 1.0;
                    if (record.isClosed()){
                        revenue[exitIndex] += record.fee();
                    }
                }
            }
        }

        if (sevenDayRevenueChart_ != nullptr){
            LineSeries revenueSeries;
            revenueSeries.name = tr("收入");
            revenueSeries.color = QColor(15, 118, 110);
            for (int i = 0; i < 7; ++i){
                revenueSeries.points.push_back({dayLabels.at(i), revenue.at(i)});
            }
            sevenDayRevenueChart_->setSeries({revenueSeries});
        }

        if (sevenDayFlowChart_ != nullptr){
            LineSeries arrivalSeries;
            arrivalSeries.name = tr("入场");
            arrivalSeries.color = QColor(2, 106, 162);
            LineSeries departureSeries;
            departureSeries.name = tr("离场");
            departureSeries.color = QColor(180, 35, 24);
            for (int i = 0; i < 7; ++i){
                arrivalSeries.points.push_back({dayLabels.at(i), arrivals.at(i)});
                departureSeries.points.push_back({dayLabels.at(i), departures.at(i)});
            }
            sevenDayFlowChart_->setSeries({arrivalSeries, departureSeries});
        }
    }

    if (flowChart_ != nullptr){
        QVector<BarSlice> bars;
        const auto trafficBar = [](const QString &label, int value, const QColor &color){
            BarSlice slice;
            slice.label = label;
            slice.value = static_cast<double>(value);
            slice.color = color;
            slice.valueText = QString::number(value) + QStringLiteral(" 辆");
            return slice;
        };
        bars.push_back(trafficBar(tr("1 小时入场"), insights.arrivals60, QColor(15, 118, 110)));
        bars.push_back(trafficBar(tr("60 分钟离场"), insights.departures60, QColor(180, 35, 24)));
        bars.push_back(trafficBar(tr("3 小时入场"), insights.arrivals180, QColor(2, 106, 162)));
        bars.push_back(trafficBar(tr("3 小时离场"), insights.departures180, QColor(124, 58, 237)));
        flowChart_->setBars(bars);
    }

    if (zonePressureChart_ != nullptr){
        QVector<BarSlice> bars;
        bars.reserve(static_cast<int>(insights.zones.size()));
        for (const smartpark::ZoneInsight &zone : insights.zones){
            const double percent = zone.pressure * 100.0;
            QColor color(15, 118, 110);
            if (percent >= 90.0){
                color = QColor(180, 35, 24);
            } else if (percent >= 80.0){
                color = QColor(181, 71, 8);
            }
            BarSlice slice;
            slice.label = QString::fromStdString(zone.zone);
            slice.value = percent;
            slice.color = color;
            slice.valueText = QString::number(percent, 'f', 0) + QStringLiteral("%");
            bars.push_back(slice);
        }
        zonePressureChart_->setBars(bars);
    }
}

void MainWindow::refreshScene(){
    scene_->clear();
    const smartpark::ParkingLayout &layout = service_->layout();
    const double width = layout.siteWidth();
    const double height = layout.siteHeight();
    const bool garage = isGarageFloorplan(layout);
    scene_->setSceneRect(-4.2, -3.2, width + 7.2, height + 6.0);
    scene_->addRect(0.0, 0.0, width, height, Qt::NoPen, QBrush(QColor(232, 234, 229)));

    if (garage){
        const double xAxes[] = {0.0, 9.0, 18.0, 27.0, 36.0, 45.0, 54.0, 58.0};
        const char *xLabels[] = {"6-1", "6-2", "6-3", "6-4", "6-5", "6-6", "6-7", "6-8"};
        const double yAxes[] = {0.0, 12.2, 24.4, 33.4, 42.4};
        const char *yLabels[] = {"6-E", "6-D", "6-C", "6-B", "6-A"};
        const QPen gridPen(QColor(186, 190, 184), 0.06, Qt::DashLine);
        for (double x : xAxes){
            scene_->addLine(x, 0.0, x, height, gridPen);
        }
        for (double y : yAxes){
            scene_->addLine(0.0, y, width, y, gridPen);
        }
        for (int i = 0; i < 8; ++i){
            addFittedText(scene_, QRectF(xAxes[i] - 1.8, -2.4, 3.6, 1.6),
                          QString::fromUtf8(xLabels[i]), QColor(86, 90, 88), false);
        }
        for (int i = 0; i < 5; ++i){
            addFittedText(scene_, QRectF(-3.8, yAxes[i] - 0.8, 3.2, 1.6),
                          QString::fromUtf8(yLabels[i]), QColor(86, 90, 88), false);
        }
    } else{
        const QPen gridPen(QColor(198, 204, 210), 0.05, Qt::DotLine);
        for (double x = 5.0; x < width; x += 5.0){
            scene_->addLine(x, 0.0, x, height, gridPen);
        }
        for (double y = 5.0; y < height; y += 5.0){
            scene_->addLine(0.0, y, width, y, gridPen);
        }
    }

    for (const smartpark::Point &entrance : layout.entrances()){
        if (entrance.y <= 0.6){
            scene_->addRect(entrance.x - 3.2, 0.05, 6.4, 2.4,
                            QPen(QColor(160, 164, 158), 0.06),
                            QBrush(QColor(176, 178, 172), Qt::BDiagPattern));
        }
    }
    for (const smartpark::Point &exit : layout.exits()){
        if (exit.y <= 0.6){
            scene_->addRect(exit.x - 2.6, 0.05, 5.2, 2.2,
                            QPen(QColor(160, 164, 158), 0.06),
                            QBrush(QColor(176, 178, 172), Qt::FDiagPattern));
        }
    }

    if (!garage){
        for (const smartpark::Rectangle &region : layout.regions()){
            scene_->addRect(region.origin.x, region.origin.y, region.width, region.height,
                            QPen(QColor(170, 178, 188), 0.08, Qt::DashLine),
                            QBrush(QColor(226, 230, 235, 40)));
        }
    }

    for (const smartpark::LayoutObstacle &obstacle : layout.obstacles()){
        const QRectF bounds(obstacle.bounds.origin.x, obstacle.bounds.origin.y,
                            obstacle.bounds.width, obstacle.bounds.height);
        scene_->addRect(bounds, QPen(QColor(92, 96, 102), 0.22),
                        QBrush(QColor(210, 214, 218)));
        scene_->addRect(bounds.adjusted(0.18, 0.18, -0.18, -0.18),
                        QPen(QColor(120, 124, 128), 0.08),
                        QBrush(QColor(168, 172, 176), Qt::BDiagPattern));
        addFittedText(scene_, bounds.adjusted(0.4, 0.4, -0.4, -0.4),
                      QString::fromStdString(obstacle.name), QColor(62, 66, 70), false);
    }

    for (const smartpark::ParkingSpot &spot : service_->spots()){
        const QRectF bounds(spot.bounds().origin.x, spot.bounds().origin.y,
                            spot.bounds().width, spot.bounds().height);
        auto *item = scene_->addRect(bounds, QPen(QColor(48, 52, 56), 0.1),
                                     QBrush(stallFillColor(spot)));
        const QString plate = spot.parkedVehicle()
            ? QString::fromStdString(spot.parkedVehicle()->plateNumber())
            : QString();
        const QString vehicle = spot.parkedVehicle()
            ? QString::fromUtf8(vehicleTypeText(spot.parkedVehicle()->type()))
            : QStringLiteral("-");
        item->setToolTip(QString("%1 | %2 | %3 | %4 | %5")
                             .arg(QString::fromStdString(spot.identifier()))
                             .arg(QString::fromUtf8(spotTypeText(spot.type())))
                             .arg(statusText(spot.status()))
                             .arg(plate.isEmpty() ? QStringLiteral("-") : plate)
                             .arg(vehicle));
        QString label = QString::fromStdString(spot.identifier());
        if (!plate.isEmpty()){
            label += QLatin1Char('\n') + plate;
        } else if (spot.type() != smartpark::SpotType::Normal
                   && spot.status() == smartpark::SpotStatus::Available){
            label += QLatin1Char('\n') + QString::fromUtf8(spotTypeText(spot.type()));
        }
        addFittedText(scene_, bounds.adjusted(0.08, 0.08, -0.08, -0.08),
                      label, labelColor(spot.status()));
    }

    std::vector<smartpark::Point> gates = layout.entrances();
    gates.insert(gates.end(), layout.exits().begin(), layout.exits().end());
    const QPen wallPen(QColor(46, 50, 54), 0.42);
    addWallWithGaps(scene_, QPointF(0.0, 0.0), QPointF(width, 0.0), gates, 4.6, wallPen);
    addWallWithGaps(scene_, QPointF(0.0, height), QPointF(width, height), gates, 4.6, wallPen);
    addWallWithGaps(scene_, QPointF(0.0, 0.0), QPointF(0.0, height), gates, 4.6, wallPen);
    addWallWithGaps(scene_, QPointF(width, 0.0), QPointF(width, height), gates, 4.6, wallPen);

    for (const smartpark::Point &entrance : layout.entrances()){
        addFittedText(scene_, QRectF(entrance.x - 2.4, -1.15, 4.8, 1.1),
                      QStringLiteral("入口"), QColor(36, 72, 160), false);
    }
    for (const smartpark::Point &exit : layout.exits()){
        addFittedText(scene_, QRectF(exit.x - 2.4, -1.15, 4.8, 1.1),
                      QStringLiteral("出口"), QColor(176, 84, 24), false);
    }

    if (lastAllocation_){
        scene_->addPolygon(routePolygon(lastAllocation_->entryRoute),
                           QPen(QColor(36, 92, 196), 0.28));
        scene_->addPolygon(routePolygon(lastAllocation_->exitRoute),
                           QPen(QColor(230, 132, 0), 0.22, Qt::DashLine));
    }
    statusLabel_->setText(QString("总车位：%1 / 空闲：%2 / 占用：%3 / 预留：%4")
                              .arg(static_cast<int>(service_->spots().size()))
                              .arg(service_->remainingSpots())
                              .arg(service_->occupiedSpots())
                              .arg(service_->reservedSpots()));
    fitMapView();
}

void MainWindow::editLayout(){
    QDialog dialog(this);
    dialog.setWindowTitle("自定义停车场布局");
    dialog.resize(620, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(
        "格式：site 宽 高\n"
        "entrance x y / exit x y，可写多条\n"
        "region 名称 x y 行数 列数 车位宽 车位长 通道宽 left|right|up|down [类型]\n"
        "obstacle x y 宽 高 [名称]  用于机房、楼梯等不可通行区域\n"
        "up/down 为南北向停车，通道在车位上/下方；left/right 为东西向停车。",
        &dialog);
    hint->setWordWrap(true);
    auto *editor = new QPlainTextEdit(&dialog);
    editor->setPlainText(layoutText_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("应用布局");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    layout->addWidget(hint);
    layout->addWidget(editor, 1);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted){
        return;
    }
    const QString previous = layoutText_;
    layoutText_ = editor->toPlainText();
    if (!applyLayout()){
        layoutText_ = previous;
    }
}

bool MainWindow::applyLayout(){
    std::optional<smartpark::ParkingLayout> layout;
    try{
        layout = smartpark::ParkingLayout::fromDescription(layoutText_.toStdString());
    } catch (const std::exception &error){
        QMessageBox::warning(this, "布局错误", QString::fromUtf8(error.what()));
        statusLabel_->setText(QStringLiteral("布局错误，已保留原布局。"));
        return false;
    }
    const bool persisted = applyService(*layout, currentStrategy());
    if (service_){
        lastAllocation_.reset();
        refreshScene();
        refreshBookings();
        refreshRecords();
        refreshOccupancy();
    }
    if (databaseFailed_){
        statusLabel_->setText(
            "数据库重置失败，已降级为内存模式：新布局仅保存在内存，重启后不会保留。");
    } else if (!persisted){
        statusLabel_->setText("已取消数据库恢复，保留原有布局与数据。");
    } else{
        statusLabel_->setText(QString("已应用自定义布局：%1 个车位。")
                                  .arg(static_cast<int>(service_->spots().size())));
        return true;
    }
    return persisted;
}

void MainWindow::updateStrategy(){
    if (service_){
        service_->setStrategy(currentStrategy());
    }
    if (strategyInput_ != nullptr){
        QSettings().setValue(QStringLiteral("Operations/strategy"), strategyInput_->currentIndex());
    }
}

void MainWindow::allocateVehicle(){
    const QString plate = plateInput_->text().trimmed();
    if (plate.isEmpty()){
        QMessageBox::information(this, "请输入车牌", "自动分配前请输入车辆车牌。");
        return;
    }
    const smartpark::Vehicle vehicle(plate.toStdString(),
                                     vehicleTypeFromIndex(vehicleTypeInput_->currentIndex()));
    // 只读 A/B 预览：不占车位、不写库，用于解释推荐并对比“最近车位”策略。
    const auto weightedPreview =
        service_->previewAllocation(vehicle, smartpark::AllocationStrategy::WeightedCost);
    const auto nearestPreview =
        service_->previewAllocation(vehicle, smartpark::AllocationStrategy::Nearest);
    const std::optional<smartpark::AllocationResult> result = service_->enter(vehicle);
    if (!result){
        QMessageBox::warning(this, "无可用车位", "当前停车场已满、车位已预留或没有可达车位。");
        return;
    }
    lastAllocation_ = result;
    refreshScene();
    refreshRecords();
    refreshOccupancy();

    if (recommendationLabel_ != nullptr){
        QStringList lines;
        lines << tr("最近推荐：车位 %1 | 入场 %2 m | 离场 %3 m | 拥堵 %4 | 分区压力 %5 | 转向 %6 | 类型成本 %7 | 综合评分 %8")
                     .arg(QString::fromStdString(result->spotId))
                     .arg(result->entryRoute.distance, 0, 'f', 1)
                     .arg(result->exitRoute.distance, 0, 'f', 1)
                     .arg(result->nearbyOccupiedSpots)
                     .arg(result->breakdown.zonePressureCost, 0, 'f', 1)
                     .arg(result->entryRoute.turnCount + result->exitRoute.turnCount)
                     .arg(result->breakdown.typePenalty, 0, 'f', 1)
                     .arg(result->score, 0, 'f', 1);
        if (weightedPreview && nearestPreview){
            lines << tr("A/B 对比：加权代价 → 车位 %1（%2）｜ 最近车位 → 车位 %3（%4）")
                         .arg(QString::fromStdString(weightedPreview->spotId))
                         .arg(weightedPreview->score, 0, 'f', 1)
                         .arg(QString::fromStdString(nearestPreview->spotId))
                         .arg(nearestPreview->score, 0, 'f', 1);
            if (result->strategy == smartpark::AllocationStrategy::WeightedCost
                && weightedPreview->spotId != nearestPreview->spotId
                && weightedPreview->score <= nearestPreview->score){
                lines << tr("为什么没选最近车位：最近车位入场更短，但离场、拥堵或车型适配成本更高，综合评分不如加权车位。");
            } else if (result->strategy == smartpark::AllocationStrategy::Nearest){
                lines << tr("当前策略为最近车位：优先缩短入场距离，忽略离场与拥堵成本。");
            }
        }
        lines << tr("说明：以上为可加权的透明代价解释，并非大模型输出。");
        recommendationLabel_->setText(lines.join(QStringLiteral("\n")));
    }

    statusLabel_->setText(
        QString("总车位：%1 / 空闲：%2 / 占用：%3 | 已分配：%4 | 入口：%5m | 出口：%6m | "
                "拥堵：%7 | 分区压力：%8 | 转向：%9 | 类型：%10 | 综合评分：%11")
            .arg(static_cast<int>(service_->spots().size()))
            .arg(service_->remainingSpots())
            .arg(service_->occupiedSpots())
            .arg(QString::fromStdString(result->spotId))
            .arg(result->entryRoute.distance, 0, 'f', 1)
            .arg(result->exitRoute.distance, 0, 'f', 1)
            .arg(result->nearbyOccupiedSpots)
            .arg(result->breakdown.zonePressureCost, 0, 'f', 1)
            .arg(result->entryRoute.turnCount + result->exitRoute.turnCount)
            .arg(result->breakdown.typePenalty, 0, 'f', 1)
            .arg(result->score, 0, 'f', 1));
}

void MainWindow::updateVehicleType(){
    const QString plate = plateInput_->text().trimmed();
    if (plate.isEmpty()){
        QMessageBox::information(this, "请输入车牌",
                                 "修改车辆类型前请输入在场车辆车牌。");
        return;
    }
    const smartpark::VehicleType newType =
        vehicleTypeFromIndex(vehicleTypeInput_->currentIndex());
    bool hadVehicle = false;
    smartpark::VehicleType previousType = smartpark::VehicleType::Car;
    for (const smartpark::ParkingSpot &spot : service_->spots()){
        if (spot.parkedVehicle()
            && spot.parkedVehicle()->plateNumber() == plate.toStdString()){
            hadVehicle = true;
            previousType = spot.parkedVehicle()->type();
            break;
        }
    }
    if (!service_->updateVehicleType(plate.toStdString(), newType)){
        QMessageBox::warning(this, "修改失败",
                             QString("无法修改车牌 %1 的车辆类型：车辆不在场或数据库更新失败。")
                                 .arg(plate));
        return;
    }

    QString spotId;
    for (const smartpark::ParkingSpot &spot : service_->spots()){
        if (spot.parkedVehicle()
            && spot.parkedVehicle()->plateNumber() == plate.toStdString()){
            spotId = QString::fromStdString(spot.identifier());
            break;
        }
    }
    refreshScene();
    refreshBookings();
    refreshOccupancy();
    const QString action = hadVehicle && previousType == newType
        ? QStringLiteral("车辆类型未变化")
        : QStringLiteral("车辆类型已修改");
    statusLabel_->setText(QString("%1：%2 | 车位：%3 | 当前类型：%4")
                              .arg(action)
                              .arg(plate)
                              .arg(spotId)
                              .arg(QString::fromUtf8(vehicleTypeText(newType))));
}

void MainWindow::releaseVehicle(){
    QString typedPlate = plateInput_->text().trimmed();
    if (typedPlate.isEmpty() && occupancyTable_ != nullptr){
        const int selectedRow = occupancyTable_->currentRow();
        if (selectedRow >= 0 && occupancyTable_->item(selectedRow, 2)
            && occupancyTable_->item(selectedRow, 2)->text() == QStringLiteral("占用")
            && occupancyTable_->item(selectedRow, 3)){
            typedPlate = occupancyTable_->item(selectedRow, 3)->text().trimmed();
        }
    }
    const std::string plate = !typedPlate.isEmpty()
        ? typedPlate.toStdString()
        : (lastAllocation_ ? lastAllocation_->plateNumber : std::string{});
    if (plate.empty()){
        QMessageBox::information(this, "没有可离场车辆",
                                 "请输入在场车辆车牌，或在当前车位表选中占用车辆。");
        return;
    }
    QString completionMessage;
    const auto closedRecord = service_->leave(plate);
    if (closedRecord){
        const auto duration = std::chrono::duration_cast<std::chrono::minutes>(
            closedRecord->duration());
        completionMessage =
            QString("离场完成：%1 | 车位：%2 | 停车时长：%3分钟 | 本次费用：%4元 | 累计收费：%5元 | 总记录：%6")
                .arg(QString::fromStdString(closedRecord->plateNumber()))
                .arg(QString::fromStdString(closedRecord->spotId()))
                .arg(duration.count())
                .arg(closedRecord->fee(), 0, 'f', 2)
                .arg(service_->totalRevenue(), 0, 'f', 2)
                .arg(static_cast<int>(service_->records().size()));
        lastAllocation_.reset();
        refreshScene();
        refreshRecords();
        refreshOccupancy();
        statusLabel_->setText(completionMessage);
    } else{
        QMessageBox::warning(this, "离场失败",
                             QString("车牌 %1 不存在可离场的在场记录。")
                                 .arg(QString::fromStdString(plate)));
    }
}

void MainWindow::refreshBookings(){
    const auto &bookings = service_->bookings();
    bookingsTable_->setRowCount(static_cast<int>(bookings.size()));
    int row = 0;
    for (const smartpark::Booking &booking : bookings){
        const QString cells[] = {
            QString::fromStdString(booking.id()),
            QString::fromStdString(booking.plateNumber()),
            QString::fromStdString(booking.spotId()),
            formatTime(booking.createdAt()),
            formatTime(booking.arrivalTime()),
            formatTime(booking.arrivalDeadline()),
            QString::number(booking.deposit(), 'f', 2),
            bookingStatusText(booking.status()),
        };
        for (int column = 0; column < 8; ++column){
            auto *item = new QTableWidgetItem(cells[column]);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            bookingsTable_->setItem(row, column, item);
        }
        ++row;
    }
    depositLabel_->setText(
        QString("预约记录：%1 条 | 待结算定金：%2 元 | 爽约没收定金：%3 元")
            .arg(static_cast<int>(bookings.size()))
            .arg(service_->pendingDeposits(), 0, 'f', 2)
            .arg(service_->forfeitedDeposits(), 0, 'f', 2));
}

void MainWindow::refreshOccupancy(){
    const auto &spots = service_->spots();
    occupancyTable_->setRowCount(static_cast<int>(spots.size()));
    int row = 0;
    for (const smartpark::ParkingSpot &spot : spots){
        const QString plate = spot.parkedVehicle()
            ? QString::fromStdString(spot.parkedVehicle()->plateNumber())
            : QStringLiteral("-");
        const QString type = spot.parkedVehicle()
            ? QString::fromUtf8(vehicleTypeText(spot.parkedVehicle()->type()))
            : QStringLiteral("-");
        const QString cells[] = {
            QString::fromStdString(spot.identifier()),
            QString::fromUtf8(spotTypeText(spot.type())),
            statusText(spot.status()),
            plate,
            type,
        };
        for (int column = 0; column < 5; ++column){
            auto *item = new QTableWidgetItem(cells[column]);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            occupancyTable_->setItem(row, column, item);
        }
        ++row;
    }
    refreshDashboard();
}

void MainWindow::refreshRecords(){
    const auto &records = service_->records();
    const auto from = fromDateTime(recordFromInput_->dateTime());
    const auto to = fromDateTime(recordToInput_->dateTime());
    std::vector<const smartpark::ParkingRecord *> visible;
    visible.reserve(records.size());
    for (const smartpark::ParkingRecord &record : records){
        if (!recordsFilterActive_ || recordOverlapsRange(record, from, to)){
            visible.push_back(&record);
        }
    }
    recordsTable_->setRowCount(static_cast<int>(visible.size()));
    int row = 0;
    int parkedCount = 0;
    double feeSum = 0.0;
    for (const smartpark::ParkingRecord *record : visible){
        const bool closed = record->isClosed();
        if (!closed){
            ++parkedCount;
        }
        feeSum += record->fee();
        const auto duration = std::chrono::duration_cast<std::chrono::minutes>(record->duration());
        const QString cells[] = {
            QString::fromStdString(record->plateNumber()),
            QString::fromStdString(record->spotId()),
            formatTime(record->entryTime()),
            closed ? formatTime(*record->exitTime()) : QStringLiteral("在停"),
            QString::number(duration.count()),
            QString::number(record->fee(), 'f', 2),
            closed ? QStringLiteral("已离场") : QStringLiteral("在停"),
        };
        for (int column = 0; column < 7; ++column){
            auto *item = new QTableWidgetItem(cells[column]);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            recordsTable_->setItem(row, column, item);
        }
        ++row;
    }
    if (recordsFilterActive_){
        recordsLabel_->setText(
            QString("查询结果：%1 条（全部 %2 条） | 在停 %3 辆 | 费用合计 %4 元 | 时间 %5 至 %6")
                .arg(static_cast<int>(visible.size()))
                .arg(static_cast<int>(records.size()))
                .arg(parkedCount)
                .arg(feeSum, 0, 'f', 2)
                .arg(recordFromInput_->dateTime().toString("yyyy-MM-dd HH:mm"))
                .arg(recordToInput_->dateTime().toString("yyyy-MM-dd HH:mm")));
    } else{
        recordsLabel_->setText(
            QString("全部停车记录：%1 条 | 在停 %2 辆 | 费用合计 %3 元")
                .arg(static_cast<int>(visible.size()))
                .arg(parkedCount)
                .arg(feeSum, 0, 'f', 2));
    }
}

void MainWindow::queryRecords(){
    if (recordFromInput_->dateTime() > recordToInput_->dateTime()){
        QMessageBox::information(this, "时间范围无效", "起始时间不能晚于结束时间。");
        return;
    }
    recordsFilterActive_ = true;
    refreshRecords();
}

void MainWindow::resetRecordFilter(){
    recordsFilterActive_ = false;
    recordFromInput_->setDateTime(QDateTime::currentDateTime().addDays(-7));
    recordToInput_->setDateTime(QDateTime::currentDateTime().addDays(1));
    refreshRecords();
}

QString MainWindow::activeBookingPlate() const{
    if (bookingPlateInput_ != nullptr && !bookingPlateInput_->text().trimmed().isEmpty()){
        return bookingPlateInput_->text().trimmed();
    }
    return plateInput_ == nullptr ? QString{} : plateInput_->text().trimmed();
}

void MainWindow::bookVehicle(){
    const QString plate = activeBookingPlate();
    if (plate.isEmpty()){
        QMessageBox::information(this, tr("请输入车牌"), tr("预约前请在预约管理页输入车辆车牌。"));
        return;
    }
    const QDateTime arrival = arrivalInput_->dateTime();
    if (arrival <= QDateTime::currentDateTime()){
        QMessageBox::information(this, "到场时间无效", "预约到场时间必须在当前时间之后。");
        return;
    }
    const auto arrivalTime = smartpark::Booking::Clock::time_point(
        std::chrono::seconds(arrival.toSecsSinceEpoch()));
    const int vehicleTypeIndex = bookingVehicleTypeInput_ != nullptr
        ? bookingVehicleTypeInput_->currentIndex()
        : vehicleTypeInput_->currentIndex();
    const smartpark::Vehicle vehicle(
        plate.toStdString(), vehicleTypeFromIndex(vehicleTypeIndex));
    const auto booked = service_->createBooking(vehicle, arrivalTime);
    if (!booked){
        QMessageBox::warning(
            this, "预约失败",
            QString("车牌 %1 无法预约：车牌已在场或已有生效预约、到场时间超出可预约范围"
                    "（最多提前 %2 天）或无可用车位。")
                .arg(plate)
                .arg(service_->bookingPolicy().advanceDays));
        return;
    }
    lastAllocation_ = booked->allocation;
    refreshScene();
    refreshBookings();
    refreshOccupancy();
    statusLabel_->setText(
        QString("预约成功：%1 | 车位 %2 | 到场 %3 | 宽限截止 %4 | 定金 %5 元已收取（待结算 %6 元）")
            .arg(QString::fromStdString(booked->booking.id()))
            .arg(QString::fromStdString(booked->booking.spotId()))
            .arg(formatTime(booked->booking.arrivalTime()))
            .arg(formatTime(booked->booking.arrivalDeadline()))
            .arg(booked->booking.deposit(), 0, 'f', 2)
            .arg(service_->pendingDeposits(), 0, 'f', 2));
}

void MainWindow::checkInBooking(){
    const QString plate = activeBookingPlate();
    if (plate.isEmpty()){
        QMessageBox::information(this, tr("请输入车牌"), tr("到场确认前请输入预约时使用的车牌。"));
        return;
    }
    const auto arrived = service_->confirmBooking(plate.toStdString());
    if (!arrived){
        QMessageBox::warning(
            this, "到场确认失败",
            QString("车牌 %1 没有可确认的预约：未预约、已取消、未到到场时间或已超过宽限期。")
                .arg(plate));
        return;
    }
    lastAllocation_ = arrived;
    refreshScene();
    refreshBookings();
    refreshRecords();
    refreshOccupancy();
    statusLabel_->setText(
        QString("到场确认成功：%1 转入停车，占用车位 %2，定金退回（离场时按计费规则结算）。")
            .arg(plate)
            .arg(QString::fromStdString(arrived->spotId)));
}

void MainWindow::cancelActiveBooking(){
    const QString plate = activeBookingPlate();
    if (plate.isEmpty()){
        QMessageBox::information(this, tr("请输入车牌"), tr("取消预约前请输入预约时使用的车牌。"));
        return;
    }
    if (!service_->cancelBooking(plate.toStdString())){
        QMessageBox::warning(
            this, "取消失败",
            QString("车牌 %1 没有可取消的预约：取消必须在预约到场时间之前。").arg(plate));
        return;
    }
    refreshScene();
    refreshBookings();
    refreshOccupancy();
    statusLabel_->setText(
        QString("取消成功：%1 预约已取消，定金退回，车位已释放。").arg(plate));
}
