#include "BarrierGate.h"
#include "network/TcpClient.h"

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
#include <QTimer>

#include <iostream>
#include <memory>

namespace{
using smartpark::TcpClient;

const char *stateText(smartpark::gate::BarrierGate::State state){
    using State = smartpark::gate::BarrierGate::State;
    switch (state){
    case State::Closed: return "关闭";
    case State::Opening: return "抬杆中";
    case State::Open: return "保持";
    case State::Closing: return "落闸中";
    case State::Fault: return "故障";
    }
    return "未知";
}

class GateApp : public QObject{
public:
    GateApp(QString host, quint16 port, QString user, QString pass,
            QString role, QString queuePath)
        : host_(std::move(host)), port_(port), user_(std::move(user)),
          pass_(std::move(pass)), role_(std::move(role)),
          queue_(std::move(queuePath)){
        barrier_.onLog = [](const QString &text){ std::cout << text.toStdString() << '\n'; };
        client_.onEvent = [this](const QJsonObject &event){
            std::cout << "[事件] "
                      << event.value(QStringLiteral("event")).toString().toStdString()
                      << " "
                      << QJsonDocument(event.value(QStringLiteral("payload")).toObject())
                             .toJson(QJsonDocument::Compact).toStdString()
                      << '\n';
        };
        reconnectTimer_.setInterval(5000);
        connect(&reconnectTimer_, &QTimer::timeout, this, [this]{
            if (client_.connected() && authenticated_) flushQueue();
            else tryConnect();
        });
        heartbeatTimer_.setInterval(20000);
        connect(&heartbeatTimer_, &QTimer::timeout, this, [this]{
            if (!client_.connected() || !authenticated_) return;
            const auto response = client_.request(QStringLiteral("heartbeat"), {}, 2500);
            if (!response || !response->value(QStringLiteral("ok")).toBool()){
                authenticated_ = false;
                client_.disconnectFromHost();
            }
        });
    }

    void start(){
        reconnectTimer_.start();
        heartbeatTimer_.start();
        tryConnect();
        // stdin 行命令（异步，不阻塞事件循环与心跳）。
        if (fileno(stdin) >= 0){
            notifier_ = std::make_unique<QSocketNotifier>(fileno(stdin),
                                                          QSocketNotifier::Read);
            connect(notifier_.get(), &QSocketNotifier::activated, this, [this]{
                std::string line;
                if (!std::getline(std::cin, line)){
                    notifier_->setEnabled(false);
                    return;
                }
                handleCommand(QString::fromStdString(line).trimmed());
            });
        }
        printHelp();
    }

private:
    void printHelp(){
        std::cout << (role_ == QStringLiteral("entrance")
                           ? "入口"
                           : "出口")
                  << "道闸终端。输入车牌回车 = 车辆识别放行；命令：status | fault on|off | reset | help | quit\n";
    }

    void log(const QString &text){
        std::cout << text.toStdString() << '\n';
    }

    void tryConnect(){
        if (client_.connected() && authenticated_) return;
        authenticated_ = false;
        client_.setToken({});
        if (client_.connected()) client_.disconnectFromHost();
        if (!client_.connectToHost(host_, port_, 2000)){
            log(QStringLiteral("服务端不可达（%1），进入离线模式：车辆放行本地记账，事件缓存待补报")
                    .arg(client_.lastError()));
            return;
        }
        const auto login = client_.request(QStringLiteral("login"),
            QJsonObject{{QStringLiteral("user"), user_},
                        {QStringLiteral("pass"), pass_}});
        if (!login.has_value() || !login->value(QStringLiteral("ok")).toBool()){
            log(QStringLiteral("登录失败：%1")
                    .arg(login ? login->value(QStringLiteral("error")).toString()
                               : client_.lastError()));
            client_.disconnectFromHost();
            return;
        }
        client_.setToken(login->value(QStringLiteral("payload")).toObject()
                             .value(QStringLiteral("token")).toString());
        authenticated_ = true;
        log(QStringLiteral("已连接服务端 %1:%2（%3）").arg(host_).arg(port_).arg(user_));
        flushQueue();
    }

    void flushQueue(){
        if (!client_.connected() || !authenticated_ || queue_.size() == 0){
            return;
        }
        const QJsonArray events = queue_.pending();
        if (events.isEmpty()){
            log(QStringLiteral("离线队列读取失败或数据损坏，保留原文件"));
            return;
        }
        QJsonObject payload;
        payload.insert(QStringLiteral("events"), events);
        const auto replay = client_.request(QStringLiteral("gate.replay"), payload);
        if (replay && replay->value(QStringLiteral("ok")).toBool()){
            const QJsonObject result = replay->value(QStringLiteral("payload")).toObject();
            if (result.value(QStringLiteral("applied")).toInt()
                    + result.value(QStringLiteral("duplicate")).toInt() == events.size()
                && queue_.acknowledge(events.size())){
                log(QStringLiteral("补报完成：应用 %1 条，去重 %2 条")
                        .arg(result.value(QStringLiteral("applied")).toInt())
                        .arg(result.value(QStringLiteral("duplicate")).toInt()));
                return;
            }
        }
        log(QStringLiteral("补报未完全确认，事件保留在本地队列"));
    }

    // 离线模式：本地放行 + 事件入队（重连后补报）。
    void recordOffline(const QString &kind, const QString &plate){
        QJsonObject event;
        event.insert(QStringLiteral("kind"), kind);
        event.insert(QStringLiteral("plate"), plate);
        event.insert(QStringLiteral("vehicleType"), QStringLiteral("car"));
        event.insert(QStringLiteral("ts"),
                     QDateTime::currentMSecsSinceEpoch());
        if (!queue_.append(event)){
            log(QStringLiteral("离线事件落盘失败，禁止放行：%1").arg(plate));
            return;
        }
        barrier_.requestOpen();
        log(QStringLiteral("[离线] %1 %2 已本地放行并缓存（待补报 %3 条）")
                .arg(kind == QStringLiteral("enter") ? QStringLiteral("入场")
                                                     : QStringLiteral("离场"),
                     plate)
                .arg(queue_.size()));
    }

    void handleCommand(const QString &line){
        if (line.isEmpty()){
            return;
        }
        if (line == QStringLiteral("help")){
            printHelp();
            return;
        }
        if (line == QStringLiteral("quit")){
            qApp->quit();
            return;
        }
        if (line == QStringLiteral("status")){
            log(QStringLiteral("道闸状态：%1 | 连接：%2 | 离线缓存：%3 条")
                    .arg(stateText(barrier_.state()),
                         client_.connected() ? QStringLiteral("在线")
                                             : QStringLiteral("离线"))
                    .arg(queue_.size()));
            return;
        }
        if (line == QStringLiteral("fault on")){
            barrier_.setFault(true);
            return;
        }
        if (line == QStringLiteral("fault off")){
            barrier_.setFault(false);
            return;
        }
        if (line == QStringLiteral("reset")){
            barrier_.reset();
            return;
        }
        if (line.startsWith(QStringLiteral("pass"))){
            barrier_.vehiclePassed();   // 模拟地感：车辆通过
            return;
        }

        // 车牌识别（手输模拟假 LPR）。
        const QString plate = line;
        if (!client_.connected() || !authenticated_){
            recordOffline(role_ == QStringLiteral("entrance") ? QStringLiteral("enter")
                                                              : QStringLiteral("exit"),
                          plate);
            return;
        }
        if (role_ == QStringLiteral("entrance")){
            handleEntrance(plate);
        } else{
            handleExit(plate);
        }
    }

    void handleEntrance(const QString &plate){
        // ParkingService::enter automatically checks in a matching reservation.
        const auto enter = client_.request(QStringLiteral("parking.enter"),
            QJsonObject{{QStringLiteral("plate"), plate},
                        {QStringLiteral("vehicleType"), QStringLiteral("car")}},
            2500);
        if (!enter.has_value() || !enter->value(QStringLiteral("ok")).toBool()){
            log(QStringLiteral("拒绝入场：%1（%2）")
                    .arg(plate,
                         enter.has_value()
                             ? enter->value(QStringLiteral("error")).toString()
                             : client_.lastError()));
            return;
        }
        log(QStringLiteral("欢迎入场：%1 → %2（步行 %3m）")
                .arg(plate,
                     enter->value(QStringLiteral("payload")).toObject()
                         .value(QStringLiteral("spotId")).toString())
                .arg(enter->value(QStringLiteral("payload")).toObject()
                         .value(QStringLiteral("entryDistance")).toDouble(),
                     0, 'f', 1));
        barrier_.requestOpen();
    }

    void handleExit(const QString &plate){
        const auto leave = client_.request(QStringLiteral("parking.leave"),
            QJsonObject{{QStringLiteral("plate"), plate}}, 2500);
        if (!leave.has_value() || !leave->value(QStringLiteral("ok")).toBool()){
            log(QStringLiteral("拒绝离场：%1（%2）")
                    .arg(plate,
                         leave.has_value()
                             ? leave->value(QStringLiteral("error")).toString()
                             : client_.lastError()));
            return;
        }
        const QJsonObject payload = leave->value(QStringLiteral("payload")).toObject();
        log(QStringLiteral("安全离场：%1 | 时长 %2 分钟 | 费用 %3 元")
                .arg(plate)
                .arg(payload.value(QStringLiteral("durationMin")).toDouble(), 0, 'f', 0)
                .arg(payload.value(QStringLiteral("fee")).toDouble(), 0, 'f', 2));
        barrier_.requestOpen();
    }

    QString host_;
    quint16 port_;
    QString user_;
    QString pass_;
    QString role_;
    smartpark::gate::OfflineQueue queue_;
    smartpark::gate::BarrierGate barrier_;
    TcpClient client_;
    QTimer reconnectTimer_;
    QTimer heartbeatTimer_;
    std::unique_ptr<QSocketNotifier> notifier_;
    bool authenticated_{false};
};
} // namespace

int main(int argc, char **argv){
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("SmartPark Gate"));
    QCoreApplication::setOrganizationName(QStringLiteral("SmartPark"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("SmartPark Gate 出入口终端（假 LPR：手输车牌 + 模拟道闸 + 断线补报）"));
    parser.addHelpOption();
    const QCommandLineOption modeOption(
        {QStringLiteral("m"), QStringLiteral("mode")},
        QStringLiteral("闸口角色：entrance（入口）或 exit（出口）"),
        QStringLiteral("mode"), QStringLiteral("entrance"));
    const QCommandLineOption hostOption({QStringLiteral("H"), QStringLiteral("host")},
                                        QStringLiteral("服务端地址"),
                                        QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    const QCommandLineOption portOption({QStringLiteral("p"), QStringLiteral("port")},
                                        QStringLiteral("服务端端口"),
                                        QStringLiteral("port"), QStringLiteral("9527"));
    const QCommandLineOption userOption({QStringLiteral("u"), QStringLiteral("user")},
                                        QStringLiteral("登录账号"),
                                        QStringLiteral("user"), QStringLiteral("gate"));
    const QCommandLineOption passOption({QStringLiteral("s"), QStringLiteral("pass")},
                                        QStringLiteral("登录密码"),
                                        QStringLiteral("pass"), QStringLiteral("smartpark"));
    const QCommandLineOption queueOption(QStringLiteral("queue"),
                                         QStringLiteral("离线事件缓存文件"),
                                         QStringLiteral("path"));
    const QCommandLineOption selftestOption(QStringLiteral("selftest"),
                                            QStringLiteral("运行道闸状态机与离线队列自测"));
    parser.addOption(modeOption);
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.addOption(userOption);
    parser.addOption(passOption);
    parser.addOption(queueOption);
    parser.addOption(selftestOption);
    parser.process(app);

    if (parser.isSet(selftestOption)){
        return smartpark::gate::runSelftest();
    }

    const QString role = parser.value(modeOption);
    if (role != QStringLiteral("entrance") && role != QStringLiteral("exit")){
        std::cerr << "--mode 必须为 entrance 或 exit\n";
        return 1;
    }
    const QString queuePath = parser.isSet(queueOption)
        ? parser.value(queueOption)
        : QStringLiteral("gate-%1-queue.jsonl").arg(role);

    GateApp appImpl(parser.value(hostOption),
                    static_cast<quint16>(parser.value(portOption).toUShort()),
                    parser.value(userOption), parser.value(passOption),
                    role, queuePath);
    appImpl.start();
    return app.exec();
}
