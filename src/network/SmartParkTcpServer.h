#pragma once
#include "network/Protocol.h"

#include <QHash>
#include <QObject>
#include <QTimer>

#include <optional>

class QTcpSocket;
class QTcpServer;

namespace smartpark{

class ParkingService;
class AuditLogService;
class UserStore;
class EventHub;

// SmartPark TCP 服务端（协议 v1，见 docs/tcp-protocol.md）。
// 单线程事件驱动：所有会话共享一个 ParkingService，避免多线程锁库。
// 职责：会话与登录、心跳超时踢除、动作分发到核心服务、事件广播、审计。
class SmartParkTcpServer : public QObject{
    Q_OBJECT

public:
    struct Options{
        quint16 port{9527};
        int heartbeatTimeoutMs{60000};   // 超过该时长无任何帧即判定掉线
        int maxLoginFails{5};
        // 车牌识别命令模板（%1 = 图片路径）。留空则用由图片哈希生成的 mock 车牌。
        // 识别只在服务端跑：远程管理端把照片发过来，不在本地拉模型推理。
        QString lprCommand;
    };

    SmartParkTcpServer(ParkingService &service, AuditLogService *audit,
                       UserStore *users, Options options,
                       QObject *parent = nullptr);

    bool listen();
    quint16 port() const;
    const QString &lastError() const noexcept;
    // 设置事件枢纽后，广播事件同步发布给 REST/WS 订阅方；不影响 TCP 行为。
    void setEventHub(EventHub *hub) noexcept;

protected:
    void timerEvent(QTimerEvent *event) override;

private:
    struct Session{
        QTcpSocket *socket{nullptr};
        QByteArray buffer;
        bool authenticated{false};
        QString user;
        QString token;
        qint64 lastSeenMs{0};
        int loginFails{0};
    };

    void onNewConnection();
    void onReadyRead(Session &session);
    void onDisconnected(Session &session);
    void handleMessage(Session &session, const QJsonObject &message);
    void dispatch(Session &session, const QString &id,
                  const QString &action, const QJsonObject &payload);
    void send(Session &session, const QJsonObject &message);
    void respond(Session &session, const QString &id, bool ok,
                 const QJsonObject &payload = {}, const QString &error = {});
    // 只发给已认证的 TCP 会话。
    void sendToSessions(const QString &event, const QJsonObject &payload);
    // 发给 TCP 会话，同时发布到 hub（供 WebSocket 订阅）。
    void broadcastEvent(const QString &event, const QJsonObject &payload);
    void kickIdleSessions();

    // ---- 动作实现（返回应答 payload；失败时置 error）----
    QJsonObject actionLogin(Session &session, const QJsonObject &payload, bool *ok, QString *error);
    QJsonObject actionStatus(const QJsonObject &payload, bool *ok, QString *error);
    QJsonObject actionSpotList(const QJsonObject &payload, bool *ok, QString *error);
    QJsonObject actionEnter(const QJsonObject &payload, bool *ok, QString *error);
    QJsonObject actionLeave(const QJsonObject &payload, bool *ok, QString *error);
    QJsonObject actionReservationCreate(const QJsonObject &payload, bool *ok, QString *error);
    QJsonObject actionReservationCancel(const QJsonObject &payload, bool *ok, QString *error);
    QJsonObject actionReservationCheckIn(const QJsonObject &payload, bool *ok, QString *error);
    QJsonObject actionAnalyticsReport(const QJsonObject &payload, bool *ok, QString *error);
    // Gate 断线补报：按原始时间戳追溯应用离线期间的入场/离场事件。
    QJsonObject actionGateReplay(const QJsonObject &payload, bool *ok, QString *error);
    // 管理端专用全量快照：布局几何 + 车位明细 + 计数，仅 admin 账号可调用。
    QJsonObject actionAdminSnapshot(const QJsonObject &payload, bool *ok, QString *error);
    QJsonObject actionLprRecognize(const QJsonObject &payload, bool *ok, QString *error);

    ParkingService *service_;
    AuditLogService *audit_;
    UserStore *users_;
    EventHub *hub_{nullptr};
    Options options_;
    class QTcpServer *server_{nullptr};
    QHash<QTcpSocket *, Session> sessions_;
    int idleTimerId_{0};
    quint64 requestCounter_{0};
    QString lastError_;
};

} // namespace smartpark
