#include <QNetworkProxy>
#include "core/persistence/Persistence.h"
#include "core/service/AuditLogService.h"
#include "core/service/ParkingService.h"
#include "core/service/UserStore.h"
#include "network/EventHub.h"
#include "network/RestGateway.h"
#include "network/SmartParkTcpServer.h"
#include "network/TcpClient.h"

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocket>
#include <QUrl>

#include <qrcodegen.hpp>

#include <iostream>
#include <memory>

namespace{
constexpr int kSelftestTimeoutMs = 5000;

// 自测用同步 HTTP 探针：HTTP/1.1 + Connection: close，收到响应即断开。
struct HttpReply{
    int status{0};
    QByteArray raw;
    QJsonObject json;
};

class HttpProbe{
public:
    explicit HttpProbe(quint16 port) : port_(port){}

    HttpReply send(const QString &method, const QString &path,
                   const QJsonObject &body = {},
                   const QString &bearer = {}) const{
        HttpReply reply;
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port_);
        if (!socket.waitForConnected(3000)){
            return reply;
        }
        const QByteArray payload = body.isEmpty()
            ? QByteArray{}
            : QJsonDocument(body).toJson(QJsonDocument::Compact);
        QByteArray request;
        request += method.toUtf8();
        request += ' ';
        request += path.toUtf8();
        request += " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n";
        if (!bearer.isEmpty()){
            request += "Authorization: Bearer ";
            request += bearer.toUtf8();
            request += "\r\n";
        }
        if (!payload.isEmpty()){
            request += "Content-Type: application/json\r\n";
        }
        request += "Content-Length: ";
        request += QByteArray::number(payload.size());
        request += "\r\n\r\n";
        request += payload;
        socket.write(request);
        // QHttpServer 默认 keep-alive，不等断开：按 Content-Length 判定
        // 响应完整即返回；超时/断开作为兜底退出。
        QByteArray received;
        QEventLoop loop;
        QTimer::singleShot(kSelftestTimeoutMs, &loop, &QEventLoop::quit);
        QObject::connect(&socket, &QTcpSocket::disconnected,
                         &loop, &QEventLoop::quit);
        QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&]{
            received += socket.readAll();
            const int headerEnd = received.indexOf("\r\n\r\n");
            if (headerEnd < 0){
                return;
            }
            qint64 contentLength = -1;
            const QList<QByteArray> headerLines =
                received.left(headerEnd).split('\n');
            for (const QByteArray &line : headerLines){
                const QByteArray trimmed = line.trimmed();
                if (trimmed.toLower().startsWith("content-length:")){
                    contentLength = trimmed.mid(15).trimmed().toLongLong();
                }
            }
            const qint64 bodySize =
                static_cast<qint64>(received.size()) - (headerEnd + 4);
            if (contentLength >= 0 && bodySize >= contentLength){
                loop.quit();
            }
        });
        loop.exec();
        socket.close();
        reply.raw = received;
        const int headerEnd = reply.raw.indexOf("\r\n\r\n");
        if (headerEnd < 0){
            return reply;
        }
        const QByteArray statusLine =
            reply.raw.left(headerEnd).split('\n').value(0).trimmed();
        if (statusLine.startsWith("HTTP/")){
            reply.status = statusLine.mid(9, 3).toInt();
        }
        reply.json = QJsonDocument::fromJson(
            reply.raw.mid(headerEnd + 4)).object();
        return reply;
    }

private:
    quint16 port_;
};

quint16 freePort(){
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}


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

    // 10b. 「部分确认」的前提：应答必须逐条给出结论，顺序与提交一致，
    // 且 applied + duplicate + skipped 恰好等于提交条数。客户端只按前缀出队，
    // 所以这三点缺一不可（详见 apps/gate 的 replayHandledPrefix）。
    const qint64 shapeTs = QDateTime::currentMSecsSinceEpoch() - 60 * 1000LL;
    const QJsonArray mixedBatch{
        QJsonObject{{QStringLiteral("kind"), QStringLiteral("enter")},
                    {QStringLiteral("plate"), QStringLiteral("晋G00101")},
                    {QStringLiteral("vehicleType"), QStringLiteral("car")},
                    {QStringLiteral("ts"), shapeTs}},
        QJsonObject{{QStringLiteral("kind"), QStringLiteral("enter")},
                    {QStringLiteral("plate"), QStringLiteral("晋G00101")},
                    {QStringLiteral("vehicleType"), QStringLiteral("car")},
                    {QStringLiteral("ts"), shapeTs}},
        QJsonObject{{QStringLiteral("kind"), QStringLiteral("enter")},
                    {QStringLiteral("plate"), QStringLiteral("晋G00102")},
                    {QStringLiteral("ts"), qint64(0)}},
        QJsonObject{{QStringLiteral("kind"), QStringLiteral("teleport")},
                    {QStringLiteral("plate"), QStringLiteral("晋G00103")},
                    {QStringLiteral("ts"), shapeTs}},
        QJsonObject{{QStringLiteral("kind"), QStringLiteral("enter")},
                    {QStringLiteral("plate"), QStringLiteral("晋G00104")},
                    {QStringLiteral("vehicleType"), QStringLiteral("spaceship")},
                    {QStringLiteral("ts"), shapeTs}}};
    const auto mixed = gate.request(QStringLiteral("gate.replay"),
        QJsonObject{{QStringLiteral("events"), mixedBatch}});
    const QJsonObject mixedPayload = mixed
        ? mixed->value(QStringLiteral("payload")).toObject() : QJsonObject{};
    const QJsonArray mixedResults =
        mixedPayload.value(QStringLiteral("results")).toArray();
    check(mixed && mixed->value(QStringLiteral("ok")).toBool()
              && mixedResults.size() == mixedBatch.size(),
          "replay answers every submitted event with a per-event verdict",
          mixed.value_or(QJsonObject()));
    bool ordered = mixedResults.size() == mixedBatch.size();
    bool everyVerdict = ordered;
    for (int i = 0; ordered && i < mixedResults.size(); ++i){
        const QJsonObject entry = mixedResults.at(i).toObject();
        const QJsonObject submitted = mixedBatch.at(i).toObject();
        ordered = entry.value(QStringLiteral("plate")).toString()
                      == submitted.value(QStringLiteral("plate")).toString()
                  && entry.value(QStringLiteral("kind")).toString()
                      == submitted.value(QStringLiteral("kind")).toString();
        everyVerdict = everyVerdict
            && entry.value(QStringLiteral("ok")).isBool();
    }
    check(ordered, "replay verdicts arrive in the submitted order",
          mixed.value_or(QJsonObject()));
    check(everyVerdict,
          "every verdict carries a boolean ok, so a client can stop at the first missing one",
          mixed.value_or(QJsonObject()));
    check(mixedPayload.value(QStringLiteral("applied")).toInt() == 1
              && mixedPayload.value(QStringLiteral("duplicate")).toInt() == 1
              && mixedPayload.value(QStringLiteral("skipped")).toInt() == 3,
          "a mixed batch reports applied + duplicate + skipped == submitted",
          mixed.value_or(QJsonObject()));
    check(mixedPayload.value(QStringLiteral("applied")).toInt()
              + mixedPayload.value(QStringLiteral("duplicate")).toInt()
              + mixedPayload.value(QStringLiteral("skipped")).toInt()
              == mixedBatch.size(),
          "the three replay counters add up to the submitted count",
          mixed.value_or(QJsonObject()));
    const auto forbidden = client.request(QStringLiteral("gate.replay"), replayPayload);
    check(forbidden && !forbidden->value(QStringLiteral("ok")).toBool(),
          "non-gate account cannot replay", forbidden.value_or(QJsonObject()));

    // 长驻入口必须在事件循环运行期间持续持有监听对象。
    QProcess daemon;
    daemon.start(QCoreApplication::applicationFilePath(),
                 {QStringLiteral("--port"), QStringLiteral("0"),
                  QStringLiteral("--http-port"), QStringLiteral("0"),
                  QStringLiteral("--ws-port"), QStringLiteral("0"),
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

    // ---- REST 网关（docs/rest-api.md）：注册/登录/查询/缴费/预约/指引/WS ----
    smartpark::RestGateway::Options restOptions;
    restOptions.httpPort = freePort();
    restOptions.wsPort = freePort();
    smartpark::EventHub hub;
    server.setEventHub(&hub);
    smartpark::RestGateway rest(service, users, &audit,
                                persistence.databaseManager().database(),
                                &hub, restOptions);
    check(rest.listen(), "rest gateway listens");
    const HttpProbe http(rest.httpPort());
    int restFailures = 0;
    auto restCheck = [&](bool condition, const std::string &what,
                         const HttpReply &reply){
        std::cout << (condition ? "  ok  " : "  FAIL ") << what << '\n';
        if (!condition){
            std::cout << "       status=" << reply.status
                      << " body=" << reply.raw.left(300).toStdString() << '\n';
            ++restFailures;
        }
    };
    const QString plateA = QStringLiteral("晋R10001");
    const QString plateB = QStringLiteral("晋R10002");
    const auto enc = [](const QString &text){
        return QString::fromUtf8(QUrl::toPercentEncoding(text));
    };

    const auto meta = http.send(QStringLiteral("GET"),
                                QStringLiteral("/api/v1/meta"));
    restCheck(meta.status == 200
                  && meta.json.value(QStringLiteral("paymentMode")).toString()
                         == QStringLiteral("mock"),
              "meta advertises mock payment mode", meta);

    const auto registered = http.send(QStringLiteral("POST"),
                                      QStringLiteral("/api/v1/auth/register"),
                                      QJsonObject{
                                          {QStringLiteral("username"),
                                           QStringLiteral("alice")},
                                          {QStringLiteral("password"),
                                           QStringLiteral("secret1")}});
    restCheck(registered.status == 201, "register returns 201", registered);
    const auto duplicate = http.send(QStringLiteral("POST"),
                                     QStringLiteral("/api/v1/auth/register"),
                                     QJsonObject{
                                         {QStringLiteral("username"),
                                          QStringLiteral("alice")},
                                         {QStringLiteral("password"),
                                          QStringLiteral("secret1")}});
    restCheck(duplicate.status == 409, "duplicate register is rejected",
              duplicate);

    const auto badLogin = http.send(QStringLiteral("POST"),
                                    QStringLiteral("/api/v1/auth/login"),
                                    QJsonObject{
                                        {QStringLiteral("username"),
                                         QStringLiteral("alice")},
                                        {QStringLiteral("password"),
                                         QStringLiteral("wrong!!")}});
    restCheck(badLogin.status == 401
                  && badLogin.json.value(QStringLiteral("code")).toString()
                         == QStringLiteral("AUTH_FAILED"),
              "wrong password answers AUTH_FAILED without leaking the cause",
              badLogin);

    const auto aliceLogin = http.send(
        QStringLiteral("POST"), QStringLiteral("/api/v1/auth/login"),
        QJsonObject{{QStringLiteral("username"), QStringLiteral("alice")},
                    {QStringLiteral("password"), QStringLiteral("secret1")}});
    restCheck(aliceLogin.status == 200
                  && aliceLogin.json.value(QStringLiteral("user")).toObject()
                         .value(QStringLiteral("role")).toString()
                         == QStringLiteral("user"),
              "alice logs in with role user", aliceLogin);
    const QString aliceToken = aliceLogin.json.value(QStringLiteral("token"))
                                   .toString();

    const auto restAnonymous = http.send(
        QStringLiteral("GET"), QStringLiteral("/api/v1/parking/status"));
    restCheck(restAnonymous.status == 401,
              "anonymous status request is rejected", restAnonymous);

    const auto me = http.send(QStringLiteral("GET"), QStringLiteral("/api/v1/me"),
                              {}, aliceToken);
    restCheck(me.status == 200
                  && me.json.value(QStringLiteral("username")).toString()
                         == QStringLiteral("alice"),
              "me resolves the bearer token", me);

    const auto restStatus = http.send(
        QStringLiteral("GET"), QStringLiteral("/api/v1/parking/status"), {},
        aliceToken);
    restCheck(restStatus.status == 200
                  && restStatus.json.value(QStringLiteral("capacity")).toInt()
                         == 60,
              "rest parking.status serves capacity", restStatus);

    const auto restEnter = http.send(
        QStringLiteral("POST"), QStringLiteral("/api/v1/parking/enter"),
        QJsonObject{{QStringLiteral("plate"), plateA},
                    {QStringLiteral("vehicleType"), QStringLiteral("car")}},
        aliceToken);
    restCheck(restEnter.status == 200
                  && !restEnter.json.value(QStringLiteral("spotId"))
                          .toString().isEmpty(),
              "rest parking.enter allocates a spot", restEnter);

    const auto active = http.send(
        QStringLiteral("GET"),
        QStringLiteral("/api/v1/records/%1/active").arg(enc(plateA)), {},
        aliceToken);
    restCheck(active.status == 200
                  && std::abs(active.json.value(QStringLiteral("estimateFee"))
                                  .toDouble()) < 1e-9,
              "active record estimates zero fee within free period", active);

    const auto orderCreate = http.send(
        QStringLiteral("POST"), QStringLiteral("/api/v1/payments/orders"),
        QJsonObject{{QStringLiteral("kind"), QStringLiteral("parking_fee")},
                    {QStringLiteral("plate"), plateA}},
        aliceToken);
    restCheck(orderCreate.status == 201
                  && orderCreate.json.value(QStringLiteral("status")).toString()
                         == QStringLiteral("pending"),
              "parking_fee order starts pending", orderCreate);
    const QString orderId =
        orderCreate.json.value(QStringLiteral("orderId")).toString();

    const auto orderConfirm = http.send(
        QStringLiteral("POST"),
        QStringLiteral("/api/v1/payments/orders/%1/confirm").arg(orderId),
        QJsonObject{}, aliceToken);
    restCheck(orderConfirm.status == 200
                  && orderConfirm.json.value(QStringLiteral("order"))
                         .toObject()
                         .value(QStringLiteral("status")).toString()
                         == QStringLiteral("paid")
                  && orderConfirm.json.value(QStringLiteral("leave"))
                         .toObject()
                         .value(QStringLiteral("paidByOrder")).toBool()
                  && std::abs(orderConfirm.json.value(QStringLiteral("leave"))
                                  .toObject()
                                  .value(QStringLiteral("fee"))
                                  .toDouble()) < 1e-9,
              "confirm pays the order and settles the exit", orderConfirm);

    const auto idempotent = http.send(
        QStringLiteral("POST"),
        QStringLiteral("/api/v1/payments/orders/%1/confirm").arg(orderId),
        QJsonObject{}, aliceToken);
    restCheck(idempotent.status == 200
                  && idempotent.json.value(QStringLiteral("status")).toString()
                         == QStringLiteral("paid"),
              "repeated confirm is idempotent", idempotent);

    const auto closedRecords = http.send(
        QStringLiteral("GET"),
        QStringLiteral("/api/v1/records?plate=%1&state=closed").arg(enc(plateA)),
        {}, aliceToken);
    restCheck(closedRecords.status == 200
                  && closedRecords.json.value(QStringLiteral("total")).toInt()
                         == 1,
              "closed record is queryable after paid exit", closedRecords);

    const qint64 restStartMs = QDateTime::currentMSecsSinceEpoch()
        + 60 * 60 * 1000;
    const auto reservationCreate = http.send(
        QStringLiteral("POST"), QStringLiteral("/api/v1/reservations"),
        QJsonObject{{QStringLiteral("plate"), plateB},
                    {QStringLiteral("vehicleType"), QStringLiteral("car")},
                    {QStringLiteral("startMs"), restStartMs},
                    {QStringLiteral("durationMin"), 120}},
        aliceToken);
    restCheck(reservationCreate.status == 201
                  && reservationCreate.json.value(QStringLiteral("entryRoute"))
                         .toObject()
                         .value(QStringLiteral("points")).isArray()
                  && reservationCreate.json.value(QStringLiteral("order"))
                         .toObject()
                         .value(QStringLiteral("status")).toString()
                         == QStringLiteral("paid")
                  && std::abs(reservationCreate.json.value(QStringLiteral("deposit"))
                                  .toDouble() - 20.0) < 1e-9,
              "reservation create returns routes and a paid deposit order",
              reservationCreate);
    const QString reservationId =
        reservationCreate.json.value(QStringLiteral("reservationId")).toString();

    const auto openList = http.send(
        QStringLiteral("GET"),
        QStringLiteral("/api/v1/reservations?plate=%1&state=open")
            .arg(enc(plateB)),
        {}, aliceToken);
    restCheck(openList.status == 200
                  && openList.json.value(QStringLiteral("total")).toInt() == 1,
              "open reservation list finds the new booking", openList);

    const auto guide = http.send(QStringLiteral("GET"),
                                 QStringLiteral("/api/v1/guide/%1")
                                     .arg(enc(plateB)),
                                 {}, aliceToken);
    restCheck(guide.status == 200
                  && guide.json.value(QStringLiteral("source")).toString()
                         == QStringLiteral("reservation"),
              "guide serves the expected route for a reservation", guide);

    const auto cancel = http.send(
        QStringLiteral("POST"),
        QStringLiteral("/api/v1/reservations/%1/cancel").arg(reservationId),
        QJsonObject{}, aliceToken);
    const auto afterCancel = http.send(
        QStringLiteral("GET"),
        QStringLiteral("/api/v1/reservations?plate=%1&state=open")
            .arg(enc(plateB)),
        {}, aliceToken);
    restCheck(cancel.status == 200
                  && afterCancel.json.value(QStringLiteral("total")).toInt()
                         == 0,
              "cancel releases the reservation", cancel);

    const auto missingOrder = http.send(
        QStringLiteral("GET"),
        QStringLiteral("/api/v1/payments/orders/po_missing"), {}, aliceToken);
    restCheck(missingOrder.status == 404, "unknown order answers 404",
              missingOrder);

    const auto logout = http.send(QStringLiteral("POST"),
                                  QStringLiteral("/api/v1/auth/logout"),
                                  QJsonObject{}, aliceToken);
    const auto afterLogout = http.send(QStringLiteral("GET"),
                                       QStringLiteral("/api/v1/me"), {},
                                       aliceToken);
    restCheck(logout.status == 200 && afterLogout.status == 401,
              "logout revokes the token", logout);

    // WebSocket：认证后应实时收到 REST 侧入场的广播事件。
    const auto wsLogin = http.send(
        QStringLiteral("POST"), QStringLiteral("/api/v1/auth/login"),
        QJsonObject{{QStringLiteral("username"), QStringLiteral("alice")},
                    {QStringLiteral("password"), QStringLiteral("secret1")}});
    const QString wsToken =
        wsLogin.json.value(QStringLiteral("token")).toString();
    QWebSocket wsClient;
    QEventLoop wsAuthLoop;
    QEventLoop wsEventLoop;
    int wsPhase = 0;                 // 1 = 等 auth 应答；2 = 等入场事件
    QEventLoop *wsActiveLoop = nullptr;
    bool wsAuthed = false;
    bool wsSawEntered = false;
    QTimer::singleShot(5000, &wsAuthLoop, &QEventLoop::quit);
    QTimer::singleShot(5000, &wsEventLoop, &QEventLoop::quit);
    QObject::connect(&wsClient, &QWebSocket::connected, &wsAuthLoop, [&]{
        wsClient.sendTextMessage(QString::fromUtf8(QJsonDocument(
            QJsonObject{{QStringLiteral("type"), QStringLiteral("auth")},
                        {QStringLiteral("token"), wsToken}})
                         .toJson(QJsonDocument::Compact)));
    });
    QObject::connect(&wsClient, &QWebSocket::textMessageReceived, &wsAuthLoop,
                     [&](const QString &message){
        const QJsonObject frame =
            QJsonDocument::fromJson(message.toUtf8()).object();
        if (frame.value(QStringLiteral("type")).toString()
                == QStringLiteral("auth")
            && frame.value(QStringLiteral("ok")).toBool()){
            wsAuthed = true;
        } else if (frame.value(QStringLiteral("event")).toString()
                       == QStringLiteral("parking.entered")){
            wsSawEntered = true;
        }
        if (wsActiveLoop != nullptr
            && ((wsPhase == 1 && wsAuthed) || (wsPhase == 2 && wsSawEntered))){
            wsActiveLoop->quit();
        }
    });
    wsClient.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws").arg(rest.wsPort())));
    wsPhase = 1;
    wsActiveLoop = &wsAuthLoop;
    wsAuthLoop.exec();
    check(wsAuthed, "websocket authenticates with bearer token");
    if (wsAuthed){
        const auto wsTrigger = http.send(
            QStringLiteral("POST"), QStringLiteral("/api/v1/parking/enter"),
            QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋R10003")},
                        {QStringLiteral("vehicleType"), QStringLiteral("car")}},
            wsToken);
        wsPhase = 2;
        wsActiveLoop = &wsEventLoop;
        wsEventLoop.exec();
        check(wsSawEntered && wsTrigger.status == 200,
              "websocket receives parking.entered broadcast from rest action");
    }
    wsClient.close();

    // ---- M2/M3：二维码 / 无感支付 / 拍照识牌 mock / 订单列表 ----
    const auto qr = http.send(QStringLiteral("GET"),
                              QStringLiteral("/api/v1/qr?text=http%3A%2F%2Fdemo"));
    restCheck(qr.status == 200 && qr.raw.contains("<svg")
                  && qr.raw.contains("image/svg+xml"),
              "qr endpoint renders an svg", qr);

    const auto lpr = http.send(
        QStringLiteral("POST"), QStringLiteral("/api/v1/lpr/recognize"),
        QJsonObject{{QStringLiteral("image"),
                     QString::fromLatin1(
                         QByteArray("demo-image-bytes").toBase64())}},
        wsToken);
    restCheck(lpr.status == 200
                  && lpr.json.value(QStringLiteral("source")).toString()
                         == QStringLiteral("mock")
                  && lpr.json.value(QStringLiteral("plate")).toString().size() == 7,
              "lpr mock returns a stable demo plate", lpr);

    // 无感支付：开通 -> REST 入场 -> REST 离场 -> 自动生成已支付订单。
    const QString frPlate = QStringLiteral("晋R30001");
    const auto frOn = http.send(
        QStringLiteral("POST"), QStringLiteral("/api/v1/me/frictionless"),
        QJsonObject{{QStringLiteral("plate"), frPlate},
                    {QStringLiteral("enabled"), true}},
        wsToken);
    const auto frEnter = http.send(
        QStringLiteral("POST"), QStringLiteral("/api/v1/parking/enter"),
        QJsonObject{{QStringLiteral("plate"), frPlate},
                    {QStringLiteral("vehicleType"), QStringLiteral("car")}},
        wsToken);
    check(frOn.status == 200 && frEnter.status == 200,
          "frictionless plate enabled and car entered");
    const auto frLeave = http.send(
        QStringLiteral("POST"), QStringLiteral("/api/v1/parking/leave"),
        QJsonObject{{QStringLiteral("plate"), frPlate}},
        wsToken);
    const auto frOrders = http.send(
        QStringLiteral("GET"),
        QStringLiteral("/api/v1/payments/orders?plate=%1&status=paid")
            .arg(enc(frPlate)),
        {}, wsToken);
    restCheck(frLeave.status == 200 && frOrders.status == 200
                  && frOrders.json.value(QStringLiteral("total")).toInt() == 1
                  && frOrders.json.value(QStringLiteral("orders")).toArray()
                         .at(0).toObject()
                         .value(QStringLiteral("amount")).toDouble() < 1e-9,
              "frictionless exit auto-charges (zero fee in free period)",
              frOrders);
    const auto frOff = http.send(
        QStringLiteral("POST"), QStringLiteral("/api/v1/me/frictionless"),
        QJsonObject{{QStringLiteral("plate"), frPlate},
                    {QStringLiteral("enabled"), false}},
        wsToken);
    restCheck(frOff.status == 200, "frictionless can be disabled", frOff);

    failures += restFailures;

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

// 虚拟接口前缀：这些地址对局域网里的其他设备没有意义。
// 用它们对外广播，二维码就会指向一个手机永远连不上的地方。
bool isVirtualInterfaceName(const QString &name){
    static const char *const kPrefixes[] = {
        "docker", "br-",   "veth",   "virbr", "vmnet", "bridge",
        "tun",    "tap",   "utun",   "feth",  "fptun", "ppp",
        "gif",    "stf",   "awdl",   "llw",   "anpi",  "ipsec",
    };
    const QString lower = name.toLower();
    for (const char *prefix : kPrefixes){
        if (lower.startsWith(QLatin1String(prefix))){
            return true;
        }
    }
    return false;
}

// 内核会为「去往某地址」的出站流量挑选源地址。UDP 的 connect 不发包，
// 只是让内核把选路结果填进来——这是拿默认路由所在地址最省事的办法，
// 而且跨平台（Qt 没有暴露路由表 API）。
QString defaultRouteAddress(){
    QUdpSocket probe;
    // 用一个不可能真的通信的地址，只为触发选路。
    probe.connectToHost(QHostAddress(QStringLiteral("10.255.255.255")), 9);
    if (probe.waitForConnected(200)){
        const QHostAddress local = probe.localAddress();
        if (!local.isNull() && !local.isLoopback()
            && local.protocol() == QAbstractSocket::IPv4Protocol){
            return local.toString();
        }
    }
    return QString();
}

// 对外广播用的地址。按可靠性依次退化：
//   1. --advertise 显式指定（部署时最可靠，多网卡机器建议直接给）
//   2. 默认路由所在、且不是虚拟接口的地址
//   3. 第一个非虚拟接口的地址
//   4. 第一个非回环地址（旧行为）
//   5. 127.0.0.1
// 第 2 步要同时满足「是默认路由」和「非虚拟」：装了 VPN 的机器默认路由会指向
// utun，单看选路结果反而会取到隧道地址。
QString localLanAddress(const QString &advertised){
    if (!advertised.isEmpty()){
        return advertised;
    }
    const QString routeAddress = defaultRouteAddress();

    QString firstNonVirtual;
    QString firstAny;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()){
        if ((iface.flags() & QNetworkInterface::IsUp) == 0
            || (iface.flags() & QNetworkInterface::IsRunning) == 0){
            continue;
        }
        const bool virtualIface = isVirtualInterfaceName(iface.name());
        for (const QNetworkAddressEntry &entry : iface.addressEntries()){
            const QHostAddress address = entry.ip();
            if (address.protocol() != QAbstractSocket::IPv4Protocol
                || address.isLoopback() || address.isLinkLocal()){
                continue;
            }
            const QString text = address.toString();
            if (!routeAddress.isEmpty() && text == routeAddress && !virtualIface){
                return text;
            }
            if (!virtualIface && firstNonVirtual.isEmpty()){
                firstNonVirtual = text;
            }
            if (firstAny.isEmpty()){
                firstAny = text;
            }
        }
    }
    if (!firstNonVirtual.isEmpty()){
        return firstNonVirtual;
    }
    if (!firstAny.isEmpty()){
        return firstAny;
    }
    return QStringLiteral("127.0.0.1");
}

// 启动横幅打印 H5 接入地址的 ASCII 二维码：答辩现场手机直接扫。
void printAccessQr(const QString &url){
    const qrcodegen::QrCode code =
        qrcodegen::QrCode::encodeText(url.toUtf8().constData(),
                                      qrcodegen::QrCode::Ecc::MEDIUM);
    const int size = code.getSize();
    std::cout << "\nScan to open SmartPark H5: " << url.toStdString() << "\n";
    for (int y = -2; y < size + 2; y += 2){
        std::cout << "  ";
        for (int x = -2; x < size + 2; ++x){
            // 两行并一行：终端字符高约为宽的两倍，保持二维码比例。
            const bool dark = x >= 0 && x < size && y >= 0 && y < size
                && code.getModule(x, y);
            const bool darkNext = x >= 0 && x < size && y + 1 >= 0
                && y + 1 < size && code.getModule(x, y + 1);
            std::cout << (dark && darkNext ? "\u2588\u2588"
                          : dark ? "\u2580\u2580"
                                 : darkNext ? "\u2584\u2584" : "  ");
        }
        std::cout << '\n';
    }
    std::cout << std::flush;
}

int runServer(QCoreApplication &app, quint16 port, const QString &databasePath,
              const QString &layoutPath,
              const smartpark::RestGateway::Options &restOptions,
              const QString &advertisedAddress){
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
    // REST/WS 网关与 TCP 服务端同进程共享 ParkingService；事件经 EventHub
    // 同步广播到两条入口。TCP 9527 协议不变，旧终端零改动。
    smartpark::EventHub hub;
    server.setEventHub(&hub);
    smartpark::RestGateway rest(*service, users, &audit,
                                persistence.databaseManager().database(),
                                &hub, restOptions);
    if (!rest.listen()){
        std::cerr << "rest gateway listen failed: "
                  << rest.lastError().toStdString() << '\n';
        return 1;
    }
    std::cout << "SmartPark server listening on port " << server.port()
              << " | REST http://"
              << localLanAddress(advertisedAddress).toStdString() << ':'
              << rest.httpPort() << "/api/v1/meta"
              << " | ws " << (rest.wsPort() != 0
                                  ? std::to_string(rest.wsPort())
                                  : std::string("disabled"))
              << " | db: " << databasePath.toStdString()
              << " | spots: " << service->spots().size() << '\n' << std::flush;
    if (restOptions.httpPort != 0){
        // 扫码进入「设置账户」流程：票随码走，H5 拿到后先确认扫的是哪个点位，
        // 未登录则先注册/登录再绑定车牌。
        const QString token = rest.siteToken();
        const QString base = QStringLiteral("http://%1:%2/")
                                 .arg(localLanAddress(advertisedAddress))
                                 .arg(rest.httpPort());
        printAccessQr(token.isEmpty()
                          ? base
                          : base + QStringLiteral("#/claim?t=") + token);
    }
    if (!restOptions.lprCommand.isEmpty()){
        std::cout << "LPR backend: script mode (" << restOptions.lprCommand.toStdString()
                  << ")\n" << std::flush;
    } else {
        std::cout << "LPR backend: mock (--lpr-command to enable script mode)\n"
                  << std::flush;
    }
    return app.exec();
}
} // namespace

int main(int argc, char **argv){
    QCoreApplication app(argc, argv);

    // 这是局域网服务/客户端，不该走系统代理。但 Qt 会自动读取 all_proxy /
    // http_proxy 等环境变量并套用到**所有** socket——包括监听 socket，
    // 于是 listen() 会直接失败："proxy type is invalid for this operation"。
    // 集群与 CI 环境常带这些变量（s1 上 all_proxy=socks5h://...），
    // 所以显式关掉，避免部署时服务端起不来。
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

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
    const QCommandLineOption httpPortOption(
        QStringLiteral("http-port"),
        QStringLiteral("REST 网关监听端口（0=系统随机分配）"), QStringLiteral("port"),
        QStringLiteral("8080"));
    const QCommandLineOption wsPortOption(
        QStringLiteral("ws-port"),
        QStringLiteral("WebSocket 推送端口（0=禁用推送）"), QStringLiteral("port"),
        QStringLiteral("8081"));
    const QCommandLineOption wsPublicUrlOption(
        QStringLiteral("ws-public-url"),
        QStringLiteral("对外暴露的 WebSocket 地址（如 wss://park.example.com/ws）。"
                       "反代/HTTPS 部署时必填，否则前端会去连内部 ws 端口"),
        QStringLiteral("url"));
    const QCommandLineOption advertiseOption(
        QStringLiteral("advertise"),
        QStringLiteral("对外广播的地址（IP 或域名）：启动横幅与二维码用它，"
                       "多网卡/VPN/Docker 机器建议显式指定"),
        QStringLiteral("host"));
    const QCommandLineOption webRootOption(
        QStringLiteral("web-root"),
        QStringLiteral("H5 静态页目录（不传则不伺服网页）"), QStringLiteral("dir"));
    const QCommandLineOption siteNameOption(
        QStringLiteral("site-name"),
        QStringLiteral("点位名称（扫码后 H5 显示，用于区分多个车场）"),
        QStringLiteral("name"), QStringLiteral("SmartPark 停车场"));
    const QCommandLineOption lprCommandOption(
        QStringLiteral("lpr-command"),
        QStringLiteral("拍照识牌命令模板（%1 替换为临时图片路径；不传用 mock）"),
        QStringLiteral("command"));
    const QCommandLineOption selftestOption(QStringLiteral("selftest"),
                                            QStringLiteral("运行进程内端到端自测并退出"));
    parser.addOption(portOption);
    parser.addOption(dbOption);
    parser.addOption(layoutOption);
    parser.addOption(httpPortOption);
    parser.addOption(wsPortOption);
    parser.addOption(advertiseOption);
    parser.addOption(wsPublicUrlOption);
    parser.addOption(webRootOption);
    parser.addOption(lprCommandOption);
    parser.addOption(siteNameOption);
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

    smartpark::RestGateway::Options restOptions;
    restOptions.httpPort =
        static_cast<quint16>(parser.value(httpPortOption).toUShort());
    restOptions.wsPort =
        static_cast<quint16>(parser.value(wsPortOption).toUShort());
    if (parser.isSet(webRootOption)){
        restOptions.webRoot = parser.value(webRootOption);
    }
    if (parser.isSet(lprCommandOption)){
        restOptions.lprCommand = parser.value(lprCommandOption);
    }
    if (parser.isSet(siteNameOption)){
        restOptions.siteName = parser.value(siteNameOption);
    }
    if (parser.isSet(wsPublicUrlOption)){
        restOptions.wsPublicUrl = parser.value(wsPublicUrlOption);
    }
    return runServer(app, port, databasePath, layoutPath, restOptions,
                     parser.value(advertiseOption));
}
