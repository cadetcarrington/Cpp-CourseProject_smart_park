#pragma once

#include "core/persistence/Persistence.h"
#include "core/service/AuditLogService.h"
#include "core/service/ParkingService.h"
#include "core/service/ParkingInsightEngine.h"
#include "ChartWidgets.h"

#include <QDate>
#include <QJsonObject>
#include <QMainWindow>
#include <QString>
#include <QTimer>

#include <memory>
#include <optional>

class QComboBox;
class QDateTimeEdit;
class QFrame;
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

namespace smartpark{
class ServerSession;
}

namespace smartpark_ui{
class DanmakuOverlay;
}

class MainWindow : public QMainWindow{
    Q_OBJECT

public:
    explicit MainWindow(QString databasePath, QWidget *parent = nullptr);
    MainWindow(QString databasePath, QString currentUser, QWidget *parent = nullptr);
    // 远程服务端模式：停车状态以 TCP 服务端为唯一权威（快照 + 事件驱动刷新）。
    MainWindow(QString serverHost, quint16 serverPort, QString currentUser,
               QString serverPassword, QWidget *parent = nullptr);
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
    void applyPageVibrancy();
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

    // ---- 远程服务端模式 ----
    void startRemoteSession(const QString &password);
    void requestSnapshot();
    void requestAnalytics();
    void applySnapshot();
    void updateConnectionBadge();
    void setRemoteActionsEnabled(bool enabled);
    void renderMapFromSnapshot();
    // 大屏联动：事件弹幕 + 关联车位闪烁（docs/rest-api.md 事件广播的展示层）。
    void announceEvent(const QString &event, const QJsonObject &payload);
    void flashSpot(const QString &spotId);
    void tickFlashes();
    void refreshOccupancyFromSnapshot();
    void refreshDashboardFromSnapshot();
    void applyRemoteInsights();
    void allocateVehicleRemote();
    void releaseVehicleRemote();

    QString databasePath_;
    QString layoutText_;
    QString currentUser_;
    bool databaseFailed_{false};
    bool recordsFilterActive_{false};
    std::unique_ptr<smartpark::Persistence> persistence_;
    std::unique_ptr<smartpark::AuditLogService> auditService_;
    QPixmap glassBackdrop_;
    bool glassMode_{false};
    bool vibrancyActive_{false};
    QPushButton *emergencyButton_{nullptr};
    QLabel *emergencyBanner_{nullptr};
    std::unique_ptr<smartpark::ParkingService> service_;
    std::optional<smartpark::AllocationResult> lastAllocation_;

    // 远程模式状态：服务端快照为唯一数据源；事件触发去抖刷新。
    bool remoteMode_{false};
    QString serverHost_;
    quint16 serverPort_{0};
    smartpark::ServerSession *session_{nullptr};
    QTimer snapshotDebounceTimer_;
    QTimer revenueDateTimer_;
    QDate revenueDate_;
    quint64 snapshotRequestId_{0};
    QJsonObject snapshot_;
    QJsonObject analyticsReport_;
    QFrame *forecastCard_{nullptr};
    QFrame *flowCard_{nullptr};
    QFrame *revenueCard_{nullptr};
    QFrame *flow7Card_{nullptr};
    QPushButton *goBookingsButton_{nullptr};

    QGraphicsScene *scene_{nullptr};
    QGraphicsView *mapView_{nullptr};
    smartpark_ui::DanmakuOverlay *danmaku_{nullptr};
    QTimer spotFlashTimer_;
    QHash<QString, qint64> spotFlashUntilMs_;
    QListWidget *navigation_{nullptr};
    QStackedWidget *pages_{nullptr};
    QSplitter *shellSplitter_{nullptr};
    QFrame *sideBar_{nullptr};
    QFrame *topHeader_{nullptr};
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
    QWidget *bookingsHost_{nullptr};
    void *nativeBookingsHandle_{nullptr};
    QTableWidget *occupancyTable_{nullptr};
    QWidget *recordsHost_{nullptr};
    void *nativeRecordsHandle_{nullptr};
    QTableWidget *recordsTable_{nullptr};
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
