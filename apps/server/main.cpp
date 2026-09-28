#include "core/persistence/Persistence.h"
#include "core/service/AuditLogService.h"
#include "core/service/ParkingService.h"
#include "core/service/UserStore.h"
#include "network/SmartParkTcpServer.h"
#include "network/TcpClient.h"

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QTemporaryDir>

#include <iostream>
#include <memory>

namespace{
constexpr int kSelftestTimeoutMs = 5000;

int runSelftest(){
    // 每次运行使用唯一的临时目录：重复执行 / 并行 CTest 都从干净状态开始。
    QTemporaryDir directory;
    if (!directory.isValid()){
        std::cerr << "FAIL: cannot create temp dir\n";
        return 1;
    }
    smartpark::Persistence persistence(directory.filePath(QStringLiteral("selftest.db")));
    smartpark::AuditLogService audit(persistence.databaseManager().database());
    smartpark::UserStore users(directory.filePath(QStringLiteral("users.db")));

    smartpark::ParkingService service(smartpark::ParkingLayout::defaultLayout(),
                                      smartpark::AllocationStrategy::WeightedCost,
                                      &persistence.repository());
    service.setAuditLog(&audit);

    smartpark::SmartParkTcpServer::Options options;
    options.port = 0;   // 由系统分配临时端口
    options.heartbeatTimeoutMs = 600000;
    smartpark::SmartParkTcpServer server(service, &audit, &users, options);
    if (!server.listen()){
        std::cerr << "FAIL: server listen: " << server.lastError().toStdString()
                  << '\n';
        return 1;
    }
    const quint16 port = server.port();
    std::cout << "selftest server listening on port " << port << '\n';

    int failures = 0;
    auto check = [&failures](bool condition, const std::string &what,
                             const QJsonObject &response = {}){
        std::cout << (condition ? "  ok  " : "  FAIL ") << what;
        if (!condition && !response.isEmpty()){
            std::cout << " | error="
                      << response.value(QStringLiteral("error")).toString().toStdString()
                      << " | ok=" << response.value(QStringLiteral("ok")).toBool();
        }
        std::cout << '\n';
        if (!condition){
            ++failures;
        }
    };

    smartpark::TcpClient client;
    check(client.connectToHost(QStringLiteral("127.0.0.1"), port),
          "client connects");

    // 1. 登录
    const auto login = client.request(QStringLiteral("login"),
        QJsonObject{{QStringLiteral("user"), QStringLiteral("admin")},
                    {QStringLiteral("pass"), QStringLiteral("smartpark")}});
    check(login.has_value() && login->value(QStringLiteral("ok")).toBool(),
          "login succeeds");
    const QString token =
        login ? login->value(QStringLiteral("payload")).toObject()
                    .value(QStringLiteral("token")).toString()
              : QString();
    client.setToken(token);

    // 2. 全场状态
    const auto status = client.request(QStringLiteral("parking.status"), {});
    check(status.has_value() && status->value(QStringLiteral("ok")).toBool()
              && status->value(QStringLiteral("payload")).toObject()
                     .value(QStringLiteral("capacity")).toInt() == 60,
          "parking.status returns capacity 60", status.value_or(QJsonObject()));

    // 3. 入场
    const auto enter = client.request(QStringLiteral("parking.enter"),
        QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋T00001")},
                    {QStringLiteral("vehicleType"), QStringLiteral("car")}});
    check(enter.has_value() && enter->value(QStringLiteral("ok")).toBool(),
          "parking.enter succeeds");

    // 4. 创建时段预约（60 分钟后开始、120 分钟时长；留足最短提前量）
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch() + 60 * 60 * 1000;
    const auto reserved = client.request(QStringLiteral("reservation.create"),
        QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋T00002")},
                    {QStringLiteral("vehicleType"), QStringLiteral("car")},
                    {QStringLiteral("startMs"), startMs},
                    {QStringLiteral("durationMin"), 120}});
    check(reserved.has_value() && reserved->value(QStringLiteral("ok")).toBool()
              && reserved->value(QStringLiteral("payload")).toObject()
                     .value(QStringLiteral("entryPoints")).isArray(),
          "reservation.create returns expected route", reserved.value_or(QJsonObject()));

    // 5. 到场核销：预约 60 分钟后才开始，立即到场应被到场窗口校验拒绝
    //    （正常到场链路由核心测试以注入时间覆盖）。
    const auto checkin = client.request(QStringLiteral("reservation.checkin"),
        QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋T00002")}});
    check(checkin.has_value() && !checkin->value(QStringLiteral("ok")).toBool()
              && checkin->value(QStringLiteral("error")).toString().contains(
                     QStringLiteral("到场时间窗口")),
          "reservation.checkin rejects arrival outside the window",
          checkin.value_or(QJsonObject()));

    // 6. 离场（晋T00001 刚入场 → 免费时段内费用为 0）
    const auto leave = client.request(QStringLiteral("parking.leave"),
        QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋T00001")}});
    check(leave.has_value() && leave->value(QStringLiteral("ok")).toBool()
              && std::abs(leave->value(QStringLiteral("payload")).toObject()
                              .value(QStringLiteral("fee")).toDouble())
                     < 1e-9,
          "parking.leave settles within free period", leave.value_or(QJsonObject()));

    // 7. 分析报告
    const auto report = client.request(QStringLiteral("analytics.report"), {});
    check(report.has_value() && report->value(QStringLiteral("ok")).toBool()
              && !report->value(QStringLiteral("payload")).toObject()
                      .value(QStringLiteral("summary")).toString().isEmpty(),
          "analytics.report returns a summary");

    // 8. 事件广播：第二个客户端登录后应收到入场/离场事件
    smartpark::TcpClient monitor;
    check(monitor.connectToHost(QStringLiteral("127.0.0.1"), port),
          "monitor client connects");
    const auto monitorLogin = monitor.request(QStringLiteral("login"),
        QJsonObject{{QStringLiteral("user"), QStringLiteral("admin")},
                    {QStringLiteral("pass"), QStringLiteral("smartpark")}});
    check(monitorLogin.has_value() && monitorLogin->value(QStringLiteral("ok")).toBool(),
          "monitor login succeeds");
    monitor.setToken(monitorLogin
                         ? monitorLogin->value(QStringLiteral("payload")).toObject()
                               .value(QStringLiteral("token")).toString()
                         : QString());
    const auto enter2 = client.request(QStringLiteral("parking.enter"),
        QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋T00003")},
                    {QStringLiteral("vehicleType"), QStringLiteral("electric")}});
    check(enter2.has_value() && enter2->value(QStringLiteral("ok")).toBool(),
          "second enter succeeds");
    // 给事件传播留一拍事件循环（通过再发一个同步请求天然等待）。
    (void)client.request(QStringLiteral("heartbeat"), {});
    const auto events = monitor.takeEvents();
    bool sawEntered = false;
    for (const QJsonObject &event : events){
        if (event.value(QStringLiteral("event")).toString()
                == QStringLiteral("parking.entered")){
            sawEntered = true;
        }
    }
    check(sawEntered, "monitor received parking.entered event");

    // 9. 未登录请求被拒绝
    smartpark::TcpClient anonymous;
    check(anonymous.connectToHost(QStringLiteral("127.0.0.1"), port),
          "anonymous client connects");
    const auto rejected = anonymous.request(QStringLiteral("parking.status"), {});
    check(rejected.has_value() && !rejected->value(QStringLiteral("ok")).toBool(),
          "unauthenticated request is rejected");

    // 10. 离线历史事件按原始时间入账；丢失确认后的重放不得重复计费。
    smartpark::TcpClient gate;
    check(gate.connectToHost(QStringLiteral("127.0.0.1"), port),
          "gate client connects");
    const auto gateLogin = gate.request(QStringLiteral("login"),
        QJsonObject{{QStringLiteral("user"), QStringLiteral("gate")},
                    {QStringLiteral("pass"), QStringLiteral("smartpark")}});
    check(gateLogin && gateLogin->value(QStringLiteral("ok")).toBool(),
          "gate login succeeds");
    gate.setToken(gateLogin
        ? gateLogin->value(QStringLiteral("payload")).toObject()
              .value(QStringLiteral("token")).toString() : QString());
    const qint64 offlineEntry = QDateTime::currentMSecsSinceEpoch() - 3 * 60 * 60 * 1000LL;
    const qint64 offlineExit = offlineEntry + 2 * 60 * 60 * 1000LL;
    const QJsonArray history{
        QJsonObject{{QStringLiteral("kind"), QStringLiteral("enter")},
                    {QStringLiteral("plate"), QStringLiteral("晋G00009")},
                    {QStringLiteral("vehicleType"), QStringLiteral("car")},
                    {QStringLiteral("ts"), offlineEntry}},
        QJsonObject{{QStringLiteral("kind"), QStringLiteral("exit")},
                    {QStringLiteral("plate"), QStringLiteral("晋G00009")},
                    {QStringLiteral("ts"), offlineExit}}};
    const QJsonObject replayPayload{{QStringLiteral("events"), history}};
    const auto replay = gate.request(QStringLiteral("gate.replay"), replayPayload);
    const QJsonObject firstReplay = replay
        ? replay->value(QStringLiteral("payload")).toObject() : QJsonObject{};
    check(replay && replay->value(QStringLiteral("ok")).toBool()
              && firstReplay.value(QStringLiteral("applied")).toInt() == 2
              && firstReplay.value(QStringLiteral("skipped")).toInt() == 0,
          "offline entry and exit applied", replay.value_or(QJsonObject()));
    const auto repeated = gate.request(QStringLiteral("gate.replay"), replayPayload);
    const QJsonObject secondReplay = repeated
        ? repeated->value(QStringLiteral("payload")).toObject() : QJsonObject{};
    check(repeated && secondReplay.value(QStringLiteral("duplicate")).toInt() == 2
              && secondReplay.value(QStringLiteral("applied")).toInt() == 0,
          "repeated replay is idempotent", repeated.value_or(QJsonObject()));
    const auto invalid = gate.request(QStringLiteral("gate.replay"),
        QJsonObject{{QStringLiteral("events"), QJsonArray{
            QJsonObject{{QStringLiteral("kind"), QStringLiteral("enter")},
                        {QStringLiteral("plate"), QStringLiteral("晋G00010")},
                        {QStringLiteral("ts"), qint64(0)}}}}});
    check(invalid && invalid->value(QStringLiteral("payload")).toObject()
                     .value(QStringLiteral("skipped")).toInt() == 1
              && !service.activeRecord("晋G00010"),
          "invalid offline timestamp rejected", invalid.value_or(QJsonObject()));
    const auto forbidden = client.request(QStringLiteral("gate.replay"), replayPayload);
    check(forbidden && !forbidden->value(QStringLiteral("ok")).toBool(),
          "non-gate account cannot replay", forbidden.value_or(QJsonObject()));

    // 长驻入口必须在事件循环运行期间持续持有监听对象。
    QProcess daemon;
    daemon.start(QCoreApplication::applicationFilePath(),
                 {QStringLiteral("--port"), QStringLiteral("0"),
                  QStringLiteral("--db"), directory.filePath(QStringLiteral("daemon.db"))});
    check(daemon.waitForStarted(5000), "daemon process starts");
    const bool announced = daemon.waitForReadyRead(5000);
    const QString banner = QString::fromUtf8(daemon.readAllStandardOutput());
    const QRegularExpression portPattern(QStringLiteral("listening on port (\\d+)"));
    const auto match = portPattern.match(banner);
    check(announced && match.hasMatch(), "daemon announces listening port");
    smartpark::TcpClient daemonClient;
    if (match.hasMatch()){
        check(daemonClient.connectToHost(QStringLiteral("127.0.0.1"),
                                         static_cast<quint16>(match.captured(1).toUShort())),
              "daemon accepts cross-process TCP connection");
        const auto loginResponse = daemonClient.request(QStringLiteral("login"),
            QJsonObject{{QStringLiteral("user"), QStringLiteral("admin")},
                        {QStringLiteral("pass"), QStringLiteral("smartpark")}});
        check(loginResponse && loginResponse->value(QStringLiteral("ok")).toBool(),
              "daemon authenticates cross-process client");
        daemonClient.setToken(loginResponse
            ? loginResponse->value(QStringLiteral("payload")).toObject()
                  .value(QStringLiteral("token")).toString() : QString());
        const auto daemonStatus = daemonClient.request(QStringLiteral("parking.status"), {});
        check(daemonStatus && daemonStatus->value(QStringLiteral("ok")).toBool(),
              "daemon serves request during event loop");
        daemonClient.disconnectFromHost();
    }
    daemon.terminate();
    if (!daemon.waitForFinished(3000)){
        daemon.kill();
        daemon.waitForFinished(3000);
    }

    // 审计链完整性
    const auto auditResult = audit.verifyChain();
    check(auditResult.ok, "audit hash chain stays consistent");

    client.disconnectFromHost();
    gate.disconnectFromHost();
    monitor.disconnectFromHost();
    anonymous.disconnectFromHost();

    std::cout << (failures == 0 ? "SERVER SELFTEST: PASS" : "SERVER SELFTEST: FAIL")
              << " (" << failures << " failure(s))\n";
    return failures == 0 ? 0 : 1;
}

int runServer(QCoreApplication &app, quint16 port, const QString &databasePath,
              const QString &layoutPath){
    smartpark::Persistence persistence(databasePath);
    smartpark::AuditLogService audit(persistence.databaseManager().database());
    smartpark::UserStore users(databasePath);

    // --layout 真正生效：缺省仍是内置 60 位布局（CLI/Gate 演示口径不变）。
    // 布局文件解析失败或与已持久化数据不兼容时直接报错退出，绝不自动清库。
    smartpark::ParkingLayout layout = smartpark::ParkingLayout::defaultLayout();
    if (!layoutPath.isEmpty()){
        QFile file(layoutPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)){
            std::cerr << "cannot open layout file: "
                      << layoutPath.toStdString() << '\n';
            return 1;
        }
        const QByteArray description = file.readAll();
        try{
            layout = smartpark::ParkingLayout::fromDescription(
                QString::fromUtf8(description).toStdString());
        } catch (const std::exception &error){
            std::cerr << "invalid layout file: " << error.what() << '\n';
            return 1;
        }
    }

    std::unique_ptr<smartpark::ParkingService> service;
    try{
        service = std::make_unique<smartpark::ParkingService>(
            layout, smartpark::AllocationStrategy::WeightedCost,
            &persistence.repository());
    } catch (const std::exception &error){
        std::cerr << "parking service init failed (layout incompatible with "
                     "persisted data?): " << error.what() << '\n'
                  << "refusing to auto-reset the database; choose a matching "
                     "--layout or remove the database manually.\n";
        return 1;
    }
    service->setAuditLog(&audit);

    smartpark::SmartParkTcpServer::Options options;
    options.port = port;
    smartpark::SmartParkTcpServer server(*service, &audit, &users, options);
    if (!server.listen()){
        std::cerr << "server listen failed: "
                  << server.lastError().toStdString() << '\n';
        return 1;
    }
    std::cout << "SmartPark server listening on port " << server.port()
              << " | db: " << databasePath.toStdString()
              << " | spots: " << service->spots().size() << '\n' << std::flush;
    return app.exec();
}
} // namespace

int main(int argc, char **argv){
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("SmartPark Server"));
    QCoreApplication::setOrganizationName(QStringLiteral("SmartPark"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("SmartPark TCP 服务端（协议 v1，见 docs/tcp-protocol.md）"));
    parser.addHelpOption();
    const QCommandLineOption portOption({QStringLiteral("p"), QStringLiteral("port")},
                                        QStringLiteral("监听端口"), QStringLiteral("port"),
                                        QStringLiteral("9527"));
    const QCommandLineOption dbOption({QStringLiteral("d"), QStringLiteral("db")},
                                      QStringLiteral("SQLite 数据库路径"),
                                      QStringLiteral("path"));
    const QCommandLineOption layoutOption(QStringLiteral("layout"),
                                          QStringLiteral("布局文件（缺省内置 60 位布局）"),
                                          QStringLiteral("path"));
    const QCommandLineOption selftestOption(QStringLiteral("selftest"),
                                            QStringLiteral("运行进程内端到端自测并退出"));
    parser.addOption(portOption);
    parser.addOption(dbOption);
    parser.addOption(layoutOption);
    parser.addOption(selftestOption);
    parser.process(app);

    if (parser.isSet(selftestOption)){
        const int result = runSelftest();
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
        app.exec();
        return result;
    }

    QString databasePath = parser.isSet(dbOption)
        ? parser.value(dbOption)
        : smartpark::Persistence::defaultDatabasePath();
    quint16 port = static_cast<quint16>(parser.value(portOption).toUShort());
    const QString layoutPath = parser.isSet(layoutOption)
        ? parser.value(layoutOption)
        : QString();

    return runServer(app, port, databasePath, layoutPath);
}
