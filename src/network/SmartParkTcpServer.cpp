#include "network/SmartParkTcpServer.h"
#include "core/model/ParkingLayout.h"
#include "core/service/AnalyticsEngine.h"
#include "core/service/AuditLogService.h"
#include "core/service/ParkingService.h"
#include "core/service/ReservationService.h"
#include "core/service/UserStore.h"
#include "network/EventHub.h"
#include "network/PlateRecognition.h"
#include "network/Protocol.h"

#include <QDateTime>
#include <QJsonArray>
#include <QTimerEvent>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>

#include <cmath>
#include <map>

namespace smartpark{
namespace{
using Clock = ParkingRecord::Clock;
using TimePoint = ParkingRecord::TimePoint;

TimePoint msToTime(qint64 ms){
    return TimePoint{} + std::chrono::milliseconds(ms);
}

qint64 timeToMs(TimePoint time){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        time.time_since_epoch()).count();
}

QJsonObject allocationToPayload(const AllocationResult &result){
    QJsonObject payload;
    payload.insert(QStringLiteral("plateNumber"),
                   QString::fromStdString(result.plateNumber));
    payload.insert(QStringLiteral("spotId"), QString::fromStdString(result.spotId));
    payload.insert(QStringLiteral("entryDistance"), result.entryRoute.distance);
    payload.insert(QStringLiteral("exitDistance"), result.exitRoute.distance);
    payload.insert(QStringLiteral("entryTurns"), result.entryRoute.turnCount);
    payload.insert(QStringLiteral("exitTurns"), result.exitRoute.turnCount);
    payload.insert(QStringLiteral("score"), result.score);
    payload.insert(QStringLiteral("entranceIndex"),
                   static_cast<int>(result.entranceIndex));
    payload.insert(QStringLiteral("exitIndex"), static_cast<int>(result.exitIndex));
    // 路线拐点也下发：管理端远程模式的车位图据此画出进场/出场路线，
    // 与本地模式（直接拿 AllocationResult）看到的是同一条线。
    const auto pointArray = [](const Route &route){
        QJsonArray points;
        for (const Point &point : route.points){
            points.append(QJsonObject{{QStringLiteral("x"), point.x},
                                      {QStringLiteral("y"), point.y}});
        }
        return points;
    };
    payload.insert(QStringLiteral("entryPoints"), pointArray(result.entryRoute));
    payload.insert(QStringLiteral("exitPoints"), pointArray(result.exitRoute));
    return payload;
}
} // namespace

SmartParkTcpServer::SmartParkTcpServer(ParkingService &service,
                                       AuditLogService *audit, UserStore *users,
                                       Options options, QObject *parent)
    : QObject(parent)
    , service_(&service)
    , audit_(audit)
    , users_(users)
    , options_(options){
    server_ = new QTcpServer(this);
    connect(server_, &QTcpServer::newConnection, this,
            &SmartParkTcpServer::onNewConnection);
    idleTimerId_ = startTimer(15000);
}

bool SmartParkTcpServer::listen(){
    if (!server_->listen(QHostAddress::Any, options_.port)){
        lastError_ = server_->errorString();
        return false;
    }
    return true;
}

quint16 SmartParkTcpServer::port() const{
    return server_->serverPort();
}

const QString &SmartParkTcpServer::lastError() const noexcept{
    return lastError_;
}

void SmartParkTcpServer::setEventHub(EventHub *hub) noexcept{
    hub_ = hub;
    if (hub_ == nullptr){
        return;
    }
    // 补上反方向：REST 入口（网页预约/缴费/入场）的动作只 publish 到 hub，
    // 原先没有任何人转发给 TCP 客户端，于是远程管理端收不到事件、永远不刷新
    // 快照——网页上刚建的预约在 macOS 端看不到。这里订阅 hub 转发给 TCP 会话。
    // 只调 sendToSessions：再 publish 一次会让 WebSocket 收到重复事件。
    connect(hub_, &EventHub::eventOccurred, this,
            [this](const QString &name, const QJsonObject &payload,
                   EventHub::Origin origin){
        if (origin == EventHub::Origin::Tcp){
            return;   // 已经由 broadcastEvent 发过了
        }
        sendToSessions(name, payload);
    });
}

void SmartParkTcpServer::timerEvent(QTimerEvent *event){
    if (event->timerId() == idleTimerId_){
        kickIdleSessions();
    }
    QObject::timerEvent(event);
}

void SmartParkTcpServer::onNewConnection(){
    while (QTcpSocket *socket = server_->nextPendingConnection()){
        Session session;
        session.socket = socket;
        session.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
        sessions_.insert(socket, session);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]{
            const auto it = sessions_.find(socket);
            if (it != sessions_.end()){
                onDisconnected(it.value());
                sessions_.erase(it);
            }
            socket->deleteLater();
        });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]{
            const auto it = sessions_.find(socket);
            if (it != sessions_.end()){
                onReadyRead(it.value());
            }
        });
    }
}

void SmartParkTcpServer::onReadyRead(Session &session){
    session.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
    session.buffer.append(session.socket->readAll());
    QJsonObject message;
    while (true){
        const auto status = protocol::tryDecodeFrame(session.buffer, &message);
        if (status == protocol::FrameStatus::NeedMore){
            return;
        }
        if (status == protocol::FrameStatus::Invalid){
            audit_->record("tcp", "protocol_invalid_frame",
                           session.user.toStdString());
            session.socket->disconnectFromHost();
            return;
        }
        handleMessage(session, message);
    }
}

void SmartParkTcpServer::onDisconnected(Session &session){
    if (session.authenticated && audit_ != nullptr){
        audit_->record(session.user.toStdString(), "tcp_disconnect");
    }
}

void SmartParkTcpServer::handleMessage(Session &session,
                                       const QJsonObject &message){
    const QString type = message.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("request")){
        return;  // 事件/未知类型不处理
    }
    const QString id = message.value(QStringLiteral("id")).toString();
    const QString action = message.value(QStringLiteral("action")).toString();
    const QString token = message.value(QStringLiteral("token")).toString();
    const QJsonObject payload =
        message.value(QStringLiteral("payload")).toObject();

    if (action == QStringLiteral("login")){
        bool ok = false;
        QString error;
        const QJsonObject result = actionLogin(session, payload, &ok, &error);
        respond(session, id, ok, result, error);
        return;
    }
    // 会话校验：token 匹配且已登录。
    if (!session.authenticated || token != session.token){
        respond(session, id, false, {}, QStringLiteral("会话未登录或 token 无效"));
        return;
    }
    session.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
    dispatch(session, id, action, payload);
}

void SmartParkTcpServer::dispatch(Session &session, const QString &id,
                                  const QString &action,
                                  const QJsonObject &payload){
    bool ok = false;
    QString error;
    QJsonObject result;
    if (action == QStringLiteral("heartbeat")){
        result.insert(QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch());
        ok = true;
    } else if (action == QStringLiteral("parking.status")){
        result = actionStatus(payload, &ok, &error);
    } else if (action == QStringLiteral("spot.list")){
        result = actionSpotList(payload, &ok, &error);
    } else if (action == QStringLiteral("parking.enter")){
        result = actionEnter(payload, &ok, &error);
    } else if (action == QStringLiteral("parking.leave")){
        result = actionLeave(payload, &ok, &error);
    } else if (action == QStringLiteral("reservation.create")){
        result = actionReservationCreate(payload, &ok, &error);
    } else if (action == QStringLiteral("reservation.cancel")){
        result = actionReservationCancel(payload, &ok, &error);
    } else if (action == QStringLiteral("reservation.checkin")){
        result = actionReservationCheckIn(payload, &ok, &error);
    } else if (action == QStringLiteral("analytics.report")){
        result = actionAnalyticsReport(payload, &ok, &error);
    } else if (action == QStringLiteral("gate.replay")){
        if (session.user == QStringLiteral("gate")){
            result = actionGateReplay(payload, &ok, &error);
        } else{
            error = QStringLiteral("仅 Gate 终端可补报");
        }
    } else if (action == QStringLiteral("lpr.recognize")){
        result = actionLprRecognize(payload, &ok, &error);
    } else if (action == QStringLiteral("admin.snapshot")){
        if (session.user == QStringLiteral("admin")){
            if (audit_ != nullptr){
                audit_->record(session.user.toStdString(), "admin_snapshot");
            }
            result = actionAdminSnapshot(payload, &ok, &error);
        } else{
            error = QStringLiteral("仅管理员可获取快照");
        }
    } else{
        error = QStringLiteral("未知 action: %1").arg(action);
    }
    respond(session, id, ok, result, error);
}

void SmartParkTcpServer::send(Session &session, const QJsonObject &message){
    session.socket->write(protocol::encodeFrame(message));
}

void SmartParkTcpServer::respond(Session &session, const QString &id, bool ok,
                                 const QJsonObject &payload, const QString &error){
    send(session, protocol::makeResponse(id, ok, payload, error));
}

QJsonObject SmartParkTcpServer::actionLprRecognize(const QJsonObject &payload,
                                                   bool *ok, QString *error){
    const QString imageBase64 = payload.value(QStringLiteral("image")).toString();
    const QByteArray imageBytes = QByteArray::fromBase64(imageBase64.toLatin1());
    if (imageBytes.isEmpty() || imageBytes.size() > protocol::kMaxImageBytes){
        *error = QStringLiteral("image 需为 base64 图片且不超过 %1MB")
                     .arg(protocol::kMaxImageBytes / (1024 * 1024));
        return {};
    }
    QString note;
    QJsonObject result = network::recognizePlate(imageBytes, options_.lprCommand, &note);
    if (result.isEmpty()){
        *error = QStringLiteral("识别失败：%1").arg(note);
        return {};
    }
    // 让调用方知道这次是真跑脚本还是 mock，别把演示结果当成识别结果。
    result.insert(QStringLiteral("backend"), note);
    *ok = true;
    return result;
}

void SmartParkTcpServer::sendToSessions(const QString &event,
                                       const QJsonObject &payload){
    const QByteArray frame = protocol::encodeFrame(
        protocol::makeEvent(event, payload));
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it){
        if (it.value().authenticated){
            it.key()->write(frame);
        }
    }
}

void SmartParkTcpServer::broadcastEvent(const QString &event,
                                        const QJsonObject &payload){
    sendToSessions(event, payload);
    if (hub_ != nullptr){
        // 标成 Tcp：本函数已经把事件发给 TCP 客户端了，订阅端别再发一次。
        hub_->publish(event, payload, EventHub::Origin::Tcp);
    }
}

void SmartParkTcpServer::kickIdleSessions(){
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<QTcpSocket *> stale;
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it){
        if (now - it.value().lastSeenMs > options_.heartbeatTimeoutMs){
            stale.append(it.key());
        }
    }
    for (QTcpSocket *socket : stale){
        if (audit_ != nullptr){
            audit_->record("tcp", "heartbeat_timeout");
        }
        socket->disconnectFromHost();
    }
}

// ---- 动作实现 ----

QJsonObject SmartParkTcpServer::actionLogin(Session &session,
                                            const QJsonObject &payload,
                                            bool *ok, QString *error){
    const QString user = payload.value(QStringLiteral("user")).toString();
    const QString pass = payload.value(QStringLiteral("pass")).toString();
    const auto loginResult = users_->verifyLogin(user, pass);
    if (loginResult != UserStore::LoginResult::Success){
        ++session.loginFails;
        if (audit_ != nullptr){
            audit_->record(user.toStdString(), "tcp_login_failed");
        }
        if (session.loginFails >= options_.maxLoginFails){
            session.socket->disconnectFromHost();
        }
        *ok = false;
        *error = UserStore::loginErrorText(loginResult);
        return {};
    }
    session.authenticated = true;
    session.user = user;
    session.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (audit_ != nullptr){
        audit_->record(user.toStdString(), "tcp_login_success");
    }
    QJsonObject result;
    result.insert(QStringLiteral("token"), session.token);
    result.insert(QStringLiteral("user"), user);
    *ok = true;
    return result;
}

QJsonObject SmartParkTcpServer::actionStatus(const QJsonObject &, bool *ok,
                                             QString *error){
    QJsonObject result;
    result.insert(QStringLiteral("capacity"), static_cast<int>(service_->spots().size()));
    result.insert(QStringLiteral("occupied"), service_->occupiedSpots());
    result.insert(QStringLiteral("available"), service_->remainingSpots());
    result.insert(QStringLiteral("reserved"), service_->reservedSpots());
    QJsonArray zones;
    std::map<std::string, std::pair<int, int>> zoneStats;
    for (const ParkingSpot &spot : service_->spots()){
        auto &stat = zoneStats[spot.zone()];
        stat.first += 1;
        if (spot.status() == SpotStatus::Occupied){
            stat.second += 1;
        }
    }
    for (const auto &entry : zoneStats){
        QJsonObject zone;
        zone.insert(QStringLiteral("zone"), QString::fromStdString(entry.first));
        zone.insert(QStringLiteral("total"), entry.second.first);
        zone.insert(QStringLiteral("occupied"), entry.second.second);
        zones.append(zone);
    }
    result.insert(QStringLiteral("zones"), zones);
    *ok = true;
    return result;
}

QJsonObject SmartParkTcpServer::actionSpotList(const QJsonObject &, bool *ok,
                                               QString *error){
    QJsonArray spots;
    for (const ParkingSpot &spot : service_->spots()){
        QJsonObject item;
        item.insert(QStringLiteral("spotId"), QString::fromStdString(spot.identifier()));
        item.insert(QStringLiteral("zone"), QString::fromStdString(spot.zone()));
        item.insert(QStringLiteral("type"), QLatin1String(toString(spot.type())));
        item.insert(QStringLiteral("status"), static_cast<int>(spot.status()));
        if (spot.parkedVehicle()){
            item.insert(QStringLiteral("plate"),
                        QString::fromStdString(spot.parkedVehicle()->plateNumber()));
        }
        spots.append(item);
    }
    QJsonObject result;
    result.insert(QStringLiteral("spots"), spots);
    *ok = true;
    return result;
}

QJsonObject SmartParkTcpServer::actionEnter(const QJsonObject &payload,
                                            bool *ok, QString *error){
    const QString plate = payload.value(QStringLiteral("plate")).toString().trimmed();
    const auto type = protocol::vehicleTypeFromString(
        payload.value(QStringLiteral("vehicleType")).toString(QStringLiteral("car")));
    if (plate.isEmpty() || !type.has_value()){
        *error = QStringLiteral("车牌或车辆类型无效");
        return {};
    }
    const auto result = service_->enter({plate.toStdString(), *type});
    if (!result){
        *error = QStringLiteral("入场失败：车辆已在场内或无可用车位");
        return {};
    }
    QJsonObject response = allocationToPayload(*result);
    QJsonObject eventPayload;
    eventPayload.insert(QStringLiteral("plate"), plate);
    eventPayload.insert(QStringLiteral("spotId"), QString::fromStdString(result->spotId));
    eventPayload.insert(QStringLiteral("entryTime"),
                        QDateTime::currentMSecsSinceEpoch());
    broadcastEvent(QStringLiteral("parking.entered"), eventPayload);
    *ok = true;
    return response;
}

QJsonObject SmartParkTcpServer::actionLeave(const QJsonObject &payload,
                                            bool *ok, QString *error){
    const QString plate = payload.value(QStringLiteral("plate")).toString().trimmed();
    const auto closed = service_->leave(plate.toStdString());
    if (!closed){
        *error = QStringLiteral("离场失败：车辆不在场内");
        return {};
    }
    QJsonObject response;
    response.insert(QStringLiteral("plate"), plate);
    response.insert(QStringLiteral("spotId"), QString::fromStdString(closed->spotId()));
    response.insert(QStringLiteral("fee"), closed->fee());
    response.insert(QStringLiteral("durationMin"),
                    static_cast<double>(std::chrono::duration_cast<
                        std::chrono::minutes>(closed->duration()).count()));
    QJsonObject eventPayload;
    eventPayload.insert(QStringLiteral("plate"), plate);
    eventPayload.insert(QStringLiteral("spotId"), QString::fromStdString(closed->spotId()));
    eventPayload.insert(QStringLiteral("fee"), closed->fee());
    broadcastEvent(QStringLiteral("parking.exited"), eventPayload);
    *ok = true;
    return response;
}

QJsonObject SmartParkTcpServer::actionReservationCreate(const QJsonObject &payload,
                                                        bool *ok, QString *error){
    const QString plate = payload.value(QStringLiteral("plate")).toString().trimmed();
    const auto type = protocol::vehicleTypeFromString(
        payload.value(QStringLiteral("vehicleType")).toString(QStringLiteral("car")));
    const qint64 startMs = payload.value(QStringLiteral("startMs")).toInteger();
    const int durationMin = payload.value(QStringLiteral("durationMin")).toInt(120);
    const bool accessible = payload.value(QStringLiteral("accessible")).toBool();
    if (plate.isEmpty() || !type.has_value()){
        *error = QStringLiteral("车牌或车辆类型无效");
        return {};
    }
    const auto start = msToTime(startMs);
    const auto end = start + std::chrono::minutes(durationMin);
    const auto created = service_->reservations().create(
        {plate.toStdString(), *type}, start, end, Clock::now(), accessible);
    if (!created){
        *error = QString::fromStdString(service_->reservations().lastError());
        return {};
    }
    // 订单字段与快照共用一份序列化：结束时间、宽限截止、状态都由服务端下发，
    // 客户端不必用「开始 + 时长」自己补算，到场窗口提示也不会跟服务端规则脱节。
    QJsonObject response = reservationToPayload(created->reservation);
    const ExpectedRoute &route = created->reservation.expectedRoute();
    response.insert(QStringLiteral("entranceIndex"), static_cast<int>(route.entranceIndex));
    response.insert(QStringLiteral("exitIndex"), static_cast<int>(route.exitIndex));
    response.insert(QStringLiteral("entryDistance"), route.entryRoute.distance);
    response.insert(QStringLiteral("exitDistance"), route.exitRoute.distance);
    response.insert(QStringLiteral("entryTurns"), route.entryRoute.turnCount);
    response.insert(QStringLiteral("exitTurns"), route.exitRoute.turnCount);
    QJsonArray entryPoints;
    for (const Point &point : route.entryRoute.points){
        entryPoints.append(QJsonObject{{QStringLiteral("x"), point.x},
                                       {QStringLiteral("y"), point.y}});
    }
    response.insert(QStringLiteral("entryPoints"), entryPoints);
    QJsonArray exitPoints;
    for (const Point &point : route.exitRoute.points){
        exitPoints.append(QJsonObject{{QStringLiteral("x"), point.x},
                                      {QStringLiteral("y"), point.y}});
    }
    response.insert(QStringLiteral("exitPoints"), exitPoints);
    QJsonObject eventPayload;
    eventPayload.insert(QStringLiteral("plate"), plate);
    eventPayload.insert(QStringLiteral("spotId"),
                        QString::fromStdString(created->reservation.spotId()));
    eventPayload.insert(QStringLiteral("accessible"), accessible);
    broadcastEvent(QStringLiteral("reservation.created"), eventPayload);
    *ok = true;
    return response;
}

QJsonObject SmartParkTcpServer::actionReservationCancel(const QJsonObject &payload,
                                                        bool *ok, QString *error){
    const QString plate = payload.value(QStringLiteral("plate")).toString().trimmed();
    if (!service_->reservations().cancel(plate.toStdString(), Clock::now())){
        *error = QString::fromStdString(service_->reservations().lastError());
        return {};
    }
    broadcastEvent(QStringLiteral("reservation.cancelled"),
                   QJsonObject{{QStringLiteral("plate"), plate}});
    *ok = true;
    return {};
}

QJsonObject SmartParkTcpServer::actionReservationCheckIn(const QJsonObject &payload,
                                                         bool *ok, QString *error){
    const QString plate = payload.value(QStringLiteral("plate")).toString().trimmed();
    const auto arrived = service_->reservations().checkIn(plate.toStdString(),
                                                          Clock::now());
    if (!arrived){
        *error = QString::fromStdString(service_->reservations().lastError());
        return {};
    }
    QJsonObject response;
    response.insert(QStringLiteral("spotId"), QString::fromStdString(arrived->spotId));
    // 到场后订单转 CheckedIn：把最新订单一起回给客户端，管理端不用再猜
    // （旧服务端只回 spotId，客户端会退回用快照缓存里的订单）。
    if (auto checkedIn = service_->reservations().findCheckedIn(plate.toStdString())){
        const QJsonObject order = reservationToPayload(*checkedIn);
        for (auto it = order.begin(); it != order.end(); ++it){
            response.insert(it.key(), it.value());
        }
    }
    broadcastEvent(QStringLiteral("reservation.checkin"),
                   QJsonObject{{QStringLiteral("plate"), plate},
                               {QStringLiteral("spotId"),
                                QString::fromStdString(arrived->spotId)}});
    *ok = true;
    return response;
}

// 预约订单的线上形状：snapshot 与 create/checkin 应答共用同一份字段名，
// 避免两处序列化漂移（客户端按同一套字段解析）。
QJsonObject SmartParkTcpServer::reservationToPayload(const Reservation &reservation){
    QJsonObject item;
    item.insert(QStringLiteral("id"), QString::fromStdString(reservation.id()));
    item.insert(QStringLiteral("reservationId"),
                QString::fromStdString(reservation.id()));
    item.insert(QStringLiteral("plate"),
                QString::fromStdString(reservation.plateNumber()));
    item.insert(QStringLiteral("vehicleType"),
                protocol::vehicleTypeToString(reservation.vehicleType()));
    item.insert(QStringLiteral("spotId"),
                QString::fromStdString(reservation.spotId()));
    item.insert(QStringLiteral("createdAtMs"), timeToMs(reservation.createdAt()));
    item.insert(QStringLiteral("startMs"), timeToMs(reservation.startTime()));
    item.insert(QStringLiteral("endMs"), timeToMs(reservation.endTime()));
    item.insert(QStringLiteral("graceDeadlineMs"),
                timeToMs(reservation.graceDeadline()));
    item.insert(QStringLiteral("deposit"), reservation.deposit());
    item.insert(QStringLiteral("status"), reservationStatusToInt(reservation.status()));
    item.insert(QStringLiteral("depositState"),
                depositStateToInt(reservation.depositState()));
    item.insert(QStringLiteral("accessible"), reservation.isAccessible());
    return item;
}

QJsonObject SmartParkTcpServer::actionGateReplay(const QJsonObject &payload,
                                                 bool *ok, QString *error){
    // Gate 断线补报：events = [{kind:"enter"|"exit", plate, vehicleType?, ts}]
    // 按原始时间戳追溯应用（ParkingService 支持注入进出场时间），
    // 返回逐条结果；任一失败不影响其余补报。
    const QJsonArray events = payload.value(QStringLiteral("events")).toArray();
    if (events.isEmpty() || events.size() > 500){
        *error = QStringLiteral("补报事件数量必须在 1 到 500 之间");
        return {};
    }
    QJsonArray results;
    int applied = 0;
    int duplicate = 0;
    int skipped = 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const QJsonValue &item : events){
        const QJsonObject event = item.toObject();
        const QString kind = event.value(QStringLiteral("kind")).toString();
        const QString plate = event.value(QStringLiteral("plate")).toString().trimmed();
        const qint64 ts = event.value(QStringLiteral("ts")).toInteger();
        QJsonObject itemResult{{QStringLiteral("plate"), plate},
                               {QStringLiteral("kind"), kind}};
        QString failure;
        if (plate.isEmpty() || !event.value(QStringLiteral("ts")).isDouble()
            || ts <= 0 || ts > now + 5 * 60 * 1000LL
            || ts < now - 30LL * 24 * 60 * 60 * 1000){
            failure = QStringLiteral("车牌或时间戳无效（仅接收过去 30 天事件）");
        } else if (kind != QStringLiteral("enter") && kind != QStringLiteral("exit")){
            failure = QStringLiteral("未知事件类型");
        } else{
            const auto plateText = plate.toStdString();
            const auto time = msToTime(ts);
            const auto sameRecord = [&](const ParkingRecord &record){
                if (record.plateNumber() != plateText) return false;
                const auto stamp = kind == QStringLiteral("enter")
                    ? std::optional<TimePoint>(record.entryTime()) : record.exitTime();
                return stamp && std::chrono::duration_cast<std::chrono::milliseconds>(
                    stamp->time_since_epoch()).count() == ts;
            };
            const auto &records = service_->records();
            if (std::any_of(records.begin(), records.end(), sameRecord)){
                itemResult.insert(QStringLiteral("duplicate"), true);
                ++duplicate;
            } else if (kind == QStringLiteral("enter")){
                const auto type = protocol::vehicleTypeFromString(event.value(
                    QStringLiteral("vehicleType")).toString(QStringLiteral("car")));
                const auto result = type ? service_->enter({plateText, *type}, time)
                                         : std::nullopt;
                if (result){
                    itemResult.insert(QStringLiteral("spotId"),
                                      QString::fromStdString(result->spotId));
                    ++applied;
                } else{
                    failure = QStringLiteral("追溯入场失败（可能已在场或无车位）");
                }
            } else{
                const auto closed = service_->leave(plateText, time);
                if (closed){
                    itemResult.insert(QStringLiteral("spotId"),
                                      QString::fromStdString(closed->spotId()));
                    itemResult.insert(QStringLiteral("fee"), closed->fee());
                    ++applied;
                } else{
                    failure = QStringLiteral("追溯离场失败（可能不在场或时间早于入场）");
                }
            }
        }
        itemResult.insert(QStringLiteral("ok"), failure.isEmpty());
        if (!failure.isEmpty()){
            itemResult.insert(QStringLiteral("error"), failure);
            ++skipped;
        }
        results.append(itemResult);
    }
    QJsonObject response;
    response.insert(QStringLiteral("applied"), applied);
    response.insert(QStringLiteral("duplicate"), duplicate);
    response.insert(QStringLiteral("skipped"), skipped);
    response.insert(QStringLiteral("results"), results);
    broadcastEvent(QStringLiteral("gate.replayed"),
                   QJsonObject{{QStringLiteral("applied"), applied},
                               {QStringLiteral("duplicate"), duplicate},
                               {QStringLiteral("skipped"), skipped}});
    *ok = true;
    return response;
}

QJsonObject SmartParkTcpServer::actionAnalyticsReport(const QJsonObject &,
                                                      bool *ok, QString *error){
    AnalyticsEngine engine(*service_);
    const auto report = engine.analyze();
    QJsonObject result;
    result.insert(QStringLiteral("model"), QString::fromStdString(report.model));
    result.insert(QStringLiteral("summary"), QString::fromStdString(report.summary));
    QJsonArray findings;
    for (const AnalysisFinding &finding : report.findings){
        QJsonObject item;
        item.insert(QStringLiteral("category"),
                    QString::fromLatin1(AnalysisReport::categoryText(finding.category)));
        item.insert(QStringLiteral("title"), QString::fromStdString(finding.title));
        item.insert(QStringLiteral("detail"), QString::fromStdString(finding.detail));
        findings.append(item);
    }
    result.insert(QStringLiteral("findings"), findings);
    QJsonArray recommendations;
    for (const std::string &recommendation : report.recommendations){
        recommendations.append(QString::fromStdString(recommendation));
    }
    result.insert(QStringLiteral("recommendations"), recommendations);
    *ok = true;
    return result;
}

QJsonObject SmartParkTcpServer::actionAdminSnapshot(const QJsonObject &,
                                                    bool *ok, QString *){
    // 管理端远程模式一次性快照：布局几何 + 车位明细 + 概览计数 + 分区统计。
    // 管理端据此绘制车位图与表格，无需本地第二个 ParkingService。
    const ParkingLayout &layout = service_->layout();

    QJsonObject layoutJson;
    layoutJson.insert(QStringLiteral("siteWidth"), layout.siteWidth());
    layoutJson.insert(QStringLiteral("siteHeight"), layout.siteHeight());
    // 与 Admin 本地 isGarageFloorplan 一致：58.0 x 42.4 视为 6 层车库平面图。
    layoutJson.insert(QStringLiteral("plan"),
                      std::abs(layout.siteWidth() - 58.0) < 0.25
                          && std::abs(layout.siteHeight() - 42.4) < 0.25
                          ? QStringLiteral("garage") : QStringLiteral("grid"));
    const auto pointJson = [](const Point &point){
        return QJsonObject{{QStringLiteral("x"), point.x},
                           {QStringLiteral("y"), point.y}};
    };
    QJsonArray entrances;
    for (const Point &point : layout.entrances()){
        entrances.append(pointJson(point));
    }
    layoutJson.insert(QStringLiteral("entrances"), entrances);
    QJsonArray exits;
    for (const Point &point : layout.exits()){
        exits.append(pointJson(point));
    }
    layoutJson.insert(QStringLiteral("exits"), exits);
    QJsonArray obstacles;
    for (const LayoutObstacle &obstacle : layout.obstacles()){
        obstacles.append(QJsonObject{
            {QStringLiteral("name"), QString::fromStdString(obstacle.name)},
            {QStringLiteral("x"), obstacle.bounds.origin.x},
            {QStringLiteral("y"), obstacle.bounds.origin.y},
            {QStringLiteral("w"), obstacle.bounds.width},
            {QStringLiteral("h"), obstacle.bounds.height}});
    }
    layoutJson.insert(QStringLiteral("obstacles"), obstacles);
    QJsonArray regions;
    for (const Rectangle &region : layout.regions()){
        regions.append(QJsonObject{
            {QStringLiteral("x"), region.origin.x},
            {QStringLiteral("y"), region.origin.y},
            {QStringLiteral("w"), region.width},
            {QStringLiteral("h"), region.height}});
    }
    layoutJson.insert(QStringLiteral("regions"), regions);

    QJsonArray spots;
    std::map<std::string, std::pair<int, int>> zoneStats;
    int occupied = 0;
    int reserved = 0;
    int disabled = 0;
    for (const ParkingSpot &spot : service_->spots()){
        auto &stat = zoneStats[spot.zone()];
        stat.first += 1;
        QJsonObject item;
        item.insert(QStringLiteral("spotId"),
                    QString::fromStdString(spot.identifier()));
        item.insert(QStringLiteral("zone"), QString::fromStdString(spot.zone()));
        item.insert(QStringLiteral("type"), QLatin1String(toString(spot.type())));
        item.insert(QStringLiteral("status"), static_cast<int>(spot.status()));
        if (spot.status() == SpotStatus::Occupied){
            ++occupied;
            stat.second += 1;
        } else if (spot.status() == SpotStatus::Reserved){
            ++reserved;
        } else if (spot.status() == SpotStatus::Disabled){
            ++disabled;
        }
        const auto &bounds = spot.bounds();
        item.insert(QStringLiteral("x"), bounds.origin.x);
        item.insert(QStringLiteral("y"), bounds.origin.y);
        item.insert(QStringLiteral("w"), bounds.width);
        item.insert(QStringLiteral("h"), bounds.height);
        if (spot.parkedVehicle()){
            item.insert(QStringLiteral("plate"),
                        QString::fromStdString(
                            spot.parkedVehicle()->plateNumber()));
            item.insert(QStringLiteral("vehicleType"),
                        protocol::vehicleTypeToString(spot.parkedVehicle()->type()));
        }
        spots.append(item);
    }

    QJsonArray zones;
    for (const auto &entry : zoneStats){
        zones.append(QJsonObject{{QStringLiteral("zone"),
                                  QString::fromStdString(entry.first)},
                                 {QStringLiteral("total"), entry.second.first},
                                 {QStringLiteral("occupied"), entry.second.second}});
    }

    QJsonArray dailyRevenue;
    const QDate today = QDate::currentDate();
    double fees[7]{};
    for (const ParkingRecord &record : service_->records()){
        if (!record.isClosed() || !record.exitTime()){
            continue;
        }
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            record.exitTime()->time_since_epoch()).count();
        const QDate exitDate = QDateTime::fromMSecsSinceEpoch(milliseconds).date();
        const qint64 daysAgo = exitDate.daysTo(today);
        if (daysAgo >= 0 && daysAgo < 7){
            fees[6 - daysAgo] += record.fee();
        }
    }
    for (int i = 0; i < 7; ++i){
        dailyRevenue.append(QJsonObject{
            {QStringLiteral("date"), today.addDays(i - 6).toString(Qt::ISODate)},
            {QStringLiteral("fee"), fees[i]}});
    }

    // 停车记录与预约：远程模式要能显示「停车记录」「预约管理」两页，
    // 并在客户端本地算出洞察（预测/分区压力），所以快照把它们一起带上——
    // 这些数据量很小，多一次拉取比多一套增量接口简单得多。
    QJsonArray recordsJson;
    for (const ParkingRecord &record : service_->records()){
        QJsonObject item;
        item.insert(QStringLiteral("plate"),
                    QString::fromStdString(record.plateNumber()));
        item.insert(QStringLiteral("spotId"),
                    QString::fromStdString(record.spotId()));
        item.insert(QStringLiteral("vehicleType"),
                    protocol::vehicleTypeToString(record.vehicleType()));
        item.insert(QStringLiteral("entryTimeMs"), timeToMs(record.entryTime()));
        if (record.exitTime()){
            item.insert(QStringLiteral("exitTimeMs"), timeToMs(*record.exitTime()));
        }
        item.insert(QStringLiteral("fee"), record.fee());
        recordsJson.append(item);
    }

    QJsonArray bookingsJson;
    for (const Booking &booking : service_->bookings()){
        QJsonObject item;
        item.insert(QStringLiteral("id"), QString::fromStdString(booking.id()));
        item.insert(QStringLiteral("plate"),
                    QString::fromStdString(booking.plateNumber()));
        item.insert(QStringLiteral("spotId"),
                    QString::fromStdString(booking.spotId()));
        item.insert(QStringLiteral("createdAtMs"), timeToMs(booking.createdAt()));
        item.insert(QStringLiteral("arrivalMs"), timeToMs(booking.arrivalTime()));
        item.insert(QStringLiteral("deadlineMs"), timeToMs(booking.arrivalDeadline()));
        item.insert(QStringLiteral("deposit"), booking.deposit());
        item.insert(QStringLiteral("status"), bookingStatusToInt(booking.status()));
        bookingsJson.append(item);
    }

    // 时段预约（Reservation，0.7 模型）：与 Booking 并存的两代功能。
    // 网页 H5、用户端 CLI、reservation.* 动作与管理端「预约管理」页的表单
    // 写的都是这张表；Booking 只剩历史数据。快照把两者一起带上。
    QJsonArray reservationsJson;
    for (const Reservation &reservation : service_->reservations().reservations()){
        reservationsJson.append(reservationToPayload(reservation));
    }
    // 预约规则：管理端表单据此提示定金、提前量、最短时长与到场窗口。
    const ReservationRule &rule = service_->reservations().rule();

    QJsonObject result;
    result.insert(QStringLiteral("reservations"), reservationsJson);
    result.insert(QStringLiteral("reservationRule"),
                  QJsonObject{{QStringLiteral("deposit"), rule.deposit},
                              {QStringLiteral("maxAdvanceDays"), rule.maxAdvanceDays},
                              {QStringLiteral("minLeadTimeMin"),
                               static_cast<int>(rule.minLeadTime.count())},
                              {QStringLiteral("minDurationMin"),
                               static_cast<int>(rule.minDuration.count())},
                              {QStringLiteral("gracePeriodMin"),
                               static_cast<int>(rule.gracePeriod.count())},
                              {QStringLiteral("lockLeadTimeMin"),
                               static_cast<int>(rule.lockLeadTime.count())}});
    result.insert(QStringLiteral("dailyRevenue"), dailyRevenue);
    result.insert(QStringLiteral("layout"), layoutJson);
    result.insert(QStringLiteral("spots"), spots);
    result.insert(QStringLiteral("zones"), zones);
    result.insert(QStringLiteral("records"), recordsJson);
    result.insert(QStringLiteral("bookings"), bookingsJson);
    // 定金口径与服务端一致：远程端不再自己猜一个 0。
    result.insert(QStringLiteral("pendingDeposits"), service_->pendingDeposits());
    result.insert(QStringLiteral("forfeitedDeposits"), service_->forfeitedDeposits());
    result.insert(QStringLiteral("capacity"),
                  static_cast<int>(service_->spots().size()));
    result.insert(QStringLiteral("occupied"), occupied);
    result.insert(QStringLiteral("reserved"), reserved);
    result.insert(QStringLiteral("disabled"), disabled);
    // 与 parking.status 的 remainingSpots 口径一致：空闲不把停用算进去。
    result.insert(QStringLiteral("available"),
                  static_cast<int>(service_->spots().size()) - occupied
                      - reserved - disabled);
    result.insert(QStringLiteral("generatedAtMs"),
                  QDateTime::currentMSecsSinceEpoch());
    *ok = true;
    return result;
}

} // namespace smartpark
