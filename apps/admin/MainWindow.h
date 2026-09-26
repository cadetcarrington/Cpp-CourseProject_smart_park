#pragma once

#include "core/persistence/Persistence.h"
#include "core/service/AuditLogService.h"
#include "core/service/ParkingService.h"
#include "core/service/ParkingInsightEngine.h"
#include "ChartWidgets.h"

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
class QListWidget;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QCloseEvent;
class QSplitter;
class QStackedWidget;
class QTableWidget;

class MainWindow : public QMainWindow{
    Q_OBJECT

public:
    explicit MainWindow(QString databasePath, QWidget *parent = nullptr);
    MainWindow(QString databasePath, QString currentUser, QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void paintEvent(QPaintEvent *event) override;

signals:
    void logoutRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void editLayout();
    bool applyLayout();
    void allocateVehicle();
    void recognizePlateImage();
    void updateVehicleType();
    void releaseVehicle();
    void updateStrategy();
    void bookVehicle();
    void checkInBooking();
    void cancelActiveBooking();
    void queryRecords();
    void resetRecordFilter();
    void changePage(int index);
    void requestLogout();

private:
    void buildUi();
    void refreshScene();
    void refreshBookings();
    void refreshRecords();
    void refreshOccupancy();
    void refreshDashboard();
    void refreshInsights();
    smartpark::ParkingInsights computeInsights() const;
    void fitMapView();
    bool applyService(const smartpark::ParkingLayout &layout,
                      smartpark::AllocationStrategy strategy);
    bool resetDatabase();
    smartpark::AllocationStrategy currentStrategy() const;
    QString activeBookingPlate() const;
    QString databasePath_;
    QString layoutText_;
    QString currentUser_;
    bool databaseFailed_{false};
    bool recordsFilterActive_{false};
    std::unique_ptr<smartpark::Persistence> persistence_;
    std::unique_ptr<smartpark::AuditLogService> auditService_;
    QPixmap glassBackdrop_;
    bool glassMode_{false};
    QPushButton *emergencyButton_{nullptr};
    QLabel *emergencyBanner_{nullptr};
    std::unique_ptr<smartpark::ParkingService> service_;
    std::optional<smartpark::AllocationResult> lastAllocation_;

    QGraphicsScene *scene_{nullptr};
    QGraphicsView *mapView_{nullptr};
    QListWidget *navigation_{nullptr};
    QStackedWidget *pages_{nullptr};
    QSplitter *shellSplitter_{nullptr};
    QLabel *pageTitleLabel_{nullptr};
    QLabel *pageSubtitleLabel_{nullptr};
    QLabel *userLabel_{nullptr};
    QLabel *connectionLabel_{nullptr};
    QLabel *mapSummaryLabel_{nullptr};
    QLabel *dashboardActivityLabel_{nullptr};
    QLabel *kpiTotalLabel_{nullptr};
    QLabel *kpiAvailableLabel_{nullptr};
    QLabel *kpiOccupiedLabel_{nullptr};
    QLabel *kpiReservedLabel_{nullptr};
    QLineEdit *plateInput_{nullptr};
    QPushButton *recognizePlateButton_{nullptr};
    QComboBox *vehicleTypeInput_{nullptr};
    QComboBox *strategyInput_{nullptr};
    QPushButton *allocateButton_{nullptr};
    QPushButton *updateVehicleTypeButton_{nullptr};
    QPushButton *releaseButton_{nullptr};
    QPushButton *layoutButton_{nullptr};
    QLineEdit *bookingPlateInput_{nullptr};
    QComboBox *bookingVehicleTypeInput_{nullptr};
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
    QLabel *dashboardInsightLabel_{nullptr};
    DonutChartWidget *compositionChart_{nullptr};
    DonutChartWidget *typeChart_{nullptr};
    BarChartWidget *flowChart_{nullptr};
    BarChartWidget *zonePressureChart_{nullptr};
    LineChartWidget *forecastChart_{nullptr};
    LineChartWidget *sevenDayRevenueChart_{nullptr};
    LineChartWidget *sevenDayFlowChart_{nullptr};
    QLabel *zoneInsightLabel_{nullptr};
    QLabel *recommendationLabel_{nullptr};
    QLabel *bookingImpactLabel_{nullptr};
    QLabel *recordsTrendLabel_{nullptr};
};
