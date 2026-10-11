#include "network/PlateRecognition.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QStringList>
#include <QTemporaryFile>
#include <QTimer>

#include <memory>

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

namespace{

// 把识别脚本的输出收成一句话：优先第一行非空文本，限长，便于放进用户可见的
// message 里（原话也能帮排障）。
QString firstLineOf(const QByteArray &text){
    const QString decoded = QString::fromUtf8(text).trimmed();
    const QStringList lines = decoded.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty()){
        return {};
    }
    QString line = lines.first().trimmed();
    if (line.size() > 160){
        line = line.left(160) + QStringLiteral("…");
    }
    return line;
}

bool looksLikeNoPlate(const QString &text){
    const QString lowered = text.toLower();
    return lowered.contains(QStringLiteral("no license plate"))
        || lowered.contains(QStringLiteral("no plate"))
        || lowered.contains(QStringLiteral("未识别到车牌"))
        || lowered.contains(QStringLiteral("未检测到车牌"));
}

// 命令模板：默认按 %1 替换成图片路径；没写 %1 的（测试替身、固定输出脚本）
// 就简单追加路径，别让 QString::arg 在日志里刷 "Argument missing" 警告。
QString buildCommand(const QString &lprCommand, const QString &imagePath){
    if (lprCommand.contains(QLatin1String("%1"))){
        return QString(lprCommand).arg(imagePath);
    }
    return lprCommand + QLatin1Char(' ') + imagePath;
}

// 把图片写成临时文件供脚本读取；失败返回空字符串。
QString writeImageFile(const QByteArray &imageBytes, QObject *owner){
    auto *imageFile = new QTemporaryFile(
        QDir::tempPath() + QStringLiteral("/smartpark-lpr-XXXXXX.jpg"), owner);
    imageFile->setAutoRemove(true);
    if (!imageFile->open()){
        delete imageFile;
        return {};
    }
    imageFile->write(imageBytes);
    imageFile->close();
    return imageFile->fileName();
}

} // namespace

namespace{
QJsonObject mockResult(const QByteArray &imageBytes){
    return QJsonObject{{QStringLiteral("plate"), mockPlateFromImage(imageBytes)},
                       {QStringLiteral("confidence"), 0.87},
                       {QStringLiteral("source"), QStringLiteral("mock")}};
}
} // namespace

LprOutcome classifyLprOutput(int exitCode, const QByteArray &stdoutBytes,
                             const QByteArray &stderrBytes){
    LprOutcome outcome;
    QJsonParseError parseError{};
    const QJsonDocument document =
        QJsonDocument::fromJson(stdoutBytes, &parseError);
    const QString scriptPlate =
        document.object().value(QStringLiteral("plate")).toString();
    if (parseError.error == QJsonParseError::NoError && !scriptPlate.isEmpty()){
        outcome.status = LprStatus::Ok;
        outcome.result = document.object();
        // recognize_plate.py 的键是 recognition_confidence，统一补 confidence 供前端展示。
        if (!outcome.result.contains(QStringLiteral("confidence"))
            && outcome.result.contains(QStringLiteral("recognition_confidence"))){
            outcome.result.insert(
                QStringLiteral("confidence"),
                outcome.result.value(QStringLiteral("recognition_confidence")).toDouble());
        }
        outcome.note = QStringLiteral("script");
        return outcome;
    }
    // 没解析出车牌：脚本可能只是「画面里没有车牌」，也可能是真出错。
    // 两种情况给客户端的说法完全不同，所以分开归类（网页端据此提示换照片）。
    const QString stdoutText = QString::fromUtf8(stdoutBytes).trimmed();
    const QString stderrText = QString::fromUtf8(stderrBytes).trimmed();
    outcome.detail = firstLineOf(stdoutBytes);
    if (outcome.detail.isEmpty()){
        outcome.detail = firstLineOf(stderrBytes);
    }
    if (looksLikeNoPlate(stdoutText) || looksLikeNoPlate(stderrText)){
        outcome.status = LprStatus::NoPlate;
        outcome.note = QStringLiteral("未识别到车牌");
        return outcome;
    }
    outcome.status = LprStatus::Failed;
    outcome.note = QStringLiteral("识别脚本未返回可用结果");
    if (outcome.detail.isEmpty() && exitCode != 0){
        outcome.note += QStringLiteral("（退出码 %1）").arg(exitCode);
    }
    return outcome;
}

QJsonObject recognizePlate(const QByteArray &imageBytes,
                           const QString &lprCommand, QString *note){
    if (lprCommand.isEmpty()){
        if (note != nullptr){
            *note = QStringLiteral("mock");
        }
        return mockResult(imageBytes);
    }
    // 脚本桥接：写临时图片 -> 执行命令模板（%1 = 图片路径）-> 解析 stdout JSON。
    QTemporaryFile imageFile(QDir::tempPath()
                             + QStringLiteral("/smartpark-lpr-XXXXXX.jpg"));
    imageFile.setAutoRemove(true);
    if (!imageFile.open()){
        if (note != nullptr){
            *note = QStringLiteral("临时文件创建失败");
        }
        return {};
    }
    imageFile.write(imageBytes);
    imageFile.close();
    QProcess process;
    process.start(QStringLiteral("/bin/sh"),
                  QStringList{QStringLiteral("-c"),
                              buildCommand(lprCommand, imageFile.fileName())});
    if (!process.waitForStarted(3000)
        || !process.waitForFinished(60000)){
        if (note != nullptr){
            *note = QStringLiteral("识别脚本启动失败或超时");
        }
        return {};
    }
    const LprOutcome outcome = classifyLprOutput(
        process.exitCode(), process.readAllStandardOutput(),
        process.readAllStandardError());
    if (note != nullptr){
        *note = outcome.note;
    }
    return outcome.result;
}

void recognizePlateAsync(const QByteArray &imageBytes, const QString &lprCommand,
                         QObject *context, std::function<void(LprOutcome)> done){
    if (context == nullptr || !done){
        return;
    }
    // mock：没有脚本可跑，但保持「异步回调」的约定，调用方只写一条路径。
    if (lprCommand.isEmpty()){
        QTimer::singleShot(0, context, [imageBytes, done]{
            LprOutcome outcome;
            outcome.status = LprStatus::Ok;
            outcome.result = mockResult(imageBytes);
            outcome.note = QStringLiteral("mock");
            done(outcome);
        });
        return;
    }
    // 进程先建：临时文件挂在进程下，随进程一起销毁（不然临时图会攒到进程退出）。
    auto *process = new QProcess(context);
    const QString imagePath = writeImageFile(imageBytes, process);
    if (imagePath.isEmpty()){
        QTimer::singleShot(0, context, [done]{
            LprOutcome outcome;
            outcome.status = LprStatus::Failed;
            outcome.note = QStringLiteral("临时文件创建失败");
            done(outcome);
        });
        return;
    }
    // 看门狗：脚本卡住时结束进程，回调照常发生（旧实现是 60 秒 waitForFinished）。
    auto *watchdog = new QTimer(process);
    watchdog->setSingleShot(true);
    watchdog->setInterval(60000);
    auto finished = std::make_shared<bool>(false);
    const auto finish = [process, done, finished](LprOutcome outcome){
        if (*finished){
            return;   // 超时与正常结束可能都到：只回调一次
        }
        *finished = true;
        done(outcome);
        process->deleteLater();
    };
    QObject::connect(watchdog, &QTimer::timeout, process, [process, finish]{
        process->kill();
        LprOutcome outcome;
        outcome.status = LprStatus::Failed;
        outcome.note = QStringLiteral("识别脚本超时（60 秒）");
        finish(outcome);
    });
    QObject::connect(process, &QProcess::errorOccurred, process,
                     [process, finish](QProcess::ProcessError error){
        if (error != QProcess::FailedToStart){
            return;   // 其它错误交给 finished 处理
        }
        LprOutcome outcome;
        outcome.status = LprStatus::Failed;
        outcome.note = QStringLiteral("识别脚本无法启动");
        outcome.detail = process->errorString();
        finish(outcome);
    });
    QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     process, [process, finish](int exitCode, QProcess::ExitStatus){
        finish(classifyLprOutput(exitCode, process->readAllStandardOutput(),
                                 process->readAllStandardError()));
    });
    process->start(QStringLiteral("/bin/sh"),
                   QStringList{QStringLiteral("-c"),
                               buildCommand(lprCommand, imagePath)});
    watchdog->start();
}

} // namespace smartpark::network
