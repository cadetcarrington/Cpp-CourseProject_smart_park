#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>

#include "MainWindow.h"
#include "core/persistence/Persistence.h"

int main(int argc, char *argv[])
{
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
    parser.addOption(databaseOption);
    parser.process(app);

    const QString databasePath = parser.isSet(databaseOption)
        ? parser.value(databaseOption)
        : smartpark::Persistence::defaultDatabasePath();

    MainWindow window(databasePath);
    window.show();

    return app.exec();
}
