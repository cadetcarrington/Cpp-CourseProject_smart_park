#include <QNetworkProxy>
#include "BarrierGate.h"
#include "GateSelftest.h"
#include "OfflineQueue.h"
#include "QtGateClock.h"
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

// 状态中文名已移到 BarrierGate::stateText()，终端与自测共用同一份。

class GateApp : public QObject{
public:
    GateApp(QString host, quint16 port, QString user, QString pass,
            QString role, QString queuePath)
        : host_(std::move(host)), port_(port), user_(std::move(user)),
          pass_(std::move(pass)), role_(std::move(role)),
          queue_(std::move(queuePath)){
        barrier_.onLog = [](const std::string &text){ std::cout << text << '\n'; };
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
                  << "道闸终端。输入车牌回车 = 车辆识别放行；当前车型："
                  << vehicleType_.toStdString() << "\n"
                  << "命令：status | type car|motorcycle|truck|electric | pass（地感：车辆通过）"
                     " | fault on|off | reset | help | quit\n";
    }

    // 车型允许在闸口切换：它直接影响服务端的车位分配与计费规则，
    // 原来写死为 car，摩托车/货车/新能源车都会被当成轿车处理。
    void setVehicleType(const QString &type){
        static const QStringList allowed = {QStringLiteral("car"),
                                            QStringLiteral("motorcycle"),
                                            QStringLiteral("truck"),
                                            QStringLiteral("electric")};
        if (!allowed.contains(type)){
            log(QStringLiteral("车型必须是 car / motorcycle / truck / electric"));
            return;
        }
        vehicleType_ = type;
        log(QStringLiteral("车型已切换为 %1").arg(type));
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
            const int applied = result.value(QStringLiteral("applied")).toInt();
            const int duplicate = result.value(QStringLiteral("duplicate")).toInt();
            const int skipped = result.value(QStringLiteral("skipped")).toInt();
            // 只出队服务端已明确处理完的前缀：若响应在第 N 条被截断，前 N-1 条
            // 已经生效的不会被重复上报，其余留在本地队列下次重报。
            const int handled =
                smartpark::gate::replayHandledPrefix(result, (int)events.size());
            if (handled > 0 && queue_.acknowledge(handled)){
                log(QStringLiteral("补报完成：本批 %1 条，已确认 %2 条（应用 %3，去重 %4，丢弃 %5）")
                        .arg(events.size()).arg(handled)
                        .arg(applied).arg(duplicate).arg(skipped));
                // 被服务端判定为无法追溯的事件要如实报出来，不能悄悄丢掉。
                for (const QString &detail :
                     smartpark::gate::replaySkippedDetails(result, handled)){
                    log(QStringLiteral("  [丢弃] %1").arg(detail));
                }
                if (handled < (int)events.size()){
                    log(QStringLiteral("本批仍有 %1 条未获确认，保留在本地队列稍后重报")
                            .arg((int)events.size() - handled));
                }
                return;
            }
            log(QStringLiteral("补报未获确认（提交 %1，应用 %2，去重 %3，丢弃 %4），事件保留在本地队列")
                    .arg(events.size()).arg(applied).arg(duplicate).arg(skipped));
            return;
        }
        log(QStringLiteral("补报请求失败，事件保留在本地队列"));
    }

    // 离线模式：本地放行 + 事件入队（重连后补报）。
    void recordOffline(const QString &kind, const QString &plate){
        QJsonObject event;
        event.insert(QStringLiteral("kind"), kind);
        event.insert(QStringLiteral("plate"), plate);
        event.insert(QStringLiteral("vehicleType"), vehicleType_);
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
            log(QStringLiteral("道闸状态：%1 | 连接：%2 | 离线缓存：%3 条 | 车型：%4")
                    .arg(QString::fromUtf8(
                             smartpark::gate::BarrierGate::stateText(barrier_.state())),
                         client_.connected() ? QStringLiteral("在线")
                                             : QStringLiteral("离线"))
                    .arg(queue_.size())
                    .arg(vehicleType_));
            return;
        }
        if (line.startsWith(QStringLiteral("type "))){
            setVehicleType(line.mid(5).trimmed());
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
                        {QStringLiteral("vehicleType"), vehicleType_}},
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
    QString vehicleType_{QStringLiteral("car")};
    smartpark::gate::OfflineQueue queue_;
    // 时钟必须先于道闸构造：BarrierGate 持有它的引用。
    smartpark::gate::QtGateClock clock_;
    smartpark::gate::BarrierGate barrier_{clock_};
    TcpClient client_;
    QTimer reconnectTimer_;
    QTimer heartbeatTimer_;
    std::unique_ptr<QSocketNotifier> notifier_;
    bool authenticated_{false};
};
} // namespace

int main(int argc, char **argv){
    QCoreApplication app(argc, argv);

    // 这是局域网服务/客户端，不该走系统代理。但 Qt 会自动读取 all_proxy /
    // http_proxy 等环境变量并套用到**所有** socket——包括监听 socket，
    // 于是 listen() 会直接失败："proxy type is invalid for this operation"。
    // 集群与 CI 环境常带这些变量（s1 上 all_proxy=socks5h://...），
    // 所以显式关掉，避免部署时服务端起不来。
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

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
