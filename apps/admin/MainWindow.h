#pragma once

#include "core/persistence/Persistence.h"
#include "core/service/ParkingService.h"

#include <QMainWindow>
#include <QString>

#include <memory>
#include <optional>

class QComboBox;
class QDateTimeEdit;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QTableWidget;

class MainWindow : public QMainWindow{
public:
    explicit MainWindow(QString databasePath, QWidget *parent = nullptr);
    ~MainWindow() override = default;

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void editLayout();
    bool applyLayout();
    void allocateVehicle();
    void releaseLastVehicle();
    void updateStrategy();
    void bookVehicle();
    void checkInBooking();
    void cancelActiveBooking();
    void queryRecords();
    void resetRecordFilter();

private:
    void buildUi();
    void refreshScene();
    void refreshBookings();
    void refreshRecords();
    void refreshOccupancy();
    void fitMapView();
    bool applyService(const smartpark::ParkingLayout &layout,
                      smartpark::AllocationStrategy strategy);
    void resetDatabase();
    smartpark::AllocationStrategy currentStrategy() const;
    QString databasePath_;
    QString layoutText_;
    bool databaseFailed_{false};
    bool recordsFilterActive_{false};
    std::unique_ptr<smartpark::Persistence> persistence_;
    std::unique_ptr<smartpark::ParkingService> service_;
    std::optional<smartpark::AllocationResult> lastAllocation_;
    QGraphicsScene *scene_{nullptr};
    QGraphicsView *mapView_{nullptr};
    QLineEdit *plateInput_{nullptr};
    QComboBox *vehicleTypeInput_{nullptr};
    QComboBox *strategyInput_{nullptr};
    QPushButton *allocateButton_{nullptr};
    QPushButton *releaseButton_{nullptr};
    QPushButton *layoutButton_{nullptr};
    QDateTimeEdit *arrivalInput_{nullptr};
    QPushButton *bookButton_{nullptr};
    QPushButton *checkInButton_{nullptr};
    QPushButton *cancelBookingButton_{nullptr};
    QTableWidget *bookingsTable_{nullptr};
    QTableWidget *recordsTable_{nullptr};
    QTableWidget *occupancyTable_{nullptr};
    QDateTimeEdit *recordFromInput_{nullptr};
    QDateTimeEdit *recordToInput_{nullptr};
    QLabel *depositLabel_{nullptr};
    QLabel *recordsLabel_{nullptr};
    QLabel *statusLabel_{nullptr};
    QLabel *billingLabel_{nullptr};
};
