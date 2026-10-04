// 远程模式集成测试：进程内起真实 TCP 服务端（临时库 + 临时端口），
// 覆盖 ServerSession 登录/快照/权限、MainWindow 远程模式镜像服务端
// 状态（含 Gate 侧事件驱动刷新与 Admin 侧入场/离场）、服务端重启重连。
#include "MainWindow.h"
#include "ChartWidgets.h"
#include "ServerSession.h"

#include "core/persistence/Persistence.h"
#include "core/service/AuditLogService.h"
#include "core/service/ParkingService.h"
#include "core/service/UserStore.h"
#include "network/SmartParkTcpServer.h"
#include "network/TcpClient.h"

#include <QtTest/qtest.h>
#include <QComboBox>
#include <QEventLoop>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>

#include <functional>

namespace{
const char *kAdminUser = "admin";
const char *kAdminPass = "smartpark";
const char *kGateUser = "gate";
const char *kGatePass = "smartpark";

// 驱动事件循环直到条件满足或超时（默认 8 秒）。
bool waitFor(const std::function<bool()> &condition, int timeoutMs = 8000){
    const auto deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (!condition()){
        if (QDateTime::currentMSecsSinceEpoch() > deadline){
            return false;
        }
        QTest::qWait(50);
    }
    return true;
}

smartpark::TcpClient *loginClient(const QString &user, const QString &pass,
                                  quint16 port){
    auto *client = new smartpark::TcpClient;
    client->connectToHost(QStringLiteral("127.0.0.1"), port);
    const auto reply = client->request(
        QStringLiteral("login"),
        QJsonObject{{QStringLiteral("user"), user},
                    {QStringLiteral("pass"), pass}});
    Q_ASSERT(reply.has_value() && reply->value(QStringLiteral("ok")).toBool());
    client->setToken(reply->value(QStringLiteral("payload")).toObject()
                         .value(QStringLiteral("token")).toString());
    return client;
}
} // namespace

class AdminRemoteTests : public QObject{
    Q_OBJECT

    QTemporaryDir databaseDir_;
    std::unique_ptr<smartpark::Persistence> persistence_;
    std::unique_ptr<smartpark::AuditLogService> audit_;
    std::unique_ptr<smartpark::UserStore> users_;
    std::unique_ptr<smartpark::ParkingService> service_;
    std::unique_ptr<smartpark::SmartParkTcpServer> server_;

    quint16 port_{0};

    void startServer(quint16 requestedPort = 0){
        persistence_ = std::make_unique<smartpark::Persistence>(
            databaseDir_.filePath("remote.db"));
        audit_ = std::make_unique<smartpark::AuditLogService>(
            persistence_->databaseManager().database());
        users_ = std::make_unique<smartpark::UserStore>(
            databaseDir_.filePath("users.db"));
        service_ = std::make_unique<smartpark::ParkingService>(
            smartpark::ParkingLayout::defaultLayout(),
            smartpark::AllocationStrategy::WeightedCost,
            &persistence_->repository());
        smartpark::SmartParkTcpServer::Options options;
        options.port = requestedPort;
        options.heartbeatTimeoutMs = 600000;
        server_ = std::make_unique<smartpark::SmartParkTcpServer>(
            *service_, audit_.get(), users_.get(), options);
        QVERIFY(server_->listen());
        port_ = server_->port();
        QVERIFY(port_ != 0);
    }

private slots:
    void init(){
        if (!persistence_){
            startServer();
        }
    }

    // ServerSession：登录、快照权限（仅 admin）、事件广播接收。
    void serverSessionSnapshotAndAuthorization();
    // 错误口令：authFailed 恰好一次，且不进入重连循环。
    void serverSessionWrongPasswordDoesNotReconnect();
    // MainWindow 远程模式：镜像服务端状态并响应 Gate 与本端操作。
    void adminWindowMirrorsServerState();
    // 服务端重启后自动重连并恢复快照。
    void adminWindowReconnectsAfterServerRestart();
    void snapshotRevenueUsesExitDatesAndSurvivesRestart();
};

void AdminRemoteTests::serverSessionSnapshotAndAuthorization(){
    smartpark::ServerSession session;
    QJsonObject snapshot;
    bool snapshotOk = false;
    session.start(QStringLiteral("127.0.0.1"), port_,
                  QString::fromLatin1(kAdminUser), QString::fromLatin1(kAdminPass));
    QVERIFY(waitFor([&]{
        return session.state()
            == smartpark::ServerSession::State::Online;
    }));
    QEventLoop loop;
    session.request(QStringLiteral("admin.snapshot"), {},
                    [&](bool ok, const QString &, const QJsonObject &payload){
        snapshotOk = ok;
        snapshot = payload;
        loop.quit();
    });
    loop.exec();
    QVERIFY(snapshotOk);
    QCOMPARE(snapshot.value(QStringLiteral("capacity")).toInt(), 60);
    QCOMPARE(snapshot.value(QStringLiteral("spots")).toArray().size(), 60);
    QVERIFY(!snapshot.value(QStringLiteral("layout")).toObject()
                 .value(QStringLiteral("entrances")).toArray().isEmpty());
    const QJsonArray dailyRevenue = snapshot.value(QStringLiteral("dailyRevenue")).toArray();
    QCOMPARE(dailyRevenue.size(), 7);
    for (int i = 0; i < dailyRevenue.size(); ++i){
        const QJsonObject day = dailyRevenue.at(i).toObject();
        QCOMPARE(day.value(QStringLiteral("date")).toString(),
                 QDate::currentDate().addDays(i - 6).toString(Qt::ISODate));
        QCOMPARE(day.value(QStringLiteral("fee")).toDouble(), 0.0);
    }

    // Gate 会话禁止快照；admin 会话禁止补报。
    smartpark::ServerSession gateSession;
    gateSession.start(QStringLiteral("127.0.0.1"), port_,
                      QString::fromLatin1(kGateUser),
                      QString::fromLatin1(kGatePass));
    QVERIFY(waitFor([&]{
        return gateSession.state()
            == smartpark::ServerSession::State::Online;
    }));
    QString gateError;
    bool gateSnapshotOk = true;
    QEventLoop gateLoop;
    gateSession.request(QStringLiteral("admin.snapshot"), {},
                        [&](bool ok, const QString &error, const QJsonObject &){
        gateSnapshotOk = ok;
        gateError = error;
        gateLoop.quit();
    });
    gateLoop.exec();
    QVERIFY(!gateSnapshotOk);
    QVERIFY(gateError.contains(QStringLiteral("仅管理员")));

    QString adminError;
    QEventLoop adminLoop;
    session.request(QStringLiteral("gate.replay"),
                    QJsonObject{{QStringLiteral("events"), QJsonArray{} }},
                    [&](bool ok, const QString &error, const QJsonObject &){
        adminError = error;
        adminLoop.quit();
    });
    adminLoop.exec();
    QVERIFY(adminError.contains(QStringLiteral("仅 Gate")));
}

void AdminRemoteTests::snapshotRevenueUsesExitDatesAndSurvivesRestart(){
    using namespace std::chrono_literals;
    const QDate today = QDate::currentDate();
    const auto atNoon = [](QDate date){
        return smartpark::ParkingRecord::TimePoint{}
            + std::chrono::milliseconds(QDateTime(date, QTime(12, 0))
                                            .toMSecsSinceEpoch());
    };
    const auto settle = [&](const std::string &plate, QDate exitDate,
                            std::chrono::milliseconds stay){
        const auto exit = atNoon(exitDate);
        QVERIFY(service_->enter({plate, smartpark::VehicleType::Car}, exit - stay)
                    .has_value());
        QVERIFY(service_->leave(plate, exit).has_value());
    };
    settle(u8"晋A10001", today.addDays(-6), 90min);
    settle(u8"晋A10002", today.addDays(-1), 3h);
    settle(u8"晋A10003", today, 45min);
    settle(u8"晋A10004", today.addDays(-7), 90min);
    settle(u8"晋A10005", today.addDays(1), 90min);

    const auto readRevenue = [&]{
        std::unique_ptr<smartpark::TcpClient> client(loginClient(
            QString::fromLatin1(kAdminUser), QString::fromLatin1(kAdminPass), port_));
        const auto response = client->request(QStringLiteral("admin.snapshot"), {});
        if (!response || !response->value(QStringLiteral("ok")).toBool()){
            return QJsonArray{};
        }
        return response->value(QStringLiteral("payload")).toObject()
            .value(QStringLiteral("dailyRevenue")).toArray();
    };
    const auto verifyRevenue = [&](const QJsonArray &days){
        QCOMPARE(days.size(), 7);
        if (days.size() != 7){
            return;
        }
        for (int i = 0; i < 7; ++i){
            const QJsonObject day = days.at(i).toObject();
            QCOMPARE(day.value(QStringLiteral("date")).toString(),
                     today.addDays(i - 6).toString(Qt::ISODate));
            const double expected = i == 0 ? 10.0 : i == 5 ? 25.0 : i == 6 ? 5.0 : 0.0;
            QCOMPARE(day.value(QStringLiteral("fee")).toDouble(), expected);
        }
    };
    verifyRevenue(readRevenue());
    server_.reset();
    service_.reset();
    audit_.reset();
    users_.reset();
    persistence_.reset();
    startServer();
    verifyRevenue(readRevenue());
}

void AdminRemoteTests::serverSessionWrongPasswordDoesNotReconnect(){
    smartpark::ServerSession session;
    QSignalSpy authSpy(&session, &smartpark::ServerSession::authFailed);
    session.start(QStringLiteral("127.0.0.1"), port_,
                  QString::fromLatin1(kAdminUser), QStringLiteral("wrong-pass"));
    QVERIFY(waitFor([&]{
        return !authSpy.isEmpty();
    }));
    QVERIFY(!authSpy.first().first().toString().isEmpty());
    // 超过首个重连周期后仍应保持 Disconnected：口令错误不重试。
    QTest::qWait(1800);
    QCOMPARE(authSpy.count(), 1);
    QVERIFY(session.state() == smartpark::ServerSession::State::Disconnected);
}

void AdminRemoteTests::adminWindowMirrorsServerState(){
    MainWindow window(QStringLiteral("127.0.0.1"), port_,
                      QString::fromLatin1(kAdminUser),
                      QString::fromLatin1(kAdminPass));
    // KPI 数值标签共用 objectName "metricValue"，按创建顺序取：
    // 0=总车位 1=空闲 2=已占用 3=有效预约。
    const auto metricValues = window.findChildren<QLabel *>("metricValue");
    QVERIFY(metricValues.size() >= 4);
    auto *kpiTotal = metricValues.at(0);
    auto *kpiOccupied = metricValues.at(2);
    auto *occupancyTable = window.findChild<QTableWidget *>("occupancyTable");
    auto *plateInput = window.findChild<QLineEdit *>("plateInput");
    auto *allocateButton = window.findChild<QPushButton *>("allocateButton");
    auto *releaseButton = window.findChild<QPushButton *>("releaseButton");
    QVERIFY(kpiTotal && kpiOccupied && occupancyTable && plateInput);
    QVERIFY(allocateButton && releaseButton);

    // 快照到达：60 车位全部可见。
    QVERIFY(waitFor([&]{ return kpiTotal->text() == QStringLiteral("60"); }));
    QCOMPARE(occupancyTable->rowCount(), 60);
    auto *revenueChart = dynamic_cast<LineChartWidget *>(
        window.findChild<QWidget *>("sevenDayRevenueChart"));
    QVERIFY(revenueChart);
    QVERIFY(waitFor([&]{
        return revenueChart->series().size() == 1
            && revenueChart->series().first().points.size() == 7;
    }));

    // Gate 侧入场 → 事件广播 → Admin 去抖刷新。
    const QString plate = QStringLiteral("京Z00001");
    smartpark::TcpClient *gate = loginClient(QString::fromLatin1(kGateUser),
                                             QString::fromLatin1(kGatePass),
                                             port_);
    const auto enter = gate->request(
        QStringLiteral("parking.enter"),
        QJsonObject{{QStringLiteral("plate"), plate},
                    {QStringLiteral("vehicleType"), QStringLiteral("car")}});
    QVERIFY(enter.has_value() && enter->value(QStringLiteral("ok")).toBool());
    QVERIFY(waitFor([&]{ return kpiOccupied->text() == QStringLiteral("1"); }));
    delete gate;

    // Admin 侧入场与离场：走同一服务端，操作后状态立即回读。
    // 注意 statusLabel 会被随后的快照渲染覆盖，改用车位表回读验证。
    const auto rowForPlate = [](QTableWidget *table, const QString &plateText){
        for (int row = 0; row < table->rowCount(); ++row){
            if (table->item(row, 3)
                && table->item(row, 3)->text() == plateText){
                return true;
            }
        }
        return false;
    };
    plateInput->setText(QStringLiteral("京Z00002"));
    allocateButton->click();
    QVERIFY(waitFor([&]{ return kpiOccupied->text() == QStringLiteral("2"); }));
    QVERIFY(waitFor([&]{ return rowForPlate(occupancyTable,
                                             QStringLiteral("京Z00002")); }));

    releaseButton->click();
    QVERIFY(waitFor([&]{ return kpiOccupied->text() == QStringLiteral("1"); }));
    QVERIFY(waitFor([&]{ return !rowForPlate(occupancyTable,
                                              QStringLiteral("京Z00002")); }));
    QCOMPARE(revenueChart->series().first().points.last().value, 0.0);
}

void AdminRemoteTests::adminWindowReconnectsAfterServerRestart(){
    MainWindow window(QStringLiteral("127.0.0.1"), port_,
                      QString::fromLatin1(kAdminUser),
                      QString::fromLatin1(kAdminPass));
    auto *connection = window.findChild<QLabel *>("connectionBadge");
    auto *revenueChart = dynamic_cast<LineChartWidget *>(
        window.findChild<QWidget *>("sevenDayRevenueChart"));
    const auto metricValues = window.findChildren<QLabel *>("metricValue");
    QVERIFY(connection && revenueChart && metricValues.size() >= 4);
    auto *kpiTotal = metricValues.at(0);
    QVERIFY(waitFor([&]{ return kpiTotal->text() == QStringLiteral("60"); }));
    QVERIFY(waitFor([&]{
        return revenueChart->series().size() == 1
            && revenueChart->series().first().points.size() == 7;
    }));

    // 重启服务端（同库同布局同端口）：旧连接断开后按退避自动重连。
    const quint16 previousPort = port_;
    server_.reset();
    QVERIFY(waitFor([&]{
        return connection->text().contains(QStringLiteral("断开"));
    }, 15000));
    QVERIFY(revenueChart->series().isEmpty());
    startServer(previousPort);
    QVERIFY(waitFor([&]{
        return connection->text().contains(QStringLiteral("已连接"));
    }, 15000));
    QVERIFY(waitFor([&]{
        return revenueChart->series().size() == 1
            && revenueChart->series().first().points.size() == 7;
    }));
    const auto metricValuesAfter = window.findChildren<QLabel *>("metricValue");
    QVERIFY(metricValuesAfter.size() >= 4);
    // 重启后快照恢复持久化状态：重启前的京Z00001 仍占用（先等恢复，
    // 避免"恰好还是旧值"的空洞断言），再入场一辆断言占用增长。
    QVERIFY(waitFor([&]{
        return metricValuesAfter.at(2)->text() == QStringLiteral("1");
    }));
    smartpark::TcpClient *gate = loginClient(QString::fromLatin1(kGateUser),
                                             QString::fromLatin1(kGatePass),
                                             port_);
    const auto enter = gate->request(
        QStringLiteral("parking.enter"),
        QJsonObject{{QStringLiteral("plate"), QStringLiteral("京Z00003")},
                    {QStringLiteral("vehicleType"), QStringLiteral("car")}});
    QVERIFY(enter.has_value() && enter->value(QStringLiteral("ok")).toBool());
    delete gate;
    QVERIFY(waitFor([&]{
        return metricValuesAfter.at(2)->text() == QStringLiteral("2");
    }));
}

#include "admin_remote_tests.moc"
QTEST_MAIN(AdminRemoteTests)
