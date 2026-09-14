#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTimer>
#include "MainWindow.h"
#include "core/persistence/Persistence.h"
int main(int argc, char *argv[]){
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SmartPark Admin"));
    QApplication::setOrganizationName(QStringLiteral("SmartPark"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("SmartPark 管理员端"));
    parser.addHelpOption();
    const QCommandLineOption databaseOption(
        {QStringLiteral("d"), QStringLiteral("db")},
        QStringLiteral("SQLite 数据库文件路径，缺省使用用户数据目录 smartpark/smartpark.db"),
        QStringLiteral("path"));
    const QCommandLineOption smokeTestOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("启动窗口后自动退出，用于无人工交互的跨平台启动检查。"));
    parser.addOption(databaseOption);
    parser.addOption(smokeTestOption);
    parser.process(app);
    const QString databasePath = parser.isSet(databaseOption)
        ? parser.value(databaseOption)
        : smartpark::Persistence::defaultDatabasePath();
    MainWindow window(databasePath);
    window.show();
    if (parser.isSet(smokeTestOption)){
        QTimer::singleShot(1500, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
