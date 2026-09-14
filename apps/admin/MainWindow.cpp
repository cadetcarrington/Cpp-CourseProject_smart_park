#include "MainWindow.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QComboBox>
#include <QColor>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFont>
#include <QGraphicsTextItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTransform>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
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

const char *vehicleTypeText(smartpark::VehicleType type){
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
        return QStringLiteral("预留");
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
        return QColor(237, 201, 72);
    case smartpark::SpotStatus::Occupied:
        return QColor(214, 86, 86);
    case smartpark::SpotStatus::Disabled:
        return QColor(168, 172, 178);
    case smartpark::SpotStatus::Available:
    default:
        break;
    }
    switch (spot.type()){
    case smartpark::SpotType::Accessible:
        return QColor(118, 156, 224);
    case smartpark::SpotType::Charging:
        return QColor(64, 176, 148);
    case smartpark::SpotType::Vip:
        return QColor(232, 186, 74);
    case smartpark::SpotType::Normal:
    default:
        return QColor(90, 176, 118);
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
    : QMainWindow(parent)
    , databasePath_(std::move(databasePath))
    , layoutText_(kDefaultLayoutText){
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
    refreshScene();
    refreshBookings();
    refreshRecords();
    refreshOccupancy();
    if (!persistenceActive){
        statusLabel_->setText(
            databaseFailed_
                ? "数据库重置失败，已降级为内存模式：重启后数据不会保留。"
                : "数据库未启用或恢复被取消，当前为内存模式，重启后数据不会保留。");
    }
}

void MainWindow::buildUi(){
    setWindowTitle("SmartPark 管理员端");
    resize(1400, 920);
    auto *centralWidget = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(centralWidget);

    auto *controlBar = new QWidget(centralWidget);
    auto *controlLayout = new QHBoxLayout(controlBar);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    plateInput_ = new QLineEdit(controlBar);
    plateInput_->setPlaceholderText("车牌，例如：晋A12345");
    vehicleTypeInput_ = new QComboBox(controlBar);
    vehicleTypeInput_->addItems({"轿车", "摩托车", "卡车", "电动车"});
    strategyInput_ = new QComboBox(controlBar);
    strategyInput_->addItems({"加权代价", "最近车位"});
    allocateButton_ = new QPushButton("自动分配车位", controlBar);
    releaseButton_ = new QPushButton("释放最近车位", controlBar);
    layoutButton_ = new QPushButton("自定义停车场布局", controlBar);
    controlLayout->addWidget(plateInput_, 1);
    controlLayout->addWidget(vehicleTypeInput_);
    controlLayout->addWidget(strategyInput_);
    controlLayout->addWidget(allocateButton_);
    controlLayout->addWidget(releaseButton_);
    controlLayout->addWidget(layoutButton_);

    auto *verticalSplitter = new QSplitter(Qt::Vertical, centralWidget);

    auto *mapGroup = new QGroupBox("实时车位、车牌与路线", verticalSplitter);
    auto *mapLayout = new QVBoxLayout(mapGroup);
    scene_ = new QGraphicsScene(mapGroup);
    mapView_ = new QGraphicsView(scene_, mapGroup);
    mapView_->setRenderHint(QPainter::Antialiasing);
    mapView_->setMinimumSize(980, 560);
    mapView_->setDragMode(QGraphicsView::ScrollHandDrag);
    mapView_->setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    mapLayout->addWidget(mapView_);
    mapLayout->addWidget(new QLabel(
        "图纸方向：北在上，轴线 6-1→6-8 / 6-E→6-A。空闲色=车位类型（绿普通 / 蓝无障碍 / 青充电 / 金VIP），"
        "红=占用，黄=预留；斜线块=机房/楼梯。车位显示编号和当前车牌，蓝线入场，橙虚线离场。",
        mapGroup));

    auto *tabWidget = new QTabWidget(verticalSplitter);

    auto *recordsPage = new QWidget(tabWidget);
    auto *recordsLayout = new QVBoxLayout(recordsPage);
    auto *filterBar = new QWidget(recordsPage);
    auto *filterLayout = new QHBoxLayout(filterBar);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    recordFromInput_ = new QDateTimeEdit(filterBar);
    recordFromInput_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    recordFromInput_->setCalendarPopup(true);
    recordFromInput_->setDateTime(QDateTime::currentDateTime().addDays(-7));
    recordToInput_ = new QDateTimeEdit(filterBar);
    recordToInput_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    recordToInput_->setCalendarPopup(true);
    recordToInput_->setDateTime(QDateTime::currentDateTime().addDays(1));
    auto *queryButton = new QPushButton("查询", filterBar);
    auto *resetFilterButton = new QPushButton("显示全部", filterBar);
    filterLayout->addWidget(new QLabel("时间范围：", filterBar));
    filterLayout->addWidget(recordFromInput_);
    filterLayout->addWidget(new QLabel("至", filterBar));
    filterLayout->addWidget(recordToInput_);
    filterLayout->addWidget(queryButton);
    filterLayout->addWidget(resetFilterButton);
    filterLayout->addStretch(1);
    recordsTable_ = new QTableWidget(recordsPage);
    recordsTable_->setColumnCount(7);
    recordsTable_->setHorizontalHeaderLabels(
        {"车牌", "车位", "入场时间", "离场时间", "时长(分钟)", "费用(元)", "状态"});
    recordsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    recordsTable_->verticalHeader()->setVisible(false);
    recordsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recordsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    recordsTable_->setMinimumHeight(180);
    recordsLabel_ = new QLabel(recordsPage);
    recordsLabel_->setWordWrap(true);
    recordsLayout->addWidget(filterBar);
    recordsLayout->addWidget(recordsTable_, 1);
    recordsLayout->addWidget(recordsLabel_);
    connect(queryButton, &QPushButton::clicked, this, &MainWindow::queryRecords);
    connect(resetFilterButton, &QPushButton::clicked, this, &MainWindow::resetRecordFilter);

    auto *occupancyPage = new QWidget(tabWidget);
    auto *occupancyLayout = new QVBoxLayout(occupancyPage);
    occupancyTable_ = new QTableWidget(occupancyPage);
    occupancyTable_->setColumnCount(5);
    occupancyTable_->setHorizontalHeaderLabels(
        {"车位", "类型", "状态", "车牌", "车辆类型"});
    occupancyTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    occupancyTable_->verticalHeader()->setVisible(false);
    occupancyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    occupancyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    occupancyLayout->addWidget(new QLabel("每个车位的当前占用情况，空闲车位车牌显示为 -。", occupancyPage));
    occupancyLayout->addWidget(occupancyTable_, 1);

    auto *bookingPage = new QWidget(tabWidget);
    auto *bookingLayout = new QVBoxLayout(bookingPage);
    auto *bookingBar = new QWidget(bookingPage);
    auto *bookingBarLayout = new QHBoxLayout(bookingBar);
    bookingBarLayout->setContentsMargins(0, 0, 0, 0);
    arrivalInput_ = new QDateTimeEdit(bookingBar);
    arrivalInput_->setDateTime(QDateTime::currentDateTime().addSecs(60 * 60));
    arrivalInput_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    arrivalInput_->setCalendarPopup(true);
    arrivalInput_->setMinimumDateTime(QDateTime::currentDateTime());
    arrivalInput_->setToolTip(QStringLiteral("预约到场时间：当前时间之后、最多提前 7 天"));
    bookButton_ = new QPushButton("预约车位", bookingBar);
    checkInButton_ = new QPushButton("到场确认", bookingBar);
    cancelBookingButton_ = new QPushButton("取消预约", bookingBar);
    bookingBarLayout->addWidget(new QLabel("到场时间：", bookingBar));
    bookingBarLayout->addWidget(arrivalInput_);
    bookingBarLayout->addWidget(bookButton_);
    bookingBarLayout->addWidget(checkInButton_);
    bookingBarLayout->addWidget(cancelBookingButton_);
    bookingBarLayout->addStretch(1);
    const smartpark::BookingPolicy bookingPolicy = service_->bookingPolicy();
    auto *bookingPolicyLabel = new QLabel(
        QString("预约规则：定金 %1 元 | 最多提前 %2 天 | 到场宽限期 %3 分钟 | "
                "车牌与车辆类型沿用上方输入框")
            .arg(bookingPolicy.deposit, 0, 'f', 2)
            .arg(bookingPolicy.advanceDays)
            .arg(bookingPolicy.gracePeriod.count()),
        bookingPage);
    bookingPolicyLabel->setWordWrap(true);
    bookingsTable_ = new QTableWidget(bookingPage);
    bookingsTable_->setColumnCount(8);
    bookingsTable_->setHorizontalHeaderLabels(
        {"编号", "车牌", "车位", "创建时间", "到场时间", "宽限截止", "定金(元)", "状态"});
    bookingsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    bookingsTable_->verticalHeader()->setVisible(false);
    bookingsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bookingsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    bookingsTable_->setMinimumHeight(160);
    depositLabel_ = new QLabel(bookingPage);
    depositLabel_->setWordWrap(true);
    bookingLayout->addWidget(bookingBar);
    bookingLayout->addWidget(bookingPolicyLabel);
    bookingLayout->addWidget(bookingsTable_, 1);
    bookingLayout->addWidget(depositLabel_);

    tabWidget->addTab(recordsPage, "停车记录");
    tabWidget->addTab(occupancyPage, "当前车位");
    tabWidget->addTab(bookingPage, "车位预约");

    verticalSplitter->addWidget(mapGroup);
    verticalSplitter->addWidget(tabWidget);
    verticalSplitter->setStretchFactor(0, 3);
    verticalSplitter->setStretchFactor(1, 2);
    verticalSplitter->setSizes({620, 240});

    billingLabel_ = new QLabel(centralWidget);
    billingLabel_->setWordWrap(true);
    const smartpark::BillingRule rule = service_->billing().rule();
    billingLabel_->setText(
        QString("计费规则：免费 %1 分钟，之后每 %2 分钟计费一次；"
                "首单元 %3 元，后续每单元 %4 元，单次封顶 %5 元")
            .arg(rule.freeDuration.count())
            .arg(rule.billingUnit.count())
            .arg(rule.minimumFee, 0, 'f', 2)
            .arg(rule.unitFee, 0, 'f', 2)
            .arg(rule.dailyCap, 0, 'f', 2));
    statusLabel_ = new QLabel(centralWidget);
    statusLabel_->setWordWrap(true);

    rootLayout->addWidget(controlBar);
    rootLayout->addWidget(verticalSplitter, 1);
    rootLayout->addWidget(billingLabel_);
    rootLayout->addWidget(statusLabel_);
    setCentralWidget(centralWidget);

    connect(allocateButton_, &QPushButton::clicked, this, &MainWindow::allocateVehicle);
    connect(releaseButton_, &QPushButton::clicked, this, &MainWindow::releaseLastVehicle);
    connect(layoutButton_, &QPushButton::clicked, this, &MainWindow::editLayout);
    connect(bookButton_, &QPushButton::clicked, this, &MainWindow::bookVehicle);
    connect(checkInButton_, &QPushButton::clicked, this, &MainWindow::checkInBooking);
    connect(cancelBookingButton_, &QPushButton::clicked, this, &MainWindow::cancelActiveBooking);
    connect(strategyInput_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::updateStrategy);
}

void MainWindow::showEvent(QShowEvent *event){
    QMainWindow::showEvent(event);
    fitMapView();
}

void MainWindow::resizeEvent(QResizeEvent *event){
    QMainWindow::resizeEvent(event);
    fitMapView();
}

void MainWindow::fitMapView(){
    if (mapView_ == nullptr || scene_ == nullptr){
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
            resetDatabase();
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

void MainWindow::resetDatabase(){
    persistence_.reset();
    if (databasePath_.isEmpty()){
        return;
    }
    QFile::remove(databasePath_);
    QFile::remove(databasePath_ + QStringLiteral("-wal"));
    QFile::remove(databasePath_ + QStringLiteral("-shm"));
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
}

void MainWindow::allocateVehicle(){
    const QString plate = plateInput_->text().trimmed();
    if (plate.isEmpty()){
        QMessageBox::information(this, "请输入车牌", "自动分配前请输入车辆车牌。");
        return;
    }
    const smartpark::Vehicle vehicle(plate.toStdString(),
                                     vehicleTypeFromIndex(vehicleTypeInput_->currentIndex()));
    const std::optional<smartpark::AllocationResult> result = service_->enter(vehicle);
    if (!result){
        QMessageBox::warning(this, "无可用车位", "当前停车场已满、车位已预留或没有可达车位。");
        return;
    }
    lastAllocation_ = result;
    refreshScene();
    refreshRecords();
    refreshOccupancy();
    statusLabel_->setText(
        QString("总车位：%1 / 空闲：%2 / 占用：%3 | 已分配：%4 | 入口：%5m | 出口：%6m | "
                "拥堵：%7 | 转向：%8 | 类型：%9 | 综合评分：%10")
            .arg(static_cast<int>(service_->spots().size()))
            .arg(service_->remainingSpots())
            .arg(service_->occupiedSpots())
            .arg(QString::fromStdString(result->spotId))
            .arg(result->entryRoute.distance, 0, 'f', 1)
            .arg(result->exitRoute.distance, 0, 'f', 1)
            .arg(result->nearbyOccupiedSpots)
            .arg(result->entryRoute.turnCount + result->exitRoute.turnCount)
            .arg(result->breakdown.typePenalty, 0, 'f', 1)
            .arg(result->score, 0, 'f', 1));
}

void MainWindow::releaseLastVehicle(){
    if (!lastAllocation_){
        QMessageBox::information(this, "没有可释放车辆", "请先自动分配一个车位。");
        return;
    }
    const auto closedRecord = service_->leave(lastAllocation_->plateNumber);
    if (closedRecord){
        const auto duration = std::chrono::duration_cast<std::chrono::minutes>(
            closedRecord->duration());
        statusLabel_->setText(QString("离场完成：%1 | 车位：%2 | 停车时长：%3分钟 | 本次费用：%4元 | 累计收费：%5元 | 总记录：%6")
                                  .arg(QString::fromStdString(closedRecord->plateNumber()))
                                  .arg(QString::fromStdString(closedRecord->spotId()))
                                  .arg(duration.count())
                                  .arg(closedRecord->fee(), 0, 'f', 2)
                                  .arg(service_->totalRevenue(), 0, 'f', 2)
                                  .arg(static_cast<int>(service_->records().size())));
        lastAllocation_.reset();
    } else{
        QMessageBox::warning(this, "离场失败",
                             "无法关闭该车辆的停车记录，分配结果已保留。");
    }
    refreshScene();
    refreshRecords();
    refreshOccupancy();
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

void MainWindow::bookVehicle(){
    const QString plate = plateInput_->text().trimmed();
    if (plate.isEmpty()){
        QMessageBox::information(this, "请输入车牌", "预约前请在上方输入车辆车牌。");
        return;
    }
    const QDateTime arrival = arrivalInput_->dateTime();
    if (arrival <= QDateTime::currentDateTime()){
        QMessageBox::information(this, "到场时间无效", "预约到场时间必须在当前时间之后。");
        return;
    }
    const auto arrivalTime = smartpark::Booking::Clock::time_point(
        std::chrono::seconds(arrival.toSecsSinceEpoch()));
    const smartpark::Vehicle vehicle(
        plate.toStdString(), vehicleTypeFromIndex(vehicleTypeInput_->currentIndex()));
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
    const QString plate = plateInput_->text().trimmed();
    if (plate.isEmpty()){
        QMessageBox::information(this, "请输入车牌", "到场确认前请输入预约时使用的车牌。");
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
    const QString plate = plateInput_->text().trimmed();
    if (plate.isEmpty()){
        QMessageBox::information(this, "请输入车牌", "取消预约前请输入预约时使用的车牌。");
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
