#include "network/PlateRecognition.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QTemporaryFile>

namespace smartpark::network{

QString mockPlateFromImage(const QByteArray &imageBytes){
    static const char *kProvinces[] = {
        "京", "沪", "粤", "苏", "浙", "皖", "鲁", "豫", "鄂", "湘"};
    static const char *kLetters = "ABCDEFGHJKLMNPQRSTUVWXYZ";
    static const char *kDigits = "0123456789ABCDEFGHJKMNPQRSTUVWXYZ";
    const QByteArray digest =
        QCryptographicHash::hash(imageBytes, QCryptographicHash::Sha256);
    QString plate;
    plate += kProvinces[static_cast<quint8>(digest.at(0)) % 10];
    plate += kLetters[static_cast<quint8>(digest.at(1)) % 24];
    for (int i = 2; i < 7; ++i){
        plate += kDigits[static_cast<quint8>(digest.at(i)) % 33];
    }
    return plate;
}

QJsonObject recognizePlate(const QByteArray &imageBytes,
                           const QString &lprCommand, QString *note){
    if (lprCommand.isEmpty()){
        *note = QStringLiteral("mock");
        return QJsonObject{{QStringLiteral("plate"), mockPlateFromImage(imageBytes)},
                           {QStringLiteral("confidence"), 0.87},
                           {QStringLiteral("source"), QStringLiteral("mock")}};
    }
    // 脚本桥接：写临时图片 -> 执行命令模板（%1 = 图片路径）-> 解析 stdout JSON。
    QTemporaryFile imageFile(QDir::tempPath()
                             + QStringLiteral("/smartpark-lpr-XXXXXX.jpg"));
    imageFile.setAutoRemove(true);
    if (!imageFile.open()){
        *note = QStringLiteral("temp file failed");
        return {};
    }
    imageFile.write(imageBytes);
    imageFile.close();
    const QString command = QString(lprCommand).arg(imageFile.fileName());
    QProcess process;
    process.start(QStringLiteral("/bin/sh"),
                  QStringList{QStringLiteral("-c"), command});
    if (!process.waitForStarted(3000)
        || !process.waitForFinished(60000)){
        *note = QStringLiteral("script timeout or start failed");
        return {};
    }
    const QByteArray stdoutBytes = process.readAllStandardOutput();
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(stdoutBytes, &parseError);
    const QString scriptPlate =
        document.object().value(QStringLiteral("plate")).toString();
    if (parseError.error != QJsonParseError::NoError || scriptPlate.isEmpty()){
        *note = QStringLiteral("unexpected script output");
        return {};
    }
    QJsonObject result = document.object();
    // recognize_plate.py 的键是 recognition_confidence，统一补充 confidence 供前端展示。
    if (!result.contains(QStringLiteral("confidence"))
        && result.contains(QStringLiteral("recognition_confidence"))){
        result.insert(QStringLiteral("confidence"),
                      result.value(QStringLiteral("recognition_confidence")).toDouble());
    }
    *note = QStringLiteral("script");
    return result;
}

} // namespace smartpark::network
