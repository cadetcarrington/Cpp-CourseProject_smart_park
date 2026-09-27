#include "core/service/UserStore.h"
#include "network/SmartParkTcpServer.h"
#include "network/TcpClient.h"

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
#include <QTimer>

#include <iostream>
#include <memory>

namespace{
using smartpark::TcpClient;

class UserApp : public QObject{
public:
    UserApp(QString host, quint16 port, QString user, QString pass)
        : host_(std::move(host)), port_(port), user_(std::move(user)),
          pass_(std::move(pass)){
        client_.onEvent = [this](const QJsonObject &event){
            std::cout << "[推送] "
                      << event.value(QStringLiteral("event")).toString().toStdString()
                      << " "
                      << QJsonDocument(event.value(QStringLiteral("payload")).toObject())
                             .toJson(QJsonDocument::Compact).toStdString()
                      << '\n';
        };
        reconnectTimer_.setInterval(5000);
        connect(&reconnectTimer_, &QTimer::timeout, this, &UserApp::tryConnect);
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
        std::cout << "SmartPark 用户端（已连接则实时接收车位事件）\n"
                  << "命令：status | reserve <车牌> [开始偏移分钟(默认60)] [时长分钟(默认120)]"
                  << " | cancel <车牌> | help | quit\n";
    }

private:
    void log(const QString &text){
        std::cout << text.toStdString() << '\n';
    }

    void tryConnect(){
        if (client_.connected() && authenticated_) return;
        authenticated_ = false;
        client_.setToken({});
        if (client_.connected()) client_.disconnectFromHost();
        if (!client_.connectToHost(host_, port_, 2000)){
            log(QStringLiteral("服务端不可达：%1").arg(client_.lastError()));
            return;
        }
        const auto login = client_.request(QStringLiteral("login"),
            QJsonObject{{QStringLiteral("user"), user_},
                        {QStringLiteral("pass"), pass_}});
        if (!login.has_value() || !login->value(QStringLiteral("ok")).toBool()){
            log(QStringLiteral("登录失败"));
            client_.disconnectFromHost();
            return;
        }
        client_.setToken(login->value(QStringLiteral("payload")).toObject()
                             .value(QStringLiteral("token")).toString());
        authenticated_ = true;
        log(QStringLiteral("已连接 %1:%2（%3）").arg(host_).arg(port_).arg(user_));
    }

    void printStatus(const QJsonObject &payload){
        log(QStringLiteral("余位 %1 / %2（占用 %3、预约 %4）")
                .arg(payload.value(QStringLiteral("available")).toInt())
                .arg(payload.value(QStringLiteral("capacity")).toInt())
                .arg(payload.value(QStringLiteral("occupied")).toInt())
                .arg(payload.value(QStringLiteral("reserved")).toInt()));
    }

    void handleCommand(const QString &line){
        if (line.isEmpty()){
            return;
        }
        const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        const QString command = parts.first();
        if (command == QStringLiteral("help")){
            log(QStringLiteral("status | reserve <车牌> [偏移分钟 时长分钟] | "
                               "cancel <车牌> | quit"));
            return;
        }
        if (command == QStringLiteral("quit")){
            qApp->quit();
            return;
        }
        if (!client_.connected() || !authenticated_){
            log(QStringLiteral("服务端离线，稍后自动重连"));
            return;
        }
        if (command == QStringLiteral("status")){
            const auto status = client_.request(QStringLiteral("parking.status"), {});
            if (status.has_value() && status->value(QStringLiteral("ok")).toBool()){
                printStatus(status->value(QStringLiteral("payload")).toObject());
            } else{
                log(QStringLiteral("查询失败"));
            }
            return;
        }
        if (command == QStringLiteral("reserve")){
            if (parts.size() < 2){
                log(QStringLiteral("用法：reserve <车牌> [开始偏移分钟 时长分钟]"));
                return;
            }
            const QString plate = parts.at(1);
            const int offset = parts.size() > 2 ? parts.at(2).toInt() : 60;
            const int duration = parts.size() > 3 ? parts.at(3).toInt() : 120;
            QJsonObject payload;
            payload.insert(QStringLiteral("plate"), plate);
            payload.insert(QStringLiteral("vehicleType"), QStringLiteral("car"));
            payload.insert(QStringLiteral("startMs"),
                           QDateTime::currentMSecsSinceEpoch()
                               + qint64(offset) * 60 * 1000);
            payload.insert(QStringLiteral("durationMin"), duration);
            const auto reserved = client_.request(QStringLiteral("reservation.create"),
                                                  payload);
            if (reserved.has_value() && reserved->value(QStringLiteral("ok")).toBool()){
                const QJsonObject result =
                    reserved->value(QStringLiteral("payload")).toObject();
                log(QStringLiteral("预约成功：%1 → %2（定金 %3 元，开始于 %4 分钟后）")
                        .arg(plate, result.value(QStringLiteral("spotId")).toString())
                        .arg(result.value(QStringLiteral("deposit")).toDouble(), 0, 'f', 0)
                        .arg(offset));
                log(QStringLiteral("预期路线：入口 %1 → %2，行驶 %3m、%4 次转向；出口 %5，离场 %6m")
                        .arg(result.value(QStringLiteral("entranceIndex")).toInt() + 1)
                        .arg(result.value(QStringLiteral("spotId")).toString())
                        .arg(result.value(QStringLiteral("entryDistance")).toDouble(), 0, 'f', 1)
                        .arg(result.value(QStringLiteral("entryTurns")).toInt())
                        .arg(result.value(QStringLiteral("exitIndex")).toInt() + 1)
                        .arg(result.value(QStringLiteral("exitDistance")).toDouble(), 0, 'f', 1));
            } else{
                log(QStringLiteral("预约失败：%1")
                        .arg(reserved.has_value()
                                 ? reserved->value(QStringLiteral("error")).toString()
                                 : client_.lastError()));
            }
            return;
        }
        if (command == QStringLiteral("cancel")){
            if (parts.size() < 2){
                log(QStringLiteral("用法：cancel <车牌>"));
                return;
            }
            const auto cancelled = client_.request(QStringLiteral("reservation.cancel"),
                QJsonObject{{QStringLiteral("plate"), parts.at(1)}});
            log(cancelled.has_value() && cancelled->value(QStringLiteral("ok")).toBool()
                    ? QStringLiteral("预约已取消，定金将退回")
                    : QStringLiteral("取消失败：%1")
                          .arg(cancelled.has_value()
                                   ? cancelled->value(QStringLiteral("error")).toString()
                                   : client_.lastError()));
            return;
        }
        log(QStringLiteral("未知命令，help 查看用法"));
    }

    QString host_;
    quint16 port_;
    QString user_;
    QString pass_;
    TcpClient client_;
    QTimer reconnectTimer_;
    QTimer heartbeatTimer_;
    std::unique_ptr<QSocketNotifier> notifier_;
    bool authenticated_{false};
};
} // namespace

int main(int argc, char **argv){
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("SmartPark User"));
    QCoreApplication::setOrganizationName(QStringLiteral("SmartPark"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("SmartPark 用户端（预约/余位查询，实时接收车位事件）"));
    parser.addHelpOption();
    const QCommandLineOption hostOption({QStringLiteral("H"), QStringLiteral("host")},
                                        QStringLiteral("服务端地址"),
                                        QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    const QCommandLineOption portOption({QStringLiteral("p"), QStringLiteral("port")},
                                        QStringLiteral("服务端端口"),
                                        QStringLiteral("port"), QStringLiteral("9527"));
    const QCommandLineOption userOption({QStringLiteral("u"), QStringLiteral("user")},
                                        QStringLiteral("登录账号"),
                                        QStringLiteral("user"), QStringLiteral("user"));
    const QCommandLineOption passOption({QStringLiteral("s"), QStringLiteral("pass")},
                                        QStringLiteral("登录密码"),
                                        QStringLiteral("pass"), QStringLiteral("smartpark"));
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.addOption(userOption);
    parser.addOption(passOption);
    parser.process(app);

    UserApp appImpl(parser.value(hostOption),
                    static_cast<quint16>(parser.value(portOption).toUShort()),
                    parser.value(userOption), parser.value(passOption));
    appImpl.start();
    return app.exec();
}
