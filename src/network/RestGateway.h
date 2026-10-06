#pragma once
#include "core/model/ParkingRecord.h"
#include "core/model/Reservation.h"
#include "core/service/ParkingService.h"

#include <QtHttpServer/QHttpServer>
#include <QtHttpServer/QHttpServerRequest>
#include <QtHttpServer/QHttpServerResponder>
#include <QtHttpServer/QHttpServerResponse>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QTimer>

#include <memory>
#include <optional>

class QTcpServer;
class QWebSocket;
class QWebSocketServer;

namespace smartpark{

class AuditLogService;
class EventHub;
class UserStore;

// REST/WebSocket 网关（协议见 docs/rest-api.md）。与 SmartParkTcpServer
// 平行运行，共享同一 ParkingService / UserStore / AuditLogService，
// 延续单线程事件驱动模型，不引入锁库。
// 职责：/api/v1 路由与 Bearer 鉴权、支付订单状态机、H5 静态页伺服、
// WebSocket 事件推送（订阅 EventHub）、支付订单超时扫描。
class RestGateway : public QObject{
    Q_OBJECT

public:
    struct Options{
        quint16 httpPort{8080};
        quint16 wsPort{8081};   // 0 = 禁用 WS 推送
        QString webRoot;        // H5 构建产物目录；空 = 不伺服静态页
        qint64 orderTimeoutMs{15 * 60 * 1000};
        qint64 tokenTtlMs{2 * 60 * 60 * 1000};
        // 拍照识牌（docs/rest-api.md §4 预留端点的落地）：识别命令模板，
        // %1 替换为临时图片路径，命令向 stdout 输出 {"plate":...,"confidence":...}；
        // 空 = 内置 mock 识别（按图片哈希生成稳定演示车牌）。
        QString lprCommand;
        // 点位名称：扫码后 H5 会显示「你正在 XXX 设置账户」。
        QString siteName{QStringLiteral("SmartPark 停车场")};
    };

    // 启动时选定的默认点位票据 token（含在终端打印的二维码里）。
    const QString &siteToken() const noexcept { return siteToken_; }

    RestGateway(ParkingService &service, UserStore &users,
                AuditLogService *audit, const QSqlDatabase &database,
                EventHub *hub, Options options, QObject *parent = nullptr);
    ~RestGateway() override;

    bool listen();
    quint16 httpPort() const noexcept;
    quint16 wsPort() const noexcept;
    const QString &lastError() const noexcept;

private:
    struct AuthContext{
        QString username;
        QString role;
    };
    // 支付订单状态机：pending -> paid | expired | refunded | cancelled。
    // deposit 订单由预约创建时自动生成（支付凭证），parking_fee 走完整
    // 下单-支付-核销流程；与核心层 deposit_payments 流水以 reservationId 关联。
    struct PaymentOrder{
        QString orderId;
        QString outTradeNo;
        QString kind;          // deposit | parking_fee
        QString username;
        QString plate;
        QString reservationId;
        double amount{0.0};
        QString status;        // pending | paid | expired | refunded | cancelled
        qint64 createdAtMs{0};
        qint64 expireAtMs{0};
        qint64 paidAtMs{0};
        qint64 refundAtMs{0};
    };
    struct WsSession{
        bool authenticated{false};
        QString username;
        QString role;
        qint64 lastSeenMs{0};
    };

    void setupHttp();
    bool setupWebSocket();
    void ensureOrderSchema();

    // ---- 路由 handler ----
    QHttpServerResponse handleMeta(const QHttpServerRequest &request) const;
    QHttpServerResponse handleRegister(const QHttpServerRequest &request);
    QHttpServerResponse handleLogin(const QHttpServerRequest &request);
    QHttpServerResponse handleRefresh(const QHttpServerRequest &request);
    QHttpServerResponse handleLogout(const QHttpServerRequest &request);
    QHttpServerResponse handleMe(const QHttpServerRequest &request);
    QHttpServerResponse handleStatus(const QHttpServerRequest &request);
    QHttpServerResponse handleSpots(const QHttpServerRequest &request);
    QHttpServerResponse handleLayout(const QHttpServerRequest &request);
    QHttpServerResponse handleRecords(const QHttpServerRequest &request);
    QHttpServerResponse handleActiveRecord(const QString &plate,
                                           const QHttpServerRequest &request);
    QHttpServerResponse handleEnter(const QHttpServerRequest &request);
    QHttpServerResponse handleLeave(const QHttpServerRequest &request);
    QHttpServerResponse handleReservationCreate(const QHttpServerRequest &request);
    QHttpServerResponse handleReservationList(const QHttpServerRequest &request);
    QHttpServerResponse handleReservationCheckIn(const QString &reservationId,
                                                 const QHttpServerRequest &request);
    QHttpServerResponse handleReservationCancel(const QString &reservationId,
                                                const QHttpServerRequest &request);
    QHttpServerResponse handleOrderCreate(const QHttpServerRequest &request);
    QHttpServerResponse handleOrderList(const QHttpServerRequest &request);
    QHttpServerResponse handleOrderGet(const QString &orderId,
                                       const QHttpServerRequest &request);
    QHttpServerResponse handleOrderConfirm(const QString &orderId,
                                           const QHttpServerRequest &request);
    QHttpServerResponse handleGuide(const QString &plate,
                                    const QHttpServerRequest &request);
    // 二维码（免认证，供 <img> 直接引用）：GET /api/v1/qr?text=...
    QHttpServerResponse handleQr(const QHttpServerRequest &request) const;
    // 点位票据解析（免认证）：扫码后、登录前就要能确认「扫的是哪个车场」。
    QHttpServerResponse handleSiteResolve(const QHttpServerRequest &request) const;
    QHttpServerResponse handlePlateList(const QHttpServerRequest &request);
    QHttpServerResponse handlePlateBind(const QHttpServerRequest &request);
    // 无感支付（先离场后付 mock）：开通/关闭车牌与列表。
    QHttpServerResponse handleFrictionlessList(const QHttpServerRequest &request);
    QHttpServerResponse handleFrictionlessToggle(const QHttpServerRequest &request);
    // 拍照识牌：POST /api/v1/lpr/recognize {image: base64}
    QHttpServerResponse handleLprRecognize(const QHttpServerRequest &request);

    // ---- 鉴权与响应辅助 ----
    std::optional<QString> bearerToken(const QHttpServerRequest &request) const;
    std::optional<AuthContext> authenticate(const QHttpServerRequest &request) const;
    std::optional<QJsonObject> bodyJson(const QHttpServerRequest &request) const;
    QJsonObject errorObject(const char *code, const QString &message) const;
    QHttpServerResponse jsonError(QHttpServerResponder::StatusCode status,
                                  const char *code, const QString &message) const;

    // ---- 业务动作（发布 EventHub 事件，与 TCP 动作同语义）----
    QJsonObject performEnter(const QString &plate, const QString &vehicleType,
                             bool *ok, QString *error);
    QJsonObject performLeave(const QString &plate, const QString &excludeOrderId,
                             bool *ok, QString *error);
    QJsonObject statusPayload() const;
    QJsonObject spotListPayload(bool includePlates) const;
    QJsonObject layoutPayload() const;
    QJsonObject routePayload(const Route &route) const;
    QJsonObject recordPayload(const ParkingRecord &record) const;
    QJsonObject reservationPayload(const Reservation &reservation) const;
    QJsonObject orderPayload(const PaymentOrder &order) const;

    // ---- 支付订单仓储 ----
    bool insertOrder(const PaymentOrder &order);
    bool updateOrderStatus(const QString &orderId, const QString &status,
                           qint64 paidAtMs, qint64 refundAtMs);
    void loadOrders();
    PaymentOrder *findOrder(const QString &orderId);
    static QString newOrderId();

    // ---- 无感支付与拍照识牌 ----
    void ensureFrictionlessSchema();
    void ensureSiteSchema();
    // 确保存在一个可用点位票据并返回其 token（没有则新建）。
    // 票据落库，因此服务端重启后张贴出去的二维码依然有效。
    QString ensureDefaultSiteTicket();
    QString frictionlessOwner(const QString &plate) const;
    void autoChargeOnExit(const QString &plate, double fee);
    QJsonObject recognizePlate(const QByteArray &imageBytes, QString *note);

    // ---- WebSocket 推送与定时扫描 ----
    void broadcastWs(const QString &name, const QJsonObject &payload);
    void sweep();

    ParkingService *service_;
    UserStore *users_;
    AuditLogService *audit_;
    EventHub *hub_;
    QSqlDatabase database_;
    // 启动时选定的默认点位票据，供终端打印取用。
    QString siteToken_;
    Options options_;
    QString lastError_;

    std::unique_ptr<QHttpServer> http_;
    QTcpServer *httpListener_{nullptr};
    QWebSocketServer *ws_{nullptr};
    QHash<QWebSocket *, WsSession> wsSessions_;
    QTimer sweepTimer_;
    QHash<QString, PaymentOrder> orders_;
    QHash<QString, qint64> paidLeaveGuard_;   // plate -> ms，防无感重复扣费
    quint16 httpPort_{0};
    quint16 wsPort_{0};
};

} // namespace smartpark
