#include "MainWindow.h"

#include <QComboBox>
#include <QColor>
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

namespace {

smartpark::VehicleType vehicleTypeFromIndex(int index)
{
    switch (index) {
    case 1: return smartpark::VehicleType::Motorcycle;
    case 2: return smartpark::VehicleType::Truck;
    case 3: return smartpark::VehicleType::Electric;
    default: return smartpark::VehicleType::Car;
    }
}

QPolygonF routePolygon(const smartpark::Route &route)
{
    QPolygonF polygon;
    for (const smartpark::Point &point : route.points) {
        polygon << QPointF(point.x, point.y);
    }
    return polygon;
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    service_ = std::make_unique<smartpark::ParkingService>(smartpark::ParkingLayout::defaultLayout());
    buildUi();
    refreshScene();
}

void MainWindow::buildUi()
{
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
        "region A 5 8 10 2 1.2 5.5 6 left\n"
        "region B 38 24 10 2 1.4 6.0 6 right\n"
        "region C 71 40 10 2 1.2 5.5 6 left\n");
    layoutEditor_->setMinimumWidth(280);
    layoutLayout->addWidget(new QLabel(
        "格式：site 宽 高 / entrance x y / exit x y /\\n"
        "region 名称 x y 行数 列数 车位宽 车位长 通道宽 left|right", layoutGroup));
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
    allocateButton_ = new QPushButton("自动分配车位", controlBar);
    releaseButton_ = new QPushButton("释放最近车位", controlBar);
    controlLayout->addWidget(plateInput_, 1);
    controlLayout->addWidget(vehicleTypeInput_);
    controlLayout->addWidget(allocateButton_);
    controlLayout->addWidget(releaseButton_);

    statusLabel_ = new QLabel(centralWidget);
    statusLabel_->setWordWrap(true);

    rootLayout->addWidget(splitter, 1);
    rootLayout->addWidget(controlBar);
    rootLayout->addWidget(statusLabel_);
    setCentralWidget(centralWidget);

    connect(allocateButton_, &QPushButton::clicked, this, &MainWindow::allocateVehicle);
    connect(releaseButton_, &QPushButton::clicked, this, &MainWindow::releaseLastVehicle);
}

void MainWindow::refreshScene()
{
    scene_->clear();
    const smartpark::ParkingLayout &layout = service_->layout();
    scene_->setSceneRect(0.0, 0.0, layout.siteWidth(), layout.siteHeight());
    scene_->addRect(0.0, 0.0, layout.siteWidth(), layout.siteHeight(),
                    QPen(Qt::darkGray, 0.15), QBrush(QColor(245, 247, 250)));

    for (const smartpark::Rectangle &region : layout.regions()) {
        scene_->addRect(region.origin.x, region.origin.y, region.width, region.height,
                        QPen(QColor(150, 160, 175), 0.12, Qt::DashLine));
    }

    for (const smartpark::ParkingSpot &spot : service_->spots()) {
        const bool available = spot.isAvailable();
        const QColor color = available ? QColor(88, 182, 124) : QColor(226, 92, 92);
        auto *item = scene_->addRect(spot.bounds().origin.x, spot.bounds().origin.y,
                                     spot.bounds().width, spot.bounds().height,
                                     QPen(Qt::black, 0.08), QBrush(color));
        item->setToolTip(QString("%1 | %2 | %3")
                             .arg(QString::fromStdString(spot.identifier()))
                             .arg(available ? "空闲" : "占用")
                             .arg(available ? "-" : QString::fromStdString(
                                                    spot.parkedVehicle()->plateNumber())));
    }

    auto *entrance = scene_->addEllipse(layout.entrance().x - 1.0, layout.entrance().y - 1.0,
                                        2.0, 2.0, QPen(Qt::darkBlue, 0.15),
                                        QBrush(Qt::darkBlue));
    entrance->setToolTip("入口");
    auto *exit = scene_->addEllipse(layout.exit().x - 1.0, layout.exit().y - 1.0,
                                    2.0, 2.0, QPen(QColor(180, 80, 0), 0.15),
                                    QBrush(QColor(180, 80, 0)));
    exit->setToolTip("出口");

    if (lastAllocation_) {
        scene_->addPolygon(routePolygon(lastAllocation_->entryRoute),
                           QPen(Qt::blue, 0.28));
        scene_->addPolygon(routePolygon(lastAllocation_->exitRoute),
                           QPen(QColor(230, 132, 0), 0.22, Qt::DashLine));
    }

    const auto availableCount = static_cast<int>(std::count_if(
        service_->spots().begin(), service_->spots().end(),
        [](const smartpark::ParkingSpot &spot) { return spot.isAvailable(); }));
    statusLabel_->setText(QString("总车位：%1 / 空闲：%2")
                              .arg(static_cast<int>(service_->spots().size()))
                              .arg(availableCount));
}

void MainWindow::applyLayout()
{
    try {
        service_ = std::make_unique<smartpark::ParkingService>(
            smartpark::ParkingLayout::fromDescription(layoutEditor_->toPlainText().toStdString()));
        lastAllocation_.reset();
        refreshScene();
    } catch (const std::exception &error) {
        QMessageBox::warning(this, "布局错误", error.what());
    }
}

void MainWindow::allocateVehicle()
{
    const QString plate = plateInput_->text().trimmed();
    if (plate.isEmpty()) {
        QMessageBox::information(this, "请输入车牌", "自动分配前请输入车辆车牌。");
        return;
    }

    const smartpark::Vehicle vehicle(plate.toStdString(),
                                     vehicleTypeFromIndex(vehicleTypeInput_->currentIndex()));
    const std::optional<smartpark::AllocationResult> result = service_->allocate(vehicle);
    if (!result) {
        QMessageBox::warning(this, "无可用车位", "当前停车场已满或没有可达车位。");
        return;
    }

    lastAllocation_ = result;
    refreshScene();
    const auto availableCount = static_cast<int>(std::count_if(
        service_->spots().begin(), service_->spots().end(),
        [](const smartpark::ParkingSpot &spot) { return spot.isAvailable(); }));
    statusLabel_->setText(QString("总车位：%1 / 空闲：%2 | 已分配：%3 | 入口距离：%4m | 出口距离：%5m | 综合评分：%6")
                              .arg(static_cast<int>(service_->spots().size()))
                              .arg(availableCount)
                              .arg(QString::fromStdString(result->spotId))
                              .arg(result->entryRoute.distance, 0, 'f', 1)
                              .arg(result->exitRoute.distance, 0, 'f', 1)
                              .arg(result->score, 0, 'f', 1));
}

void MainWindow::releaseLastVehicle()
{
    if (!lastAllocation_) {
        QMessageBox::information(this, "没有可释放车辆", "请先自动分配一个车位。");
        return;
    }

    service_->release(lastAllocation_->spotId);
    lastAllocation_.reset();
    refreshScene();
}
