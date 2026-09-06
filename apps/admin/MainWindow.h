#pragma once

#include "core/service/ParkingService.h"

#include <QMainWindow>

#include <memory>
#include <optional>

class QComboBox;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void applyLayout();
    void allocateVehicle();
    void releaseLastVehicle();

private:
    void buildUi();
    void refreshScene();

    std::unique_ptr<smartpark::ParkingService> service_;
    std::optional<smartpark::AllocationResult> lastAllocation_;
    QGraphicsScene *scene_{nullptr};
    QGraphicsView *mapView_{nullptr};
    QPlainTextEdit *layoutEditor_{nullptr};
    QLineEdit *plateInput_{nullptr};
    QComboBox *vehicleTypeInput_{nullptr};
    QPushButton *allocateButton_{nullptr};
    QPushButton *releaseButton_{nullptr};
    QLabel *statusLabel_{nullptr};
};
