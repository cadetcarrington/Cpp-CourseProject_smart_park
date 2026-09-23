#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTimer>

#include "LoginDialog.h"
#include "MainWindow.h"
#include "core/persistence/Persistence.h"

int main(int argc, char *argv[]){
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SmartPark Admin"));
    QApplication::setOrganizationName(QStringLiteral("SmartPark"));
    QApplication::setOrganizationDomain(QStringLiteral("smartpark.local"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("SmartPark 管理员端"));
    parser.addHelpOption();
    const QCommandLineOption databaseOption(
        {QStringLiteral("d"), QStringLiteral("db")},
        QStringLiteral("SQLite 数据库文件路径，缺省使用用户数据目录 smartpark/smartpark.db"),
        QStringLiteral("path"));
    const QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("跳过交互登录并在启动窗口后自动退出，用于无人工交互的跨平台启动检查。"));
    parser.addOption(databaseOption);
    parser.addOption(smokeTestOption);
    parser.process(app);

    const QString databasePath = parser.isSet(databaseOption)
        ? parser.value(databaseOption)
        : smartpark::Persistence::defaultDatabasePath();

    // Smoke test is intentionally non-interactive. Normal launches always begin at the login page.
    if (parser.isSet(smokeTestOption)){
        MainWindow window(databasePath, QStringLiteral("系统检查"));
        window.show();
        QTimer::singleShot(1500, &app, &QCoreApplication::quit);
        return app.exec();
    }

    // Closing a window normally exits the application. A requested logout closes the current
    // session window and re-enters this loop, so users return to LoginDialog instead of quitting.
    while (true){
        LoginDialog login;
        if (login.exec() != QDialog::Accepted){
            break;
        }

        MainWindow window(databasePath, login.userName());
        bool shouldReturnToLogin = false;
        QObject::connect(&window, &MainWindow::logoutRequested, &window, [&]{
            shouldReturnToLogin = true;
            window.close();
        });
        window.show();
        app.exec();
        if (!shouldReturnToLogin){
            break;
        }
    }
    return 0;
}
