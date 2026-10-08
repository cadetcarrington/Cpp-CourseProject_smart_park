#include "network/RestGateway.h"

#include "core/service/AuditLogService.h"
#include "core/service/ReservationService.h"
#include "core/service/UserStore.h"
#include "network/EventHub.h"

#include <QHttpHeaders>
#include "network/PlateRecognition.h"
#include "network/Protocol.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryFile>
#include <QTcpServer>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocket>
#include <QWebSocketServer>

#include <qrcodegen.hpp>

#include <algorithm>
#include <cmath>

namespace smartpark{
namespace{
using Clock = ParkingRecord::Clock;
using TimePoint = ParkingRecord::TimePoint;

TimePoint msToTime(qint64 ms){
    return TimePoint{} + std::chrono::milliseconds(ms);
}

qint64 timeToMs(const TimePoint &time){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        time.time_since_epoch()).count();
}

QString vehicleTypeText(VehicleType type){
    return protocol::vehicleTypeToString(type);
}

QString reservationStatusText(ReservationStatus status){
    switch (status){
    case ReservationStatus::PendingPayment: return QStringLiteral("pendingPayment");
    case ReservationStatus::Confirmed: return QStringLiteral("confirmed");
    case ReservationStatus::CheckedIn: return QStringLiteral("checkedIn");
    case ReservationStatus::Completed: return QStringLiteral("completed");
    case ReservationStatus::Cancelled: return QStringLiteral("cancelled");
    case ReservationStatus::NoShow: return QStringLiteral("noShow");
    case ReservationStatus::Expired: return QStringLiteral("expired");
    }
    return QStringLiteral("unknown");
}

QString depositStateText(DepositState state){
    switch (state){
    case DepositState::Pending: return QStringLiteral("pending");
    case DepositState::Refunded: return QStringLiteral("refunded");
    case DepositState::Forfeited: return QStringLiteral("forfeited");
    case DepositState::Applied: return QStringLiteral("applied");
    }
    return QStringLiteral("unknown");
}

QString randomHex(int bytes){
    QByteArray raw(bytes, 0);
    QRandomGenerator::system()->fillRange(
        reinterpret_cast<quint32 *>(raw.data()), bytes / 4);
    return QString::fromLatin1(raw.toHex());
}

QByteArray mimeTypeForPath(const QString &path){
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("html") || suffix == QStringLiteral("htm")){
        return QByteArrayLiteral("text/html; charset=utf-8");
    }
    if (suffix == QStringLiteral("js")){
        return QByteArrayLiteral("text/javascript; charset=utf-8");
    }
    if (suffix == QStringLiteral("css")){
        return QByteArrayLiteral("text/css; charset=utf-8");
    }
    if (suffix == QStringLiteral("json") || suffix == QStringLiteral("map")){
        return QByteArrayLiteral("application/json");
    }
    if (suffix == QStringLiteral("png")){
        return QByteArrayLiteral("image/png");
    }
    if (suffix == QStringLiteral("svg")){
        return QByteArrayLiteral("image/svg+xml");
    }
    if (suffix == QStringLiteral("ico")){
        return QByteArrayLiteral("image/x-icon");
    }
    if (suffix == QStringLiteral("woff2")){
        return QByteArrayLiteral("font/woff2");
    }
    return QByteArrayLiteral("application/octet-stream");
}

// 统一的安全响应头。API 走 afterRequestHandler 自动附加；
// 静态页走 setMissingHandler + responder.write，不经过那条链，
// 必须用 write() 的 QHttpHeaders 重载显式带上。
void appendSecurityHeaders(QHttpHeaders &headers){
    headers.append(
        QByteArrayLiteral("Content-Security-Policy"),
        QByteArrayLiteral("default-src 'self'; script-src 'self'; "
                          "style-src 'self' 'unsafe-inline'; "
                          "img-src 'self' data: blob:; "
                          "connect-src 'self' ws: wss:; "
                          "object-src 'none'; base-uri 'none'; "
                          "frame-ancestors 'none'; form-action 'none'"));
    headers.append(QByteArrayLiteral("X-Content-Type-Options"),
                   QByteArrayLiteral("nosniff"));
    headers.append(QByteArrayLiteral("Referrer-Policy"),
                   QByteArrayLiteral("no-referrer"));
    headers.append(QByteArrayLiteral("X-Frame-Options"),
                   QByteArrayLiteral("DENY"));
}

// 静态/JSON 直写用的头集合（安全头 + 显式 Content-Type，
// 因为 write(data, headers, status) 不会替你推断类型）。
QHttpHeaders headersFor(const QByteArray &mimeType){
    QHttpHeaders headers;
    appendSecurityHeaders(headers);
    headers.append(QByteArrayLiteral("Content-Type"), mimeType);
    return headers;
}
} // namespace

RestGateway::RestGateway(ParkingService &service, UserStore &users,
                         AuditLogService *audit, const QSqlDatabase &database,
                         EventHub *hub, Options options, QObject *parent)
    : QObject(parent)
    , service_(&service)
    , users_(&users)
    , audit_(audit)
    , hub_(hub)
    , database_(database)
    , options_(options){
    ensureOrderSchema();
    ensureFrictionlessSchema();
    loadOrders();
    setupHttp();
    if (hub_ != nullptr){
        connect(hub_, &EventHub::eventOccurred,
                this, &RestGateway::broadcastWs);
        // 无感支付：任何入口（Gate 出口/REST 离场）广播 parking.exited 后，
        // 已开通车牌自动生成已支付订单并广播 payment.paid。
        connect(hub_, &EventHub::eventOccurred, this,
                [this](const QString &name, const QJsonObject &payload){
            if (name != QStringLiteral("parking.exited")){
                return;
            }
            const QString plate =
                payload.value(QStringLiteral("plate")).toString();
            const double fee = payload.value(QStringLiteral("fee")).toDouble();
            autoChargeOnExit(plate, fee);
        });
    }
    ensureSiteSchema();
    siteToken_ = ensureDefaultSiteTicket();
    sweepTimer_.setInterval(15000);
    connect(&sweepTimer_, &QTimer::timeout, this, &RestGateway::sweep);
}

RestGateway::~RestGateway() = default;

bool RestGateway::listen(){
    if (!http_){
        lastError_ = QStringLiteral("HTTP 服务未初始化");
        return false;
    }
    httpListener_ = new QTcpServer(this);
    if (!httpListener_->listen(QHostAddress::Any, options_.httpPort)){
        lastError_ = httpListener_->errorString();
        delete httpListener_;
        httpListener_ = nullptr;
        return false;
    }
    if (!http_->bind(httpListener_)){
        lastError_ = QStringLiteral("HTTP 服务绑定失败");
        delete httpListener_;
        httpListener_ = nullptr;
        return false;
    }
    httpPort_ = httpListener_->serverPort();
    if (options_.wsPort != 0 && !setupWebSocket()){
        return false;
    }
    sweepTimer_.start();
    return true;
}

quint16 RestGateway::httpPort() const noexcept{
    return httpPort_;
}

quint16 RestGateway::wsPort() const noexcept{
    return wsPort_;
}

const QString &RestGateway::lastError() const noexcept{
    return lastError_;
}

// ---- 初始化 ----

void RestGateway::setupHttp(){
    http_ = std::make_unique<QHttpServer>(this);

    // 统一给每个响应加安全头。H5 与 API 同源伺服，页面里的 XSS 只能靠
    // 转义从源头堵；CSP 是第二道防线。
    // 注意 style-src 必须放开 'unsafe-inline'：模板里大量使用 style="..." 属性。
    // connect-src 放开 ws:/wss:：推送走独立端口（--ws-port），'self' 覆盖不到。
    http_->addAfterRequestHandler(
        this, [](const QHttpServerRequest &, QHttpServerResponse &response){
        // 先取出原有头再追加：setHeaders 是整体替换，
        // 直接塞新集合会把 Content-Type 一起抹掉。
        QHttpHeaders headers = response.headers();
        appendSecurityHeaders(headers);
        response.setHeaders(headers);
    });

    http_->route(QStringLiteral("/api/v1/meta"), QHttpServerRequest::Method::Get,
                 [this](const QHttpServerRequest &request){
                     return handleMeta(request);
                 });
    http_->route(QStringLiteral("/api/v1/auth/register"),
                 QHttpServerRequest::Method::Post,
                 [this](const QHttpServerRequest &request){
                     return handleRegister(request);
                 });
    http_->route(QStringLiteral("/api/v1/auth/login"),
                 QHttpServerRequest::Method::Post,
                 [this](const QHttpServerRequest &request){
                     return handleLogin(request);
                 });
    http_->route(QStringLiteral("/api/v1/auth/refresh"),
                 QHttpServerRequest::Method::Post,
                 [this](const QHttpServerRequest &request){
                     return handleRefresh(request);
                 });
    http_->route(QStringLiteral("/api/v1/auth/logout"),
                 QHttpServerRequest::Method::Post,
                 [this](const QHttpServerRequest &request){
                     return handleLogout(request);
                 });
    http_->route(QStringLiteral("/api/v1/me"), QHttpServerRequest::Method::Get,
                 [this](const QHttpServerRequest &request){
                     return handleMe(request);
                 });
    http_->route(QStringLiteral("/api/v1/parking/status"),
                 QHttpServerRequest::Method::Get,
                 [this](const QHttpServerRequest &request){
                     return handleStatus(request);
                 });
    http_->route(QStringLiteral("/api/v1/spots"), QHttpServerRequest::Method::Get,
                 [this](const QHttpServerRequest &request){
                     return handleSpots(request);
                 });
    http_->route(QStringLiteral("/api/v1/layout"), QHttpServerRequest::Method::Get,
                 [this](const QHttpServerRequest &request){
                     return handleLayout(request);
                 });
    http_->route(QStringLiteral("/api/v1/records"), QHttpServerRequest::Method::Get,
                 [this](const QHttpServerRequest &request){
                     return handleRecords(request);
                 });
    http_->route(QStringLiteral("/api/v1/records/<arg>/active"),
                 QHttpServerRequest::Method::Get,
                 [this](const QString &plate, const QHttpServerRequest &request){
                     return handleActiveRecord(plate, request);
                 });
    http_->route(QStringLiteral("/api/v1/parking/enter"),
                 QHttpServerRequest::Method::Post,
                 [this](const QHttpServerRequest &request){
                     return handleEnter(request);
                 });
    http_->route(QStringLiteral("/api/v1/parking/leave"),
                 QHttpServerRequest::Method::Post,
                 [this](const QHttpServerRequest &request){
                     return handleLeave(request);
                 });
    http_->route(QStringLiteral("/api/v1/reservations"),
                 QHttpServerRequest::Method::Post,
                 [this](const QHttpServerRequest &request){
                     return handleReservationCreate(request);
                 });
    http_->route(QStringLiteral("/api/v1/reservations"),
                 QHttpServerRequest::Method::Get,
                 [this](const QHttpServerRequest &request){
                     return handleReservationList(request);
                 });
    http_->route(QStringLiteral("/api/v1/reservations/<arg>/checkin"),
                 QHttpServerRequest::Method::Post,
                 [this](const QString &id, const QHttpServerRequest &request){
                     return handleReservationCheckIn(id, request);
                 });
    http_->route(QStringLiteral("/api/v1/reservations/<arg>/cancel"),
                 QHttpServerRequest::Method::Post,
                 [this](const QString &id, const QHttpServerRequest &request){
                     return handleReservationCancel(id, request);
                 });
    http_->route(QStringLiteral("/api/v1/payments/orders"),
                 QHttpServerRequest::Method::Post,
                 [this](const QHttpServerRequest &request){
                     return handleOrderCreate(request);
                 });
    http_->route(QStringLiteral("/api/v1/payments/orders/<arg>"),
                 QHttpServerRequest::Method::Get,
                 [this](const QString &id, const QHttpServerRequest &request){
                     return handleOrderGet(id, request);
                 });
    http_->route(QStringLiteral("/api/v1/payments/orders/<arg>/confirm"),
                 QHttpServerRequest::Method::Post,
                 [this](const QString &id, const QHttpServerRequest &request){
                     return handleOrderConfirm(id, request);
                 });
    http_->route(QStringLiteral("/api/v1/guide/<arg>"),
                 QHttpServerRequest::Method::Get,
                 [this](const QString &plate, const QHttpServerRequest &request){
                     return handleGuide(plate, request);
                 });
    // 以下三个端点中，qr 免认证（<img> 无法携带 Bearer）；
    // orders 列表与 frictionless/lpr 走标准鉴权。
    http_->route(QStringLiteral("/api/v1/qr"), QHttpServerRequest::Method::Get,
                 [this](const QHttpServerRequest &request){
                     return handleQr(request);
                 });
    http_->route(QStringLiteral("/api/v1/payments/orders"),
                 QHttpServerRequest::Method::Get,
                 [this](const QHttpServerRequest &request){
                     return handleOrderList(request);
                 });
    http_->route(QStringLiteral("/api/v1/me/frictionless"),
                 QHttpServerRequest::Method::Get,
                 [this](const QHttpServerRequest &request){
                     return handleFrictionlessList(request);
                 });
    http_->route(QStringLiteral("/api/v1/me/frictionless"),
                 QHttpServerRequest::Method::Post,
                 [this](const QHttpServerRequest &request){
                     return handleFrictionlessToggle(request);
                 });
    http_->route(QStringLiteral("/api/v1/lpr/recognize"),
                 QHttpServerRequest::Method::Post,
                 [this](const QHttpServerRequest &request){
                     return handleLprRecognize(request);
                 });

    // 点位票据解析：扫码后、登录前就要知道扫的是哪个车场，因此免认证。
    http_->route(QStringLiteral("/api/v1/site/resolve"),
                 QHttpServerRequest::Method::Get,
                 [this](const QHttpServerRequest &request){
                     return handleSiteResolve(request);
                 });
    http_->route(QStringLiteral("/api/v1/me/plates"),
                 QHttpServerRequest::Method::Get,
                 [this](const QHttpServerRequest &request){
                     return handlePlateList(request);
                 });
    http_->route(QStringLiteral("/api/v1/me/plates"),
                 QHttpServerRequest::Method::Post,
                 [this](const QHttpServerRequest &request){
                     return handlePlateBind(request);
                 });

    // 未命中路由：/api/* 回 JSON 404；其余按静态页伺服（SPA 回退 index.html）。
    http_->setMissingHandler(this,
                             [this](const QHttpServerRequest &request,
                                    QHttpServerResponder &responder){
        const QString path = request.url().path();
        if (path.startsWith(QLatin1String("/api/"))){
            responder.write(
                QJsonDocument(errorObject("NOT_FOUND", QStringLiteral("接口不存在"))),
                headersFor(QByteArrayLiteral("application/json")),
                QHttpServerResponder::StatusCode::NotFound);
            return;
        }
        if (options_.webRoot.isEmpty()){
            responder.write(
                QJsonDocument(errorObject(
                    "NOT_FOUND",
                    QStringLiteral("未配置 Web 静态目录（--web-root）"))),
                headersFor(QByteArrayLiteral("application/json")),
                QHttpServerResponder::StatusCode::NotFound);
            return;
        }
        QString relative = QDir::cleanPath(path);
        if (relative == QStringLiteral(".") || relative == QStringLiteral("/")){
            relative = QStringLiteral("/index.html");
        }
        if (relative.contains(QStringLiteral(".."))){
            responder.write(
                QJsonDocument(errorObject("FORBIDDEN", QStringLiteral("非法路径"))),
                headersFor(QByteArrayLiteral("application/json")),
                QHttpServerResponder::StatusCode::Forbidden);
            return;
        }
        // mime 必须按「实际送出的文件」算，不能按请求路径算：
        // SPA 回退时请求的是 /some-route（无扩展名），送出的却是 index.html，
        // 按请求路径会得到 application/octet-stream，加上 nosniff 后
        // 浏览器会拒绝把它当 HTML 渲染。
        QString served = relative;
        QFile file(options_.webRoot + relative);
        if (!file.exists()
            && QFileInfo(relative).suffix().isEmpty()){
            // SPA 路由回退：无扩展名的路径一律返回入口页。
            served = QStringLiteral("/index.html");
            file.setFileName(options_.webRoot + served);
        }
        if (!file.open(QIODevice::ReadOnly)){
            responder.write(
                QJsonDocument(errorObject("NOT_FOUND", QStringLiteral("页面不存在"))),
                headersFor(QByteArrayLiteral("application/json")),
                QHttpServerResponder::StatusCode::NotFound);
            return;
        }
        responder.write(&file, headersFor(mimeTypeForPath(served)),
                        QHttpServerResponder::StatusCode::Ok);
    });
}

bool RestGateway::setupWebSocket(){
    ws_ = new QWebSocketServer(QStringLiteral("SmartPark WS"),
                               QWebSocketServer::NonSecureMode, this);
    if (!ws_->listen(QHostAddress::Any, options_.wsPort)){
        lastError_ = ws_->errorString();
        ws_->deleteLater();
        ws_ = nullptr;
        return false;
    }
    wsPort_ = ws_->serverPort();
    connect(ws_, &QWebSocketServer::newConnection, this, [this]{
        while (QWebSocket *socket = ws_->nextPendingConnection()){
            WsSession session;
            session.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
            wsSessions_.insert(socket, session);
            connect(socket, &QWebSocket::textMessageReceived, this,
                    [this, socket](const QString &message){
                const auto it = wsSessions_.find(socket);
                if (it == wsSessions_.end()){
                    return;
                }
                it.value().lastSeenMs = QDateTime::currentMSecsSinceEpoch();
                const QJsonObject frame =
                    QJsonDocument::fromJson(message.toUtf8()).object();
                const QString type =
                    frame.value(QStringLiteral("type")).toString();
                if (!it.value().authenticated){
                    if (type == QStringLiteral("auth")){
                        const auto identity = users_->verifyToken(
                            frame.value(QStringLiteral("token")).toString());
                        if (identity.has_value()){
                            it.value().authenticated = true;
                            it.value().username = identity->username;
                            it.value().role = identity->role;
                            socket->sendTextMessage(
                                QString::fromUtf8(QJsonDocument(QJsonObject{
                                    {QStringLiteral("type"),
                                     QStringLiteral("auth")},
                                    {QStringLiteral("ok"), true}})
                                                     .toJson(QJsonDocument::Compact)));
                        } else{
                            socket->sendTextMessage(
                                QString::fromUtf8(QJsonDocument(QJsonObject{
                                    {QStringLiteral("type"),
                                     QStringLiteral("error")},
                                    {QStringLiteral("code"),
                                     QStringLiteral("AUTH_REQUIRED")}})
                                                     .toJson(QJsonDocument::Compact)));
                        }
                    } else if (type == QStringLiteral("ping")){
                        socket->sendTextMessage(
                            QString::fromUtf8(QJsonDocument(QJsonObject{
                                {QStringLiteral("type"), QStringLiteral("pong")},
                                {QStringLiteral("ts"),
                                 QDateTime::currentMSecsSinceEpoch()}})
                                             .toJson(QJsonDocument::Compact)));
                    }
                    return;
                }
                if (type == QStringLiteral("ping")){
                    socket->sendTextMessage(
                        QString::fromUtf8(QJsonDocument(QJsonObject{
                            {QStringLiteral("type"), QStringLiteral("pong")},
                            {QStringLiteral("ts"),
                             QDateTime::currentMSecsSinceEpoch()}})
                                         .toJson(QJsonDocument::Compact)));
                }
            });
            connect(socket, &QWebSocket::disconnected, this, [this, socket]{
                wsSessions_.remove(socket);
                socket->deleteLater();
            });
        }
    });
    return true;
}

void RestGateway::ensureOrderSchema(){
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS payment_orders ("
            "order_id TEXT PRIMARY KEY,"
            "out_trade_no TEXT NOT NULL UNIQUE,"
            "kind TEXT NOT NULL CHECK(kind IN ('deposit','parking_fee')),"
            "username TEXT NOT NULL,"
            "plate TEXT NOT NULL,"
            "reservation_id TEXT,"
            "amount REAL NOT NULL,"
            "status TEXT NOT NULL"
            " CHECK(status IN ('pending','paid','expired','refunded','cancelled')),"
            "created_at_ms INTEGER NOT NULL,"
            "expire_at_ms INTEGER NOT NULL,"
            "paid_at_ms INTEGER,"
            "refund_at_ms INTEGER,"
            "payload TEXT NOT NULL DEFAULT '{}')"))){
        lastError_ = query.lastError().text();
    }
}

void RestGateway::loadOrders(){
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT order_id, out_trade_no, kind, username, plate, reservation_id,"
            " amount, status, created_at_ms, expire_at_ms, paid_at_ms, refund_at_ms"
            " FROM payment_orders"))){
        lastError_ = query.lastError().text();
        return;
    }
    while (query.next()){
        PaymentOrder order;
        order.orderId = query.value(0).toString();
        order.outTradeNo = query.value(1).toString();
        order.kind = query.value(2).toString();
        order.username = query.value(3).toString();
        order.plate = query.value(4).toString();
        order.reservationId = query.value(5).toString();
        order.amount = query.value(6).toDouble();
        order.status = query.value(7).toString();
        order.createdAtMs = query.value(8).toLongLong();
        order.expireAtMs = query.value(9).toLongLong();
        order.paidAtMs = query.value(10).toLongLong();
        order.refundAtMs = query.value(11).toLongLong();
        orders_.insert(order.orderId, order);
    }
}

// ---- 鉴权与辅助 ----

std::optional<QString> RestGateway::bearerToken(
    const QHttpServerRequest &request) const{
    const QByteArray header = request.value(QByteArrayLiteral("Authorization"));
    if (!header.startsWith(QByteArrayLiteral("Bearer "))){
        return std::nullopt;
    }
    const QString token = QString::fromUtf8(header.mid(7)).trimmed();
    if (token.isEmpty()){
        return std::nullopt;
    }
    return token;
}

std::optional<RestGateway::AuthContext> RestGateway::authenticate(
    const QHttpServerRequest &request) const{
    const auto token = bearerToken(request);
    if (!token.has_value()){
        return std::nullopt;
    }
    const auto identity = users_->verifyToken(*token);
    if (!identity.has_value()){
        return std::nullopt;
    }
    return AuthContext{identity->username, identity->role};
}

std::optional<QJsonObject> RestGateway::bodyJson(
    const QHttpServerRequest &request) const{
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(request.body(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()){
        return std::nullopt;
    }
    return document.object();
}

QJsonObject RestGateway::errorObject(const char *code,
                                     const QString &message) const{
    return QJsonObject{{QStringLiteral("code"), QLatin1String(code)},
                       {QStringLiteral("message"), message}};
}

QHttpServerResponse RestGateway::jsonError(
    QHttpServerResponder::StatusCode status, const char *code,
    const QString &message) const{
    return QHttpServerResponse(errorObject(code, message), status);
}

// ---- 路由 handler：账号 ----

QHttpServerResponse RestGateway::handleMeta(
    const QHttpServerRequest &request) const{
    QString host = QString::fromUtf8(
        request.value(QByteArrayLiteral("Host")));
    const int colon = host.lastIndexOf(QLatin1Char(':'));
    if (colon > 0){
        host = host.left(colon);
    }
    if (host.isEmpty()){
        host = QStringLiteral("127.0.0.1");
    }
    QJsonObject meta;
    meta.insert(QStringLiteral("name"), QStringLiteral("SmartPark"));
    meta.insert(QStringLiteral("apiVersion"), 1);
    meta.insert(QStringLiteral("maxV"), 1);
    meta.insert(QStringLiteral("paymentMode"), QStringLiteral("mock"));
    meta.insert(QStringLiteral("serverTimeMs"),
                QDateTime::currentMSecsSinceEpoch());
    meta.insert(QStringLiteral("httpPort"), httpPort_);
    if (wsPort_ != 0){
        meta.insert(QStringLiteral("wsPort"), wsPort_);
        // 默认按「请求的 Host + 内部 ws 端口」拼，直连时是对的：手机访问
        // 10.0.0.5:8080 就拿到 ws://10.0.0.5:8081/ws。
        // 但反代（尤其 HTTPS）部署下 Host 是对外域名、ws 端口并不对外，
        // 浏览器要么连不上要么按混合内容拦掉——此时用 --ws-public-url 覆盖。
        meta.insert(QStringLiteral("wsUrl"),
                    options_.wsPublicUrl.isEmpty()
                        ? QStringLiteral("ws://%1:%2/ws").arg(host).arg(wsPort_)
                        : options_.wsPublicUrl);
    }
    return QHttpServerResponse(meta);
}

QHttpServerResponse RestGateway::handleRegister(
    const QHttpServerRequest &request){
    const auto body = bodyJson(request);
    if (!body.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("请求体必须是 JSON 对象"));
    }
    const QString userName =
        body->value(QStringLiteral("username")).toString().trimmed();
    const QString password = body->value(QStringLiteral("password")).toString();
    const auto result = users_->registerUser(userName, password);
    if (result != UserStore::RegisterResult::Success){
        const auto status =
            result == UserStore::RegisterResult::DuplicateUser
                ? QHttpServerResponder::StatusCode::Conflict
                : QHttpServerResponder::StatusCode::BadRequest;
        return jsonError(status,
                         result == UserStore::RegisterResult::DuplicateUser
                             ? "CONFLICT" : "VALIDATION",
                         UserStore::registerErrorText(result));
    }
    if (audit_ != nullptr){
        audit_->record(userName.toStdString(), "rest_register");
    }
    return QHttpServerResponse(
        QJsonObject{{QStringLiteral("username"), userName},
                    {QStringLiteral("role"), QStringLiteral("user")}},
        QHttpServerResponder::StatusCode::Created);
}

QHttpServerResponse RestGateway::handleLogin(
    const QHttpServerRequest &request){
    const auto body = bodyJson(request);
    if (!body.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("请求体必须是 JSON 对象"));
    }
    const QString userName =
        body->value(QStringLiteral("username")).toString().trimmed();
    const QString password = body->value(QStringLiteral("password")).toString();
    const auto result = users_->verifyLogin(userName, password);
    if (result == UserStore::LoginResult::Locked){
        const qint64 remainder = users_->lockedRemainderMs(userName);
        QJsonObject payload = errorObject(
            "AUTH_LOCKED",
            QStringLiteral("账号已临时锁定，请 %1 秒后再试")
                .arg((remainder + 999) / 1000));
        payload.insert(QStringLiteral("remainMs"), remainder);
        return QHttpServerResponse(
            payload, QHttpServerResponder::StatusCode::Locked);
    }
    if (result != UserStore::LoginResult::Success){
        if (audit_ != nullptr){
            audit_->record(userName.toStdString(), "rest_login_failed");
        }
        const bool authProblem =
            result == UserStore::LoginResult::UnknownUser
            || result == UserStore::LoginResult::WrongPassword;
        // 统一口径防账号枚举：不存在与密码错误返回相同错误。
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         authProblem ? "AUTH_FAILED" : "VALIDATION",
                         authProblem
                             ? QStringLiteral("账号或密码错误")
                             : UserStore::loginErrorText(result));
    }
    const QString token = users_->issueToken(userName, options_.tokenTtlMs);
    if (token.isEmpty()){
        return jsonError(QHttpServerResponder::StatusCode::InternalServerError,
                         "INTERNAL", QStringLiteral("token 签发失败"));
    }
    if (audit_ != nullptr){
        audit_->record(userName.toStdString(), "rest_login_success");
    }
    QJsonObject user;
    user.insert(QStringLiteral("username"), userName);
    user.insert(QStringLiteral("role"), users_->roleOf(userName));
    QJsonObject payload;
    payload.insert(QStringLiteral("token"), token);
    payload.insert(QStringLiteral("expiresAtMs"),
                   QDateTime::currentMSecsSinceEpoch() + options_.tokenTtlMs);
    payload.insert(QStringLiteral("user"), user);
    return QHttpServerResponse(payload);
}

QHttpServerResponse RestGateway::handleRefresh(
    const QHttpServerRequest &request){
    const auto token = bearerToken(request);
    if (!token.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少 Bearer token"));
    }
    const auto identity = users_->verifyToken(*token);
    if (!identity.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_EXPIRED", QStringLiteral("token 已过期或无效"));
    }
    const QString fresh = users_->issueToken(identity->username,
                                             options_.tokenTtlMs);
    if (fresh.isEmpty()){
        return jsonError(QHttpServerResponder::StatusCode::InternalServerError,
                         "INTERNAL", QStringLiteral("token 签发失败"));
    }
    users_->revokeToken(*token);
    return QHttpServerResponse(
        QJsonObject{{QStringLiteral("token"), fresh},
                    {QStringLiteral("expiresAtMs"),
                     QDateTime::currentMSecsSinceEpoch() + options_.tokenTtlMs}});
}

QHttpServerResponse RestGateway::handleLogout(
    const QHttpServerRequest &request){
    const auto token = bearerToken(request);
    if (token.has_value()){
        const auto identity = users_->verifyToken(*token);
        if (audit_ != nullptr && identity.has_value()){
            audit_->record(identity->username.toStdString(), "rest_logout");
        }
        users_->revokeToken(*token);
    }
    return QHttpServerResponse(QJsonObject{});
}

QHttpServerResponse RestGateway::handleMe(const QHttpServerRequest &request){
    const auto auth = authenticate(request);
    if (!auth.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    return QHttpServerResponse(
        QJsonObject{{QStringLiteral("username"), auth->username},
                    {QStringLiteral("role"), auth->role}});
}

// ---- 路由 handler：车位与记录 ----

QHttpServerResponse RestGateway::handleStatus(const QHttpServerRequest &request){
    if (!authenticate(request).has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    return QHttpServerResponse(statusPayload());
}

QHttpServerResponse RestGateway::handleSpots(const QHttpServerRequest &request){
    if (!authenticate(request).has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    return QHttpServerResponse(spotListPayload(false));
}

QHttpServerResponse RestGateway::handleLayout(const QHttpServerRequest &request){
    if (!authenticate(request).has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    return QHttpServerResponse(layoutPayload());
}

QHttpServerResponse RestGateway::handleRecords(
    const QHttpServerRequest &request){
    const auto auth = authenticate(request);
    if (!auth.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    const QUrlQuery query(request.url().query());
    const QString plate = query.queryItemValue(QStringLiteral("plate"));
    const QString state = query.queryItemValue(QStringLiteral("state"));
    int page = query.queryItemValue(QStringLiteral("page")).toInt();
    int pageSize = query.queryItemValue(QStringLiteral("pageSize")).toInt();
    if (page < 1){
        page = 1;
    }
    if (pageSize < 1 || pageSize > 100){
        pageSize = 20;
    }
    std::vector<const ParkingRecord *> filtered;
    for (const ParkingRecord &record : service_->records()){
        if (!plate.isEmpty()
            && record.plateNumber() != plate.toStdString()){
            continue;
        }
        if (state == QStringLiteral("active") && record.isClosed()){
            continue;
        }
        if (state == QStringLiteral("closed") && !record.isClosed()){
            continue;
        }
        filtered.push_back(&record);
    }
    // 最近入场在前。
    std::sort(filtered.begin(), filtered.end(),
              [](const ParkingRecord *a, const ParkingRecord *b){
                  return a->entryTime() > b->entryTime();
              });
    QJsonArray items;
    const int begin = (page - 1) * pageSize;
    const int end = static_cast<int>(
        std::min<size_t>(filtered.size(), static_cast<size_t>(begin + pageSize)));
    for (int index = begin; index < end; ++index){
        items.append(recordPayload(*filtered[static_cast<size_t>(index)]));
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("total"), static_cast<qint64>(filtered.size()));
    payload.insert(QStringLiteral("page"), page);
    payload.insert(QStringLiteral("pageSize"), pageSize);
    payload.insert(QStringLiteral("records"), items);
    return QHttpServerResponse(payload);
}

QHttpServerResponse RestGateway::handleActiveRecord(
    const QString &plate, const QHttpServerRequest &request){
    if (!authenticate(request).has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    const auto record = service_->activeRecord(plate.toStdString());
    if (!record.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::NotFound,
                         "NOT_FOUND", QStringLiteral("该车牌当前不在场内"));
    }
    QJsonObject payload = recordPayload(*record);
    payload.insert(QStringLiteral("durationMin"),
                   static_cast<double>(
                       std::chrono::duration_cast<std::chrono::minutes>(
                           record->duration()).count()));
    payload.insert(QStringLiteral("estimateFee"),
                   service_->billing().calculateFee(record->duration()));
    return QHttpServerResponse(payload);
}

// ---- 路由 handler：入离场 ----

QHttpServerResponse RestGateway::handleEnter(const QHttpServerRequest &request){
    if (!authenticate(request).has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    const auto body = bodyJson(request);
    if (!body.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("请求体必须是 JSON 对象"));
    }
    bool ok = false;
    QString error;
    QJsonObject payload = performEnter(
        body->value(QStringLiteral("plate")).toString().trimmed(),
        body->value(QStringLiteral("vehicleType")).toString(QStringLiteral("car")),
        &ok, &error);
    if (!ok){
        return jsonError(QHttpServerResponder::StatusCode::Conflict,
                         "CONFLICT", error);
    }
    return QHttpServerResponse(payload);
}

QHttpServerResponse RestGateway::handleLeave(const QHttpServerRequest &request){
    if (!authenticate(request).has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    const auto body = bodyJson(request);
    if (!body.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("请求体必须是 JSON 对象"));
    }
    bool ok = false;
    QString error;
    QJsonObject payload = performLeave(
        body->value(QStringLiteral("plate")).toString().trimmed(),
        QString(), &ok, &error);
    if (!ok){
        return jsonError(QHttpServerResponder::StatusCode::Conflict,
                         "CONFLICT", error);
    }
    return QHttpServerResponse(payload);
}

// ---- 路由 handler：预约 ----

QHttpServerResponse RestGateway::handleReservationCreate(
    const QHttpServerRequest &request){
    const auto auth = authenticate(request);
    if (!auth.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    const auto body = bodyJson(request);
    if (!body.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("请求体必须是 JSON 对象"));
    }
    const QString plate =
        body->value(QStringLiteral("plate")).toString().trimmed();
    const auto type = protocol::vehicleTypeFromString(
        body->value(QStringLiteral("vehicleType")).toString(QStringLiteral("car")));
    const qint64 startMs = body->value(QStringLiteral("startMs")).toInteger();
    const int durationMin = body->value(QStringLiteral("durationMin")).toInt(120);
    const bool accessible = body->value(QStringLiteral("accessible")).toBool();
    if (plate.isEmpty() || !type.has_value() || startMs <= 0){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("车牌、车辆类型或时间无效"));
    }
    const auto start = msToTime(startMs);
    const auto created = service_->reservations().create(
        {plate.toStdString(), *type}, start,
        start + std::chrono::minutes(durationMin), Clock::now(), accessible);
    if (!created.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Conflict,
                         "CONFLICT",
                         QString::fromStdString(
                             service_->reservations().lastError()));
    }
    // 定金即时收取（核心层 FakePaymentGateway 语义）：网关落一张已支付
    // 的 deposit 订单作为支付凭证，退款由取消/爽约流程自动生成。
    PaymentOrder depositOrder;
    depositOrder.orderId = newOrderId();
    depositOrder.outTradeNo = QStringLiteral("SP%1%2")
                                  .arg(QDateTime::currentDateTime()
                                           .toString(QStringLiteral("yyyyMMdd")),
                                       depositOrder.orderId.mid(3));
    depositOrder.kind = QStringLiteral("deposit");
    depositOrder.username = auth->username;
    depositOrder.plate = plate;
    depositOrder.reservationId =
        QString::fromStdString(created->reservation.id());
    depositOrder.amount = created->reservation.deposit();
    depositOrder.status = QStringLiteral("paid");
    depositOrder.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    depositOrder.paidAtMs = depositOrder.createdAtMs;
    insertOrder(depositOrder);

    if (hub_ != nullptr){
        hub_->publish(QStringLiteral("reservation.created"),
                      QJsonObject{{QStringLiteral("plate"), plate},
                                  {QStringLiteral("spotId"),
                                   QString::fromStdString(
                                       created->reservation.spotId())},
                                  {QStringLiteral("accessible"), accessible}});
    }
    QJsonObject payload = reservationPayload(created->reservation);
    payload.insert(QStringLiteral("entryRoute"),
                   routePayload(created->reservation.expectedRoute().entryRoute));
    payload.insert(QStringLiteral("exitRoute"),
                   routePayload(created->reservation.expectedRoute().exitRoute));
    payload.insert(QStringLiteral("entranceIndex"),
                   static_cast<int>(
                       created->reservation.expectedRoute().entranceIndex));
    payload.insert(QStringLiteral("exitIndex"),
                   static_cast<int>(
                       created->reservation.expectedRoute().exitIndex));
    payload.insert(QStringLiteral("order"), orderPayload(depositOrder));
    return QHttpServerResponse(payload,
                               QHttpServerResponder::StatusCode::Created);
}

QHttpServerResponse RestGateway::handleReservationList(
    const QHttpServerRequest &request){
    const auto auth = authenticate(request);
    if (!auth.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    const QUrlQuery query(request.url().query());
    const QString plate = query.queryItemValue(QStringLiteral("plate"));
    const QString state = query.queryItemValue(QStringLiteral("state"));
    int page = query.queryItemValue(QStringLiteral("page")).toInt();
    int pageSize = query.queryItemValue(QStringLiteral("pageSize")).toInt();
    if (page < 1){
        page = 1;
    }
    if (pageSize < 1 || pageSize > 100){
        pageSize = 20;
    }
    // 用户角色必须指定车牌，避免看到他人预约；admin 可查全部。
    if (auth->role != QStringLiteral("admin") && plate.isEmpty()){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("请指定车牌号查询预约"));
    }
    std::vector<const Reservation *> filtered;
    for (const Reservation &reservation : service_->reservations().reservations()){
        if (!plate.isEmpty()
            && reservation.plateNumber() != plate.toStdString()){
            continue;
        }
        if (state == QStringLiteral("open") && !reservation.isOpen()){
            continue;
        }
        filtered.push_back(&reservation);
    }
    std::sort(filtered.begin(), filtered.end(),
              [](const Reservation *a, const Reservation *b){
                  return a->startTime() > b->startTime();
              });
    QJsonArray items;
    const int begin = (page - 1) * pageSize;
    const int end = static_cast<int>(
        std::min<size_t>(filtered.size(), static_cast<size_t>(begin + pageSize)));
    for (int index = begin; index < end; ++index){
        items.append(reservationPayload(*filtered[static_cast<size_t>(index)]));
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("total"), static_cast<qint64>(filtered.size()));
    payload.insert(QStringLiteral("page"), page);
    payload.insert(QStringLiteral("pageSize"), pageSize);
    payload.insert(QStringLiteral("reservations"), items);
    return QHttpServerResponse(payload);
}

const Reservation *findReservationById(ParkingService &service,
                                       const QString &reservationId){
    for (const Reservation &reservation : service.reservations().reservations()){
        if (reservation.id() == reservationId.toStdString()){
            return &reservation;
        }
    }
    return nullptr;
}

QHttpServerResponse RestGateway::handleReservationCheckIn(
    const QString &reservationId, const QHttpServerRequest &request){
    if (!authenticate(request).has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    const auto *reservation =
        findReservationById(*service_, reservationId);
    if (reservation == nullptr){
        return jsonError(QHttpServerResponder::StatusCode::NotFound,
                         "NOT_FOUND", QStringLiteral("预约不存在"));
    }
    const QString plate = QString::fromStdString(reservation->plateNumber());
    const auto arrived = service_->reservations().checkIn(
        reservation->plateNumber(), Clock::now());
    if (!arrived.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Conflict,
                         "CONFLICT",
                         QString::fromStdString(
                             service_->reservations().lastError()));
    }
    if (hub_ != nullptr){
        hub_->publish(QStringLiteral("reservation.checkin"),
                      QJsonObject{{QStringLiteral("plate"), plate},
                                  {QStringLiteral("spotId"),
                                   QString::fromStdString(arrived->spotId)}});
    }
    return QHttpServerResponse(
        QJsonObject{{QStringLiteral("spotId"),
                     QString::fromStdString(arrived->spotId)}});
}

QHttpServerResponse RestGateway::handleReservationCancel(
    const QString &reservationId, const QHttpServerRequest &request){
    if (!authenticate(request).has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    const auto *reservation = findReservationById(*service_, reservationId);
    if (reservation == nullptr){
        return jsonError(QHttpServerResponder::StatusCode::NotFound,
                         "NOT_FOUND", QStringLiteral("预约不存在"));
    }
    const QString plate = QString::fromStdString(reservation->plateNumber());
    const double deposit = reservation->deposit();
    if (!service_->reservations().cancel(reservation->plateNumber(),
                                         Clock::now())){
        return jsonError(QHttpServerResponder::StatusCode::Conflict,
                         "CONFLICT",
                         QString::fromStdString(
                             service_->reservations().lastError()));
    }
    // 取消退定金：网关落一张 refunded 流水，与核心 DepositPayment::Refund 对应。
    PaymentOrder refundOrder;
    refundOrder.orderId = newOrderId();
    refundOrder.outTradeNo = QStringLiteral("SP%1%2")
                                 .arg(QDateTime::currentDateTime()
                                          .toString(QStringLiteral("yyyyMMdd")),
                                      refundOrder.orderId.mid(3));
    refundOrder.kind = QStringLiteral("deposit");
    refundOrder.plate = plate;
    refundOrder.reservationId = reservationId;
    refundOrder.amount = deposit;
    refundOrder.status = QStringLiteral("refunded");
    refundOrder.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    refundOrder.refundAtMs = refundOrder.createdAtMs;
    refundOrder.username = QStringLiteral("system");
    insertOrder(refundOrder);
    if (hub_ != nullptr){
        hub_->publish(QStringLiteral("reservation.cancelled"),
                      QJsonObject{{QStringLiteral("plate"), plate}});
    }
    return QHttpServerResponse(QJsonObject{});
}

// ---- 路由 handler：支付订单 ----

QHttpServerResponse RestGateway::handleOrderCreate(
    const QHttpServerRequest &request){
    const auto auth = authenticate(request);
    if (!auth.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    const auto body = bodyJson(request);
    if (!body.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("请求体必须是 JSON 对象"));
    }
    const QString kind = body->value(QStringLiteral("kind")).toString();
    const QString plate =
        body->value(QStringLiteral("plate")).toString().trimmed();
    if (kind != QStringLiteral("parking_fee")){
        // deposit 订单由预约创建自动生成，不开放手工下单。
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION",
                         QStringLiteral("kind 仅支持 parking_fee（定金随预约自动生成）"));
    }
    const auto record = service_->activeRecord(plate.toStdString());
    if (!record.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::NotFound,
                         "NOT_FOUND", QStringLiteral("该车牌当前不在场内"));
    }
    PaymentOrder order;
    order.orderId = newOrderId();
    order.outTradeNo = QStringLiteral("SP%1%2")
                           .arg(QDateTime::currentDateTime()
                                    .toString(QStringLiteral("yyyyMMdd")),
                                order.orderId.mid(3));
    order.kind = kind;
    order.username = auth->username;
    order.plate = plate;
    order.amount = service_->billing().calculateFee(record->duration());
    order.status = QStringLiteral("pending");
    order.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    order.expireAtMs = order.createdAtMs + options_.orderTimeoutMs;
    if (!insertOrder(order)){
        return jsonError(QHttpServerResponder::StatusCode::InternalServerError,
                         "INTERNAL", QStringLiteral("订单写入失败"));
    }
    QJsonObject payload = orderPayload(order);
    payload.insert(QStringLiteral("durationMin"),
                   static_cast<double>(
                       std::chrono::duration_cast<std::chrono::minutes>(
                           record->duration()).count()));
    return QHttpServerResponse(payload,
                               QHttpServerResponder::StatusCode::Created);
}

QHttpServerResponse RestGateway::handleOrderGet(
    const QString &orderId, const QHttpServerRequest &request){
    const auto auth = authenticate(request);
    if (!auth.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    const auto it = orders_.constFind(orderId);
    if (it == orders_.constEnd()
        || (it->username != auth->username
            && auth->role != QStringLiteral("admin"))){
        // 不区分不存在与无权访问，避免泄露他人订单存在性。
        return jsonError(QHttpServerResponder::StatusCode::NotFound,
                         "NOT_FOUND", QStringLiteral("订单不存在"));
    }
    return QHttpServerResponse(orderPayload(it.value()));
}

QHttpServerResponse RestGateway::handleOrderConfirm(
    const QString &orderId, const QHttpServerRequest &request){
    const auto auth = authenticate(request);
    if (!auth.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    auto it = orders_.find(orderId);
    if (it == orders_.end()
        || (it->username != auth->username
            && auth->role != QStringLiteral("admin"))){
        return jsonError(QHttpServerResponder::StatusCode::NotFound,
                         "NOT_FOUND", QStringLiteral("订单不存在"));
    }
    PaymentOrder &order = it.value();
    if (order.status == QStringLiteral("paid")){
        // 幂等：重复确认返回当前状态，不产生二次扣款。
        return QHttpServerResponse(orderPayload(order));
    }
    if (order.status != QStringLiteral("pending")){
        return jsonError(QHttpServerResponder::StatusCode::Conflict,
                         "PAYMENT_STATE",
                         QStringLiteral("订单状态为 %1，不能支付").arg(order.status));
    }
    if (order.kind == QStringLiteral("deposit")){
        return jsonError(QHttpServerResponder::StatusCode::Conflict,
                         "PAYMENT_STATE",
                         QStringLiteral("定金订单随预约自动结算，无需确认"));
    }
    bool ok = false;
    QString error;
    QJsonObject leaveResult = performLeave(order.plate, order.orderId,
                                           &ok, &error);
    if (!ok){
        order.status = QStringLiteral("cancelled");
        updateOrderStatus(order.orderId, order.status, 0, 0);
        return jsonError(QHttpServerResponder::StatusCode::Conflict,
                         "PAYMENT_STATE", error);
    }
    order.status = QStringLiteral("paid");
    order.paidAtMs = QDateTime::currentMSecsSinceEpoch();
    updateOrderStatus(order.orderId, order.status, order.paidAtMs, 0);
    // 已人工/收银台支付的离场，10 分钟内不再触发无感自动扣费。
    paidLeaveGuard_.insert(order.plate, order.paidAtMs);
    if (hub_ != nullptr){
        hub_->publish(QStringLiteral("payment.paid"),
                      QJsonObject{{QStringLiteral("orderId"), order.orderId},
                                  {QStringLiteral("kind"), order.kind},
                                  {QStringLiteral("plate"), order.plate},
                                  {QStringLiteral("amount"), order.amount}});
    }
    leaveResult.insert(QStringLiteral("paidByOrder"), true);
    QJsonObject payload;
    payload.insert(QStringLiteral("order"), orderPayload(order));
    payload.insert(QStringLiteral("leave"), leaveResult);
    return QHttpServerResponse(payload);
}

// ---- 路由 handler：指引 ----

QHttpServerResponse RestGateway::handleGuide(
    const QString &plate, const QHttpServerRequest &request){
    if (!authenticate(request).has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    // 在场车辆：反向寻车，步行栅格路线（可穿越车位区）。
    const auto finder = service_->findCar(plate.toStdString());
    if (finder.has_value()){
        QJsonObject payload;
        payload.insert(QStringLiteral("plate"), plate);
        payload.insert(QStringLiteral("source"), QStringLiteral("active"));
        payload.insert(QStringLiteral("spotId"),
                       QString::fromStdString(finder->spotId));
        payload.insert(QStringLiteral("zone"), QString::fromStdString(finder->zone));
        payload.insert(QStringLiteral("anchorIndex"),
                       static_cast<int>(finder->anchorIndex));
        payload.insert(QStringLiteral("anchor"),
                       QJsonObject{{QStringLiteral("x"), finder->anchor.x},
                                   {QStringLiteral("y"), finder->anchor.y}});
        payload.insert(QStringLiteral("route"), routePayload(finder->walkRoute));
        return QHttpServerResponse(payload);
    }
    // 无在场记录但有有效预约：返回预约时下发的预期路线快照。
    const auto reservation = service_->reservations().findOpen(
        plate.toStdString());
    if (reservation.has_value()){
        QJsonObject payload = reservationPayload(*reservation);
        payload.insert(QStringLiteral("source"), QStringLiteral("reservation"));
        payload.insert(QStringLiteral("entryRoute"),
                       routePayload(reservation->expectedRoute().entryRoute));
        payload.insert(QStringLiteral("exitRoute"),
                       routePayload(reservation->expectedRoute().exitRoute));
        return QHttpServerResponse(payload);
    }
    return jsonError(QHttpServerResponder::StatusCode::NotFound,
                     "NOT_FOUND",
                     QStringLiteral("该车牌无在场记录或有效预约"));
}

// ---- 业务动作 ----

QJsonObject RestGateway::performEnter(const QString &plate,
                                      const QString &vehicleType,
                                      bool *ok, QString *error){
    const auto type = protocol::vehicleTypeFromString(vehicleType);
    if (plate.isEmpty() || !type.has_value()){
        *error = QStringLiteral("车牌或车辆类型无效");
        return {};
    }
    const auto result = service_->enter({plate.toStdString(), *type});
    if (!result.has_value()){
        *error = QStringLiteral("入场失败：车辆已在场内或无可用车位");
        return {};
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("plateNumber"),
                   QString::fromStdString(result->plateNumber));
    payload.insert(QStringLiteral("spotId"),
                   QString::fromStdString(result->spotId));
    payload.insert(QStringLiteral("entryDistance"), result->entryRoute.distance);
    payload.insert(QStringLiteral("exitDistance"), result->exitRoute.distance);
    payload.insert(QStringLiteral("entryTurns"), result->entryRoute.turnCount);
    payload.insert(QStringLiteral("exitTurns"), result->exitRoute.turnCount);
    payload.insert(QStringLiteral("score"), result->score);
    if (hub_ != nullptr){
        hub_->publish(QStringLiteral("parking.entered"),
                      QJsonObject{{QStringLiteral("plate"), plate},
                                  {QStringLiteral("spotId"),
                                   QString::fromStdString(result->spotId)},
                                  {QStringLiteral("entryTime"),
                                   QDateTime::currentMSecsSinceEpoch()}});
    }
    *ok = true;
    return payload;
}

QJsonObject RestGateway::performLeave(const QString &plate,
                                      const QString &excludeOrderId,
                                      bool *ok, QString *error){
    const auto closed = service_->leave(plate.toStdString());
    if (!closed.has_value()){
        *error = QStringLiteral("离场失败：车辆不在场内");
        return {};
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("plate"), plate);
    payload.insert(QStringLiteral("spotId"),
                   QString::fromStdString(closed->spotId()));
    payload.insert(QStringLiteral("fee"), closed->fee());
    payload.insert(QStringLiteral("durationMin"),
                   static_cast<double>(
                       std::chrono::duration_cast<std::chrono::minutes>(
                           closed->duration()).count()));
    // 该车牌未支付的停车费订单随出口结算关闭，避免悬空订单。
    for (auto it = orders_.begin(); it != orders_.end(); ++it){
        if (it.value().status == QStringLiteral("pending")
            && it.value().kind == QStringLiteral("parking_fee")
            && it.value().plate == plate
            && it.key() != excludeOrderId){
            it.value().status = QStringLiteral("cancelled");
            updateOrderStatus(it.key(), it.value().status, 0, 0);
        }
    }
    if (hub_ != nullptr){
        hub_->publish(QStringLiteral("parking.exited"),
                      QJsonObject{{QStringLiteral("plate"), plate},
                                  {QStringLiteral("spotId"),
                                   QString::fromStdString(closed->spotId())},
                                  {QStringLiteral("fee"), closed->fee()}});
    }
    *ok = true;
    return payload;
}

QJsonObject RestGateway::statusPayload() const{
    QJsonObject payload;
    payload.insert(QStringLiteral("capacity"),
                   static_cast<int>(service_->spots().size()));
    payload.insert(QStringLiteral("occupied"), service_->occupiedSpots());
    payload.insert(QStringLiteral("available"), service_->remainingSpots());
    payload.insert(QStringLiteral("reserved"), service_->reservedSpots());
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
        zones.append(QJsonObject{
            {QStringLiteral("zone"), QString::fromStdString(entry.first)},
            {QStringLiteral("total"), entry.second.first},
            {QStringLiteral("occupied"), entry.second.second}});
    }
    payload.insert(QStringLiteral("zones"), zones);
    return payload;
}

QJsonObject RestGateway::spotListPayload(bool includePlates) const{
    QJsonArray spots;
    for (const ParkingSpot &spot : service_->spots()){
        QJsonObject item;
        item.insert(QStringLiteral("spotId"),
                    QString::fromStdString(spot.identifier()));
        item.insert(QStringLiteral("zone"), QString::fromStdString(spot.zone()));
        item.insert(QStringLiteral("type"), QLatin1String(toString(spot.type())));
        item.insert(QStringLiteral("status"), static_cast<int>(spot.status()));
        if (includePlates && spot.parkedVehicle()){
            item.insert(QStringLiteral("plate"),
                        QString::fromStdString(
                            spot.parkedVehicle()->plateNumber()));
        }
        spots.append(item);
    }
    return QJsonObject{{QStringLiteral("spots"), spots}};
}

QJsonObject RestGateway::layoutPayload() const{
    const ParkingLayout &layout = service_->layout();
    QJsonObject layoutJson;
    layoutJson.insert(QStringLiteral("siteWidth"), layout.siteWidth());
    layoutJson.insert(QStringLiteral("siteHeight"), layout.siteHeight());
    layoutJson.insert(QStringLiteral("plan"),
                      std::abs(layout.siteWidth() - 58.0) < 0.25
                          && std::abs(layout.siteHeight() - 42.4) < 0.25
                          ? QStringLiteral("garage") : QStringLiteral("grid"));
    QJsonArray entrances;
    for (const Point &point : layout.entrances()){
        entrances.append(QJsonObject{{QStringLiteral("x"), point.x},
                                     {QStringLiteral("y"), point.y}});
    }
    layoutJson.insert(QStringLiteral("entrances"), entrances);
    QJsonArray exits;
    for (const Point &point : layout.exits()){
        exits.append(QJsonObject{{QStringLiteral("x"), point.x},
                                 {QStringLiteral("y"), point.y}});
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
    // 面向用户端：车位几何与状态下发，但不包含他人车牌。
    QJsonArray spots;
    for (const ParkingSpot &spot : service_->spots()){
        QJsonObject item;
        item.insert(QStringLiteral("spotId"),
                    QString::fromStdString(spot.identifier()));
        item.insert(QStringLiteral("zone"), QString::fromStdString(spot.zone()));
        item.insert(QStringLiteral("type"), QLatin1String(toString(spot.type())));
        item.insert(QStringLiteral("status"), static_cast<int>(spot.status()));
        const auto &bounds = spot.bounds();
        item.insert(QStringLiteral("x"), bounds.origin.x);
        item.insert(QStringLiteral("y"), bounds.origin.y);
        item.insert(QStringLiteral("w"), bounds.width);
        item.insert(QStringLiteral("h"), bounds.height);
        spots.append(item);
    }
    return QJsonObject{{QStringLiteral("layout"), layoutJson},
                       {QStringLiteral("spots"), spots}};
}

QJsonObject RestGateway::routePayload(const Route &route) const{
    QJsonArray points;
    for (const Point &point : route.points){
        points.append(QJsonObject{{QStringLiteral("x"), point.x},
                                  {QStringLiteral("y"), point.y}});
    }
    return QJsonObject{{QStringLiteral("points"), points},
                       {QStringLiteral("distanceM"), route.distance},
                       {QStringLiteral("turns"), route.turnCount}};
}

QJsonObject RestGateway::recordPayload(const ParkingRecord &record) const{
    QJsonObject payload;
    payload.insert(QStringLiteral("plate"),
                   QString::fromStdString(record.plateNumber()));
    payload.insert(QStringLiteral("spotId"),
                   QString::fromStdString(record.spotId()));
    payload.insert(QStringLiteral("vehicleType"),
                   vehicleTypeText(record.vehicleType()));
    payload.insert(QStringLiteral("entryTimeMs"), timeToMs(record.entryTime()));
    payload.insert(QStringLiteral("status"),
                   record.isClosed() ? QStringLiteral("closed")
                                     : QStringLiteral("active"));
    if (record.exitTime().has_value()){
        payload.insert(QStringLiteral("exitTimeMs"),
                       timeToMs(record.exitTime().value()));
        payload.insert(QStringLiteral("fee"), record.fee());
        payload.insert(QStringLiteral("durationMin"),
                       static_cast<double>(
                           std::chrono::duration_cast<std::chrono::minutes>(
                               record.duration()).count()));
    }
    return payload;
}

QJsonObject RestGateway::reservationPayload(
    const Reservation &reservation) const{
    QJsonObject payload;
    payload.insert(QStringLiteral("reservationId"),
                   QString::fromStdString(reservation.id()));
    payload.insert(QStringLiteral("plate"),
                   QString::fromStdString(reservation.plateNumber()));
    payload.insert(QStringLiteral("vehicleType"),
                   vehicleTypeText(reservation.vehicleType()));
    payload.insert(QStringLiteral("spotId"),
                   QString::fromStdString(reservation.spotId()));
    payload.insert(QStringLiteral("createdAtMs"),
                   timeToMs(reservation.createdAt()));
    payload.insert(QStringLiteral("startMs"), timeToMs(reservation.startTime()));
    payload.insert(QStringLiteral("endMs"), timeToMs(reservation.endTime()));
    payload.insert(QStringLiteral("graceDeadlineMs"),
                   timeToMs(reservation.graceDeadline()));
    payload.insert(QStringLiteral("deposit"), reservation.deposit());
    payload.insert(QStringLiteral("depositState"),
                   depositStateText(reservation.depositState()));
    payload.insert(QStringLiteral("status"),
                   reservationStatusText(reservation.status()));
    payload.insert(QStringLiteral("accessible"), reservation.isAccessible());
    return payload;
}

QJsonObject RestGateway::orderPayload(const PaymentOrder &order) const{
    QJsonObject payload;
    payload.insert(QStringLiteral("orderId"), order.orderId);
    payload.insert(QStringLiteral("outTradeNo"), order.outTradeNo);
    payload.insert(QStringLiteral("kind"), order.kind);
    payload.insert(QStringLiteral("plate"), order.plate);
    payload.insert(QStringLiteral("amount"), order.amount);
    payload.insert(QStringLiteral("status"), order.status);
    payload.insert(QStringLiteral("createdAtMs"), order.createdAtMs);
    payload.insert(QStringLiteral("expireAtMs"), order.expireAtMs);
    if (order.paidAtMs > 0){
        payload.insert(QStringLiteral("paidAtMs"), order.paidAtMs);
    }
    if (order.refundAtMs > 0){
        payload.insert(QStringLiteral("refundAtMs"), order.refundAtMs);
    }
    if (!order.reservationId.isEmpty()){
        payload.insert(QStringLiteral("reservationId"), order.reservationId);
    }
    return payload;
}

// ---- 支付订单仓储 ----

bool RestGateway::insertOrder(const PaymentOrder &order){
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO payment_orders(order_id, out_trade_no, kind, username,"
        " plate, reservation_id, amount, status, created_at_ms, expire_at_ms,"
        " paid_at_ms, refund_at_ms) VALUES(:orderId, :outTradeNo, :kind,"
        " :username, :plate, :reservationId, :amount, :status, :createdAt,"
        " :expireAt, :paidAt, :refundAt)"));
    query.bindValue(QStringLiteral(":orderId"), order.orderId);
    query.bindValue(QStringLiteral(":outTradeNo"), order.outTradeNo);
    query.bindValue(QStringLiteral(":kind"), order.kind);
    query.bindValue(QStringLiteral(":username"), order.username);
    query.bindValue(QStringLiteral(":plate"), order.plate);
    query.bindValue(QStringLiteral(":reservationId"), order.reservationId);
    query.bindValue(QStringLiteral(":amount"), order.amount);
    query.bindValue(QStringLiteral(":status"), order.status);
    query.bindValue(QStringLiteral(":createdAt"), order.createdAtMs);
    query.bindValue(QStringLiteral(":expireAt"), order.expireAtMs);
    query.bindValue(QStringLiteral(":paidAt"),
                    order.paidAtMs > 0 ? QVariant(order.paidAtMs) : QVariant());
    query.bindValue(QStringLiteral(":refundAt"),
                    order.refundAtMs > 0 ? QVariant(order.refundAtMs)
                                         : QVariant());
    if (!query.exec()){
        lastError_ = query.lastError().text();
        return false;
    }
    orders_.insert(order.orderId, order);
    return true;
}

bool RestGateway::updateOrderStatus(const QString &orderId,
                                    const QString &status, qint64 paidAtMs,
                                    qint64 refundAtMs){
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE payment_orders SET status = :status, paid_at_ms = :paidAt,"
        " refund_at_ms = :refundAt WHERE order_id = :orderId"));
    query.bindValue(QStringLiteral(":status"), status);
    query.bindValue(QStringLiteral(":paidAt"),
                    paidAtMs > 0 ? QVariant(paidAtMs) : QVariant());
    query.bindValue(QStringLiteral(":refundAt"),
                    refundAtMs > 0 ? QVariant(refundAtMs) : QVariant());
    query.bindValue(QStringLiteral(":orderId"), orderId);
    if (!query.exec()){
        lastError_ = query.lastError().text();
        return false;
    }
    return true;
}

RestGateway::PaymentOrder *RestGateway::findOrder(const QString &orderId){
    const auto it = orders_.find(orderId);
    return it == orders_.end() ? nullptr : &it.value();
}

QString RestGateway::newOrderId(){
    return QStringLiteral("po_") + randomHex(6);
}

// ---- 二维码 / 无感支付 / 拍照识牌 ----

QHttpServerResponse RestGateway::handleQr(
    const QHttpServerRequest &request) const{
    // 免认证：供 H5 的 <img src="/api/v1/qr?text=..."> 直接引用。
    const QUrlQuery query(request.url().query());
    const QString text = query.queryItemValue(QStringLiteral("text"));
    const QByteArray utf8 = text.toUtf8();
    if (utf8.isEmpty() || utf8.size() > 800){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("text 长度需在 1-800 字节"));
    }
    const qrcodegen::QrCode code =
        qrcodegen::QrCode::encodeText(utf8.constData(),
                                      qrcodegen::QrCode::Ecc::MEDIUM);
    const int border = 2;
    const int size = code.getSize() + border * 2;
    QString path;
    for (int y = 0; y < code.getSize(); ++y){
        for (int x = 0; x < code.getSize(); ++x){
            if (code.getModule(x, y)){
                path += QStringLiteral("M%1,%2h1v1h-1z")
                            .arg(x + border).arg(y + border);
            }
        }
    }
    const QString svg = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %1 %1\""
        " shape-rendering=\"crispEdges\" stroke=\"none\">"
        "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>"
        "<path d=\"%2\" fill=\"#0f1b2d\"/></svg>").arg(size).arg(path);
    return QHttpServerResponse(
        QByteArrayLiteral("image/svg+xml"), svg.toUtf8());
}

QHttpServerResponse RestGateway::handleSiteResolve(
    const QHttpServerRequest &request) const{
    const QUrlQuery query(request.url().query());
    const QString token = query.queryItemValue(QStringLiteral("t"));
    if (token.isEmpty()){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("缺少点位票据 t"));
    }
    QSqlQuery statement(database_);
    statement.prepare(QStringLiteral(
        "SELECT site_name, expires_at_ms, revoked FROM site_tickets"
        " WHERE token = :token"));
    statement.bindValue(QStringLiteral(":token"), token);
    if (!statement.exec() || !statement.next()){
        return jsonError(QHttpServerResponder::StatusCode::NotFound,
                         "SITE_NOT_FOUND",
                         QStringLiteral("二维码无效或已被撤销，请扫现场最新的码"));
    }
    const bool revoked = statement.value(2).toInt() != 0;
    const qint64 expiresAt = statement.value(1).toLongLong();
    const bool expired = expiresAt != 0
        && expiresAt <= QDateTime::currentMSecsSinceEpoch();
    if (revoked || expired){
        return jsonError(QHttpServerResponder::StatusCode::Gone,
                         "SITE_EXPIRED",
                         QStringLiteral("二维码已过期，请扫现场最新的码"));
    }
    return QHttpServerResponse(QJsonObject{
        {QStringLiteral("siteName"), statement.value(0).toString()},
        {QStringLiteral("expiresAtMs"), expiresAt}});
}

QHttpServerResponse RestGateway::handlePlateList(
    const QHttpServerRequest &request){
    const auto identity = authenticate(request);
    if (!identity){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("请先登录"));
    }
    QSqlQuery statement(database_);
    statement.prepare(QStringLiteral(
        "SELECT plate, created_at_ms FROM user_plates WHERE username = :user"
        " ORDER BY created_at_ms"));
    statement.bindValue(QStringLiteral(":user"), identity->username);
    QJsonArray plates;
    if (statement.exec()){
        while (statement.next()){
            plates.append(QJsonObject{
                {QStringLiteral("plate"), statement.value(0).toString()},
                {QStringLiteral("createdAtMs"), statement.value(1).toLongLong()}});
        }
    }
    return QHttpServerResponse(QJsonObject{{QStringLiteral("plates"), plates}});
}

QHttpServerResponse RestGateway::handlePlateBind(
    const QHttpServerRequest &request){
    const auto identity = authenticate(request);
    if (!identity){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("请先登录"));
    }
    const auto body = bodyJson(request);
    if (!body.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("请求体必须是 JSON 对象"));
    }
    const QString plate = body->value(QStringLiteral("plate")).toString().trimmed();
    if (plate.isEmpty() || plate.size() > 16){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("车牌号不合法"));
    }
    const QString ticket = body->value(QStringLiteral("ticket")).toString();
    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO user_plates (username, plate, site_token,"
        " created_at_ms) VALUES (:user, :plate, :ticket, :now)"));
    insert.bindValue(QStringLiteral(":user"), identity->username);
    insert.bindValue(QStringLiteral(":plate"), plate);
    insert.bindValue(QStringLiteral(":ticket"), ticket);
    insert.bindValue(QStringLiteral(":now"), QDateTime::currentMSecsSinceEpoch());
    if (!insert.exec()){
        lastError_ = insert.lastError().text();
        return jsonError(QHttpServerResponder::StatusCode::InternalServerError,
                         "STORAGE", QStringLiteral("绑定失败，请重试"));
    }
    return QHttpServerResponse(QJsonObject{
        {QStringLiteral("plate"), plate},
        {QStringLiteral("created"), insert.numRowsAffected() > 0}});
}

void RestGateway::ensureSiteSchema(){
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS site_tickets ("
            "token TEXT PRIMARY KEY,"
            "site_name TEXT NOT NULL,"
            "created_at_ms INTEGER NOT NULL,"
            "expires_at_ms INTEGER NOT NULL,"   // 0 = 不过期
            "revoked INTEGER NOT NULL DEFAULT 0)"))){
        lastError_ = query.lastError().text();
        return;
    }
    // 用户绑定的车牌。与无感支付车牌分开：这里是「这台车属于这个人」，
    // 无感支付是「这台车离场自动扣费」，两者可以不同。
    QSqlQuery plates(database_);
    if (!plates.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS user_plates ("
            "username TEXT NOT NULL,"
            "plate TEXT NOT NULL,"
            "site_token TEXT,"
            "created_at_ms INTEGER NOT NULL,"
            "PRIMARY KEY(username, plate))"))){
        lastError_ = plates.lastError().text();
    }
}

QString RestGateway::ensureDefaultSiteTicket(){
    // 已有可用票据就复用：张贴出去的二维码不能因为重启服务端就失效。
    QSqlQuery existing(database_);
    existing.prepare(QStringLiteral(
        "SELECT token FROM site_tickets WHERE revoked = 0"
        " AND (expires_at_ms = 0 OR expires_at_ms > :now)"
        " ORDER BY created_at_ms LIMIT 1"));
    existing.bindValue(QStringLiteral(":now"), QDateTime::currentMSecsSinceEpoch());
    if (existing.exec() && existing.next()){
        return existing.value(0).toString();
    }
    const QString token = QString::fromLatin1(
        QCryptographicHash::hash(
            QByteArray::number(QRandomGenerator::system()->generate64())
                + QByteArray::number(QRandomGenerator::system()->generate64()),
            QCryptographicHash::Sha256).toHex().left(24));
    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT INTO site_tickets (token, site_name, created_at_ms, expires_at_ms,"
        " revoked) VALUES (:token, :name, :now, 0, 0)"));
    insert.bindValue(QStringLiteral(":token"), token);
    insert.bindValue(QStringLiteral(":name"), options_.siteName);
    insert.bindValue(QStringLiteral(":now"), QDateTime::currentMSecsSinceEpoch());
    if (!insert.exec()){
        lastError_ = insert.lastError().text();
        return QString();
    }
    return token;
}

void RestGateway::ensureFrictionlessSchema(){
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS frictionless_plates ("
            "username TEXT NOT NULL,"
            "plate TEXT NOT NULL,"
            "created_at_ms INTEGER NOT NULL,"
            "PRIMARY KEY(username, plate))"))){
        lastError_ = query.lastError().text();
    }
}

QString RestGateway::frictionlessOwner(const QString &plate) const{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT username FROM frictionless_plates WHERE plate = :plate"
        " ORDER BY created_at_ms LIMIT 1"));
    query.bindValue(QStringLiteral(":plate"), plate);
    if (!query.exec() || !query.next()){
        return QString();
    }
    return query.value(0).toString();
}

void RestGateway::autoChargeOnExit(const QString &plate, double fee){
    // 免费时段 fee=0 也生成 ¥0 流水：让「自动扣费已发生」在演示中可见。
    // 订单归属开通者账号，避免「系统」流水对所有用户可见。
    const QString owner = frictionlessOwner(plate);
    if (owner.isEmpty()){
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto guard = paidLeaveGuard_.constFind(plate);
    if (guard != paidLeaveGuard_.constEnd() && now - guard.value() < 10 * 60 * 1000){
        return;  // 收银台刚完成支付，避免重复扣费
    }
    PaymentOrder order;
    order.orderId = newOrderId();
    order.outTradeNo = QStringLiteral("SP%1%2")
                           .arg(QDateTime::currentDateTime()
                                    .toString(QStringLiteral("yyyyMMdd")),
                                order.orderId.mid(3));
    order.kind = QStringLiteral("parking_fee");
    order.username = owner;
    order.plate = plate;
    order.amount = fee;
    order.status = QStringLiteral("paid");
    order.createdAtMs = now;
    order.paidAtMs = now;
    insertOrder(order);
    if (hub_ != nullptr){
        hub_->publish(QStringLiteral("payment.paid"),
                      QJsonObject{{QStringLiteral("orderId"), order.orderId},
                                  {QStringLiteral("kind"), order.kind},
                                  {QStringLiteral("plate"), plate},
                                  {QStringLiteral("amount"), fee},
                                  {QStringLiteral("frictionless"), true}});
    }
}

QHttpServerResponse RestGateway::handleFrictionlessList(
    const QHttpServerRequest &request){
    const auto auth = authenticate(request);
    if (!auth.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT plate, created_at_ms FROM frictionless_plates"
        " WHERE username = :userName ORDER BY created_at_ms"));
    query.bindValue(QStringLiteral(":userName"), auth->username);
    QJsonArray plates;
    if (query.exec()){
        while (query.next()){
            plates.append(QJsonObject{
                {QStringLiteral("plate"), query.value(0).toString()},
                {QStringLiteral("createdAtMs"), query.value(1).toLongLong()}});
        }
    }
    return QHttpServerResponse(
        QJsonObject{{QStringLiteral("plates"), plates}});
}

QHttpServerResponse RestGateway::handleFrictionlessToggle(
    const QHttpServerRequest &request){
    const auto auth = authenticate(request);
    if (!auth.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    const auto body = bodyJson(request);
    if (!body.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("请求体必须是 JSON 对象"));
    }
    const QString plate =
        body->value(QStringLiteral("plate")).toString().trimmed();
    const bool enabled = body->value(QStringLiteral("enabled")).toBool();
    if (plate.isEmpty()){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("车牌不能为空"));
    }
    QSqlQuery query(database_);
    if (enabled){
        query.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO frictionless_plates(username, plate,"
            " created_at_ms) VALUES(:userName, :plate, :createdAt)"));
        query.bindValue(QStringLiteral(":userName"), auth->username);
        query.bindValue(QStringLiteral(":plate"), plate);
        query.bindValue(QStringLiteral(":createdAt"),
                        QDateTime::currentMSecsSinceEpoch());
    } else{
        query.prepare(QStringLiteral(
            "DELETE FROM frictionless_plates"
            " WHERE username = :userName AND plate = :plate"));
        query.bindValue(QStringLiteral(":userName"), auth->username);
        query.bindValue(QStringLiteral(":plate"), plate);
    }
    if (!query.exec()){
        return jsonError(QHttpServerResponder::StatusCode::InternalServerError,
                         "INTERNAL", query.lastError().text());
    }
    return QHttpServerResponse(
        QJsonObject{{QStringLiteral("plate"), plate},
                    {QStringLiteral("enabled"), enabled}});
}

QJsonObject RestGateway::recognizePlate(const QByteArray &imageBytes,
                                        QString *note){
    // 实现移到 network/PlateRecognition，TCP 服务端的 lpr.recognize 动作共用同一套，
    // 避免「跑在服务器上」这件事有两个版本。
    return network::recognizePlate(imageBytes, options_.lprCommand, note);
}

QHttpServerResponse RestGateway::handleLprRecognize(
    const QHttpServerRequest &request){
    const auto auth = authenticate(request);
    if (!auth.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    const auto body = bodyJson(request);
    if (!body.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION", QStringLiteral("请求体必须是 JSON 对象"));
    }
    const QString imageBase64 =
        body->value(QStringLiteral("image")).toString();
    QByteArray imageBytes = QByteArray::fromBase64(imageBase64.toLatin1());
    if (imageBytes.isEmpty() || imageBytes.size() > 6 * 1024 * 1024){
        return jsonError(QHttpServerResponder::StatusCode::BadRequest,
                         "VALIDATION",
                         QStringLiteral("image 需为 base64 图片且不超过 6MB"));
    }
    QString note;
    const QJsonObject result = recognizePlate(imageBytes, &note);
    if (result.isEmpty()){
        return jsonError(QHttpServerResponder::StatusCode::InternalServerError,
                         "INTERNAL",
                         QStringLiteral("识别失败：%1").arg(note));
    }
    QJsonObject payload = result;
    payload.insert(QStringLiteral("backend"), note);
    if (audit_ != nullptr){
        audit_->record(auth->username.toStdString(), "lpr_recognize");
    }
    return QHttpServerResponse(payload);
}

QHttpServerResponse RestGateway::handleOrderList(
    const QHttpServerRequest &request){
    const auto auth = authenticate(request);
    if (!auth.has_value()){
        return jsonError(QHttpServerResponder::StatusCode::Unauthorized,
                         "AUTH_REQUIRED", QStringLiteral("缺少或无效的 Bearer token"));
    }
    const QUrlQuery query(request.url().query());
    const QString plate = query.queryItemValue(QStringLiteral("plate"));
    const QString status = query.queryItemValue(QStringLiteral("status"));
    int page = query.queryItemValue(QStringLiteral("page")).toInt();
    int pageSize = query.queryItemValue(QStringLiteral("pageSize")).toInt();
    if (page < 1){
        page = 1;
    }
    if (pageSize < 1 || pageSize > 100){
        pageSize = 20;
    }
    std::vector<const PaymentOrder *> filtered;
    for (const PaymentOrder &order : orders_){
        if (auth->role != QStringLiteral("admin")
            && order.username != auth->username){
            continue;
        }
        if (!plate.isEmpty() && order.plate != plate){
            continue;
        }
        if (!status.isEmpty() && order.status != status){
            continue;
        }
        filtered.push_back(&order);
    }
    std::sort(filtered.begin(), filtered.end(),
              [](const PaymentOrder *a, const PaymentOrder *b){
                  return a->createdAtMs > b->createdAtMs;
              });
    QJsonArray items;
    const int begin = (page - 1) * pageSize;
    const int end = static_cast<int>(
        std::min<size_t>(filtered.size(), static_cast<size_t>(begin + pageSize)));
    for (int index = begin; index < end; ++index){
        items.append(orderPayload(*filtered[static_cast<size_t>(index)]));
    }
    return QHttpServerResponse(
        QJsonObject{{QStringLiteral("total"),
                     static_cast<qint64>(filtered.size())},
                    {QStringLiteral("page"), page},
                    {QStringLiteral("pageSize"), pageSize},
                    {QStringLiteral("orders"), items}});
}

// ---- WebSocket 推送与扫描 ----

void RestGateway::broadcastWs(const QString &name, const QJsonObject &payload){
    if (ws_ == nullptr || wsSessions_.isEmpty()){
        return;
    }
    const QByteArray frame = QJsonDocument(
        protocol::makeEvent(name, payload)).toJson(QJsonDocument::Compact);
    for (auto it = wsSessions_.begin(); it != wsSessions_.end(); ++it){
        if (it.value().authenticated){
            it.key()->sendTextMessage(QString::fromUtf8(frame));
        }
    }
}

void RestGateway::sweep(){
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // 订单超时关单。
    QList<QString> expired;
    for (auto it = orders_.begin(); it != orders_.end(); ++it){
        if (it.value().status == QStringLiteral("pending")
            && it.value().expireAtMs < now){
            expired.append(it.key());
        }
    }
    for (const QString &orderId : expired){
        auto it = orders_.find(orderId);
        it.value().status = QStringLiteral("expired");
        updateOrderStatus(orderId, it.value().status, 0, 0);
        if (hub_ != nullptr){
            hub_->publish(QStringLiteral("payment.expired"),
                          QJsonObject{{QStringLiteral("orderId"), orderId},
                                      {QStringLiteral("kind"), it.value().kind},
                                      {QStringLiteral("plate"), it.value().plate}});
        }
    }
    // WS 空闲踢除：60 秒无任何帧。
    if (ws_ != nullptr){
        QList<QWebSocket *> stale;
        for (auto it = wsSessions_.begin(); it != wsSessions_.end(); ++it){
            if (now - it.value().lastSeenMs > 60000){
                stale.append(it.key());
            }
        }
        for (QWebSocket *socket : stale){
            socket->close(QWebSocketProtocol::CloseCodeGoingAway,
                          QStringLiteral("heartbeat timeout"));
        }
    }
}

} // namespace smartpark
