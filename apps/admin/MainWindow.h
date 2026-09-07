#pragma once

#include "core/persistence/Persistence.h"
#include "core/service/ParkingService.h"

#include <QMainWindow>
#include <QString>

#include <memory>
#include <optional>

class QComboBox;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class MainWindow : public QMainWindow{
public:
    explicit MainWindow(QString databasePath, QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void applyLayout();
    void allocateVehicle();
    void releaseLastVehicle();
    void updateStrategy();

private:
    void buildUi();
    void refreshScene();
    bool applyService(const smartpark::ParkingLayout &layout,
                      smartpark::AllocationStrategy strategy);
    void resetDatabase();
    smartpark::AllocationStrategy currentStrategy() const;
    QString databasePath_;
    bool databaseFailed_{false};
    std::unique_ptr<smartpark::Persistence> persistence_;
    std::unique_ptr<smartpark::ParkingService> service_;
    std::optional<smartpark::AllocationResult> lastAllocation_;
    QGraphicsScene *scene_{nullptr};
    QGraphicsView *mapView_{nullptr};
    QPlainTextEdit *layoutEditor_{nullptr};
    QLineEdit *plateInput_{nullptr};
    QComboBox *vehicleTypeInput_{nullptr};
    QComboBox *strategyInput_{nullptr};
    QPushButton *allocateButton_{nullptr};
    QPushButton *releaseButton_{nullptr};
    QLabel *statusLabel_{nullptr};
    QLabel *billingLabel_{nullptr};
};
