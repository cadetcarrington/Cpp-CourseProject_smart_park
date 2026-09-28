#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QMessageBox>
#include <QTimer>
#include <QUuid>

#include "LoginDialog.h"
#include "MainWindow.h"
#include "core/service/UserStore.h"
#include "core/persistence/Persistence.h"
#include "network/TcpClient.h"

namespace{
// 缺省连接本机服务端；可用 --server host:port 指向其他部署。
constexpr const char *kDefaultServerHost = "127.0.0.1";
constexpr quint16 kDefaultServerPort = 9527;

// 远程登录：一次性同步 TCP 会话验证账号口令（登录阶段无主窗口），
// 成功后 MainWindow 内的 ServerSession 用相同凭据建立长连接。
// 远程管理端仅接受 admin 账号：服务端按动作鉴权，这里提前给出明确提示。
bool remoteLogin(const QString &host, quint16 port,
                 const QString &user, const QString &pass, QString *error){
    if (user != QStringLiteral("admin")){
        *error = QStringLiteral("远程管理端需要 admin 账号（Gate/用户端账号无快照权限）。");
        return false;
    }
    smartpark::TcpClient client;
    if (!client.connectToHost(host, port, 4000)){
        *error = QStringLiteral("无法连接服务端 %1:%2（%3）")
                     .arg(host).arg(port).arg(client.lastError());
        return false;
    }
    const auto reply = client.request(
        QStringLiteral("login"),
        QJsonObject{{QStringLiteral("user"), user},
                    {QStringLiteral("pass"), pass}}, 5000);
    if (!reply.has_value()){
        *error = QStringLiteral("服务端无响应：%1").arg(client.lastError());
        return false;
    }
    if (!reply->value(QStringLiteral("ok")).toBool()){
        *error = reply->value(QStringLiteral("error")).toString();
        if (error->isEmpty()){
            *error = QStringLiteral("账号或密码错误");
        }
        return false;
    }
    return true;
}
} // namespace

int main(int argc, char *argv[]){
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SmartPark Admin"));
    QApplication::setOrganizationName(QStringLiteral("SmartPark"));
    QApplication::setOrganizationDomain(QStringLiteral("smartpark.local"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "SmartPark 管理员端（默认连接 TCP 服务端，与 Gate 共享服务端状态）"));
    parser.addHelpOption();
    const QCommandLineOption databaseOption(
        {QStringLiteral("d"), QStringLiteral("db")},
        QStringLiteral("本地模式 SQLite 数据库文件路径，缺省使用用户数据目录 smartpark/smartpark.db"),
        QStringLiteral("path"));
    const QCommandLineOption serverOption(
        QStringLiteral("server"),
        QStringLiteral("远程服务端 host:port（缺省 127.0.0.1:9527）"),
        QStringLiteral("endpoint"));
    const QCommandLineOption localOption(
        QStringLiteral("local"),
        QStringLiteral("强制本地数据模式（不连接服务端，行为与旧版本一致）。"));
    const QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("跳过交互登录并在启动窗口后自动退出，用于无人工交互的跨平台启动检查。"));
    parser.addOption(databaseOption);
    parser.addOption(serverOption);
    parser.addOption(localOption);
    parser.addOption(smokeTestOption);
    parser.process(app);

    const QString databasePath = parser.isSet(databaseOption)
        ? parser.value(databaseOption)
        : smartpark::Persistence::defaultDatabasePath();

    // Smoke test is intentionally non-interactive. Normal launches always begin at the login page.
    if (parser.isSet(smokeTestOption)){
        // 未显式指定数据库时改用临时库，避免恢复失败时弹模态框卡住自动化。
        QString smokePath = databasePath;
        if (!parser.isSet(databaseOption)){
            smokePath = QDir::tempPath()
                + QStringLiteral("/smartpark-smoke-%1.db")
                      .arg(QUuid::createUuid().toString(QUuid::Id128));
        }
        MainWindow window(smokePath, QStringLiteral("系统检查"));
        window.show();
        QTimer::singleShot(1500, &app, &QCoreApplication::quit);
        return app.exec();
    }

    // 远程模式（默认）：登录账号在服务端校验，界面数据来自服务端快照；
    // --local 保留旧的本地数据模式。
    const bool remoteMode = !parser.isSet(localOption);
    QString serverHost = kDefaultServerHost;
    quint16 serverPort = kDefaultServerPort;
    if (remoteMode){
        QString endpoint = parser.value(serverOption);
        if (endpoint.isEmpty()){
            endpoint = QStringLiteral("%1:%2")
                           .arg(serverHost).arg(serverPort);
        }
        const int separator = endpoint.lastIndexOf(QLatin1Char(':'));
        if (separator <= 0){
            QMessageBox::critical(nullptr, QStringLiteral("SmartPark"),
                QStringLiteral("--server 参数应为 host:port：%1").arg(endpoint));
            return 1;
        }
        serverHost = endpoint.left(separator);
        serverPort = static_cast<quint16>(
            endpoint.mid(separator + 1).toUShort());
        if (serverPort == 0){
            QMessageBox::critical(nullptr, QStringLiteral("SmartPark"),
                QStringLiteral("--server 端口无效：%1").arg(endpoint));
            return 1;
        }
    }

    // 本地模式需要账号库；远程模式不创建本地 UserStore（避免把演示账号
    // 播种进本地库），登录完全由服务端校验，注册入口同步隐藏。
    smartpark::UserStore userStore(databasePath);
    if (!remoteMode && !userStore.lastError().isEmpty()){
        QMessageBox::critical(
            nullptr, QStringLiteral("SmartPark"),
            QStringLiteral("账号数据库打开失败：%1").arg(userStore.lastError()));
        return 1;
    }

    // Closing a window normally exits the application. A requested logout closes the current
    // session window and re-enters this loop, so users return to LoginDialog instead of quitting.
    // 登录/注册账号与停车数据同库存放（users 表）；空库会自动播种演示账号。
    while (true){
        LoginDialog login(remoteMode ? nullptr : &userStore);
        QString serverPassword;
        if (remoteMode){
            login.setRemoteAuthenticator(
                [&](const QString &user, const QString &pass, QString *error){
                    if (remoteLogin(serverHost, serverPort, user, pass, error)){
                        serverPassword = pass;
                        return true;
                    }
                    return false;
                });
        }
        if (login.exec() != QDialog::Accepted){
            break;
        }

        MainWindow *window = nullptr;
        if (remoteMode){
            window = new MainWindow(serverHost, serverPort, login.userName(),
                                    serverPassword);
        } else{
            window = new MainWindow(databasePath, login.userName());
        }
        bool shouldReturnToLogin = false;
        QObject::connect(window, &MainWindow::logoutRequested, window, [&]{
            shouldReturnToLogin = true;
            window->close();
        });
        window->show();
        app.exec();
        delete window;
        if (!shouldReturnToLogin){
            break;
        }
    }
    return 0;
}
