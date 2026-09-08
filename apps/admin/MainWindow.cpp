#include "MainWindow.h"
#include <QComboBox>
#include <QColor>
#include <QFile>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>
#include <utility>
namespace{
smartpark::VehicleType vehicleTypeFromIndex(int index){
    switch (index){
    case 1: return smartpark::VehicleType::Motorcycle;
    case 2: return smartpark::VehicleType::Truck;
    case 3: return smartpark::VehicleType::Electric;
    default: return smartpark::VehicleType::Car;
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
QColor statusColor(smartpark::SpotStatus status){
    switch (status){
    case smartpark::SpotStatus::Reserved:
        return QColor(237, 201, 72);
    case smartpark::SpotStatus::Occupied:
        return QColor(226, 92, 92);
    case smartpark::SpotStatus::Disabled:
        return QColor(160, 160, 160);
    case smartpark::SpotStatus::Available:
    default:
        return QColor(88, 182, 124);
    }
}
QPolygonF routePolygon(const smartpark::Route &route){
    QPolygonF polygon;
    for (const smartpark::Point &point : route.points){
        polygon << QPointF(point.x, point.y);
    }
    return polygon;
}
} // namespace
MainWindow::MainWindow(QString databasePath, QWidget *parent)
    : QMainWindow(parent)
    , databasePath_(std::move(databasePath)){
    const bool persistenceActive = !databasePath_.isEmpty()
        && applyService(smartpark::ParkingLayout::defaultLayout(),
                        smartpark::AllocationStrategy::WeightedCost);
    if (!persistenceActive){
        // 安全降级：先释放可能指向旧持久化的 service_，再进入内存模式。
        service_.reset();
        persistence_.reset();
        service_ = std::make_unique<smartpark::ParkingService>(
            smartpark::ParkingLayout::defaultLayout());
    }
    buildUi();
    refreshScene();
    if (!persistenceActive){
        statusLabel_->setText(
            databaseFailed_
                ? "数据库重置失败，已降级为内存模式：重启后数据不会保留。"
                : "数据库未启用或恢复被取消，当前为内存模式，重启后数据不会保留。");
    }
}
void MainWindow::buildUi(){
    setWindowTitle("SmartPark 管理员端");
    resize(1200, 800);
    auto *centralWidget = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(centralWidget);
    auto *splitter = new QSplitter(Qt::Horizontal, centralWidget);
    auto *layoutGroup = new QGroupBox("自定义停车场布局", splitter);
    auto *layoutLayout = new QVBoxLayout(layoutGroup);
    layoutEditor_ = new QPlainTextEdit(layoutGroup);
    layoutEditor_->setPlainText(
        "site 100 60\n"
        "entrance 0 30\n"
        "exit 100 30\n"
        "region A 5 8 10 2 1.2 5.5 6 left normal\n"
        "region B 38 24 10 2 1.4 6.0 6 right charging\n"
        "region C 71 40 10 2 1.2 5.5 6 left accessible\n");
    layoutEditor_->setMinimumWidth(280);
    layoutLayout->addWidget(new QLabel(
        "格式：site 宽 高\n"
        "entrance x y / exit x y，可写多条\n"
        "region 名称 x y 行数 列数 车位宽 车位长 通道宽 left|right [normal|charging|accessible|vip]",
        layoutGroup));
    layoutLayout->addWidget(layoutEditor_);
    auto *applyButton = new QPushButton("应用布局", layoutGroup);
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::applyLayout);
    layoutLayout->addWidget(applyButton);
    auto *mapGroup = new QGroupBox("实时车位分配与路线", splitter);
    auto *mapLayout = new QVBoxLayout(mapGroup);
    scene_ = new QGraphicsScene(mapGroup);
    mapView_ = new QGraphicsView(scene_, mapGroup);
    mapView_->setRenderHint(QPainter::Antialiasing);
    mapView_->setMinimumSize(780, 540);
    mapLayout->addWidget(mapView_);
    splitter->addWidget(layoutGroup);
    splitter->addWidget(mapGroup);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    auto *controlBar = new QWidget(centralWidget);
    auto *controlLayout = new QHBoxLayout(controlBar);
    plateInput_ = new QLineEdit(controlBar);
    plateInput_->setPlaceholderText("车牌，例如：晋A12345");
    vehicleTypeInput_ = new QComboBox(controlBar);
    vehicleTypeInput_->addItems({"轿车", "摩托车", "卡车", "电动车"});
    strategyInput_ = new QComboBox(controlBar);
    strategyInput_->addItems({"加权代价", "最近车位"});
    allocateButton_ = new QPushButton("自动分配车位", controlBar);
    releaseButton_ = new QPushButton("释放最近车位", controlBar);
    controlLayout->addWidget(plateInput_, 1);
    controlLayout->addWidget(vehicleTypeInput_);
    controlLayout->addWidget(strategyInput_);
    controlLayout->addWidget(allocateButton_);
    controlLayout->addWidget(releaseButton_);
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
    rootLayout->addWidget(splitter, 1);
    rootLayout->addWidget(controlBar);
    rootLayout->addWidget(billingLabel_);
    rootLayout->addWidget(statusLabel_);
    setCentralWidget(centralWidget);
    connect(allocateButton_, &QPushButton::clicked, this, &MainWindow::allocateVehicle);
    connect(releaseButton_, &QPushButton::clicked, this, &MainWindow::releaseLastVehicle);
    connect(strategyInput_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::updateStrategy);
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
        // 终态错误：数据库已不可用，后续一律使用内存模式，保证 service_ 非空。
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
            // 先释放 service_（其持有 repository_ 指针），再释放 Persistence，
            // 避免悬空指针；成功后重建两者。
            service_.reset();
            resetDatabase();
            persistence_ = std::make_unique<smartpark::Persistence>(databasePath_);
            service_ = std::make_unique<smartpark::ParkingService>(
                layout, strategy, &persistence_->repository());
            return true;
        } catch (const std::exception &resetError){
            QMessageBox::warning(this, "数据库重置失败",
                                 QString::fromUtf8(resetError.what()));
            // 终态降级：立即转为内存模式，service_ 永不为空，禁止后续空指针。
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
    scene_->setSceneRect(0.0, 0.0, layout.siteWidth(), layout.siteHeight());
    scene_->addRect(0.0, 0.0, layout.siteWidth(), layout.siteHeight(),
                    QPen(Qt::darkGray, 0.15), QBrush(QColor(245, 247, 250)));
    for (const smartpark::Rectangle &region : layout.regions()){
        scene_->addRect(region.origin.x, region.origin.y, region.width, region.height,
                        QPen(QColor(150, 160, 175), 0.12, Qt::DashLine));
    }
    for (const smartpark::ParkingSpot &spot : service_->spots()){
        auto *item = scene_->addRect(spot.bounds().origin.x, spot.bounds().origin.y,
                                     spot.bounds().width, spot.bounds().height,
                                     QPen(Qt::black, 0.08), QBrush(statusColor(spot.status())));
        const QString plate = spot.parkedVehicle()
            ? QString::fromStdString(spot.parkedVehicle()->plateNumber())
            : QStringLiteral("-");
        item->setToolTip(QString("%1 | %2 | %3 | %4")
                             .arg(QString::fromStdString(spot.identifier()))
                             .arg(QString::fromUtf8(toString(spot.type())))
                             .arg(statusText(spot.status()))
                             .arg(plate));
    }
    for (const smartpark::Point &entrance : layout.entrances()){
        auto *item = scene_->addEllipse(entrance.x - 1.0, entrance.y - 1.0, 2.0, 2.0,
                                        QPen(Qt::darkBlue, 0.15), QBrush(Qt::darkBlue));
        item->setToolTip(QStringLiteral("入口"));
    }
    for (const smartpark::Point &exit : layout.exits()){
        auto *item = scene_->addEllipse(exit.x - 1.0, exit.y - 1.0, 2.0, 2.0,
                                        QPen(QColor(180, 80, 0), 0.15),
                                        QBrush(QColor(180, 80, 0)));
        item->setToolTip(QStringLiteral("出口"));
    }
    if (lastAllocation_){
        scene_->addPolygon(routePolygon(lastAllocation_->entryRoute),
                           QPen(Qt::blue, 0.28));
        scene_->addPolygon(routePolygon(lastAllocation_->exitRoute),
                           QPen(QColor(230, 132, 0), 0.22, Qt::DashLine));
    }
    statusLabel_->setText(QString("总车位：%1 / 空闲：%2 / 占用：%3 / 预留：%4")
                              .arg(static_cast<int>(service_->spots().size()))
                              .arg(service_->remainingSpots())
                              .arg(service_->occupiedSpots())
                              .arg(service_->reservedSpots()));
}
void MainWindow::applyLayout(){
    std::optional<smartpark::ParkingLayout> layout;
    try{
        layout = smartpark::ParkingLayout::fromDescription(
            layoutEditor_->toPlainText().toStdString());
    } catch (const std::exception &error){
        QMessageBox::warning(this, "布局错误", QString::fromUtf8(error.what()));
        return;
    }
    const bool persisted = applyService(*layout, currentStrategy());
    if (service_){
        lastAllocation_.reset();
        refreshScene();
    }
    if (databaseFailed_){
        statusLabel_->setText(
            "数据库重置失败，已降级为内存模式：新布局仅保存在内存，重启后不会保留。");
    } else if (!persisted){
        statusLabel_->setText("已取消数据库恢复，保留原有布局与数据。");
    }
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
}
