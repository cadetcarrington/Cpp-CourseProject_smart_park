#include "PlateReviewDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QPixmap>
#include <QPen>

#include <cmath>
#include <utility>

PlateReviewDialog::PlateReviewDialog(QString imagePath, QWidget *parent)
    : QDialog(parent), imagePath_(std::move(imagePath)){
    setWindowTitle(tr("车牌识别审阅"));
    setMinimumSize(680, 600);
    auto *layout = new QVBoxLayout(this);
    preview_ = new QLabel(this);
    preview_->setObjectName("platePreview");
    preview_->setMinimumHeight(300);
    preview_->setAlignment(Qt::AlignCenter);
    crop_ = new QLabel(this);
    crop_->setObjectName("plateCrop");
    crop_->setFixedHeight(85);
    crop_->setAlignment(Qt::AlignCenter);
    status_ = new QLabel(this);
    status_->setObjectName("recognitionStatus");
    status_->setWordWrap(true);
    confidence_ = new QLabel(this);
    confidence_->setObjectName("recognitionConfidence");
    plate_ = new QLineEdit(this);
    plate_->setObjectName("recognizedPlate");
    plate_->setPlaceholderText(tr("候选车牌"));
    auto *controls = new QHBoxLayout;
    auto *choose = new QPushButton(tr("换图片"), this);
    choose->setObjectName("choosePlateImage");
    retry_ = new QPushButton(tr("重新识别"), this);
    retry_->setObjectName("retryPlateRecognition");
    controls->addWidget(choose);
    controls->addWidget(retry_);
    controls->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    use_ = buttons->addButton(tr("使用车牌"), QDialogButtonBox::AcceptRole);
    use_->setObjectName("useRecognizedPlate");
    use_->setEnabled(false);
    layout->addWidget(preview_, 1);
    layout->addWidget(crop_);
    layout->addWidget(status_);
    layout->addWidget(confidence_);
    layout->addWidget(plate_);
    layout->addLayout(controls);
    layout->addWidget(buttons);
    connect(choose, &QPushButton::clicked, this, &PlateReviewDialog::chooseImage);
    connect(retry_, &QPushButton::clicked, this, &PlateReviewDialog::recognize);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(use_, &QPushButton::clicked, this, &QDialog::accept);
    connect(plate_, &QLineEdit::textChanged, this, [this](const QString &text){
        use_->setEnabled(process_ == nullptr && !text.trimmed().isEmpty()
                         && !image_.isNull() && !crop_->pixmap().isNull());
    });
    image_ = QImage(imagePath_);
    updatePreview();
    QTimer::singleShot(0, this, &PlateReviewDialog::recognize);
}

PlateReviewDialog::~PlateReviewDialog(){
    stopRecognition();
}

QString PlateReviewDialog::plate() const{
    return plate_->text().trimmed();
}

void PlateReviewDialog::stopRecognition(){
    if (process_ == nullptr){
        return;
    }
    disconnect(process_, nullptr, this, nullptr);
    if (process_->state() != QProcess::NotRunning){
        process_->terminate();
        if (!process_->waitForFinished(3000)){
            process_->kill();
            process_->waitForFinished(3000);
        }
    }
    process_->deleteLater();
    process_ = nullptr;
}

void PlateReviewDialog::chooseImage(){
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择车辆图片"), QFileInfo(imagePath_).absolutePath(),
        tr("图片 (*.jpg *.jpeg *.png *.bmp)"));
    if (path.isEmpty()){
        return;
    }
    stopRecognition();
    imagePath_ = path;
    image_ = QImage(path);
    crop_->clear();
    plate_->clear();
    confidence_->clear();
    updatePreview();
    recognize();
}

void PlateReviewDialog::updatePreview(){
    if (image_.isNull()){
        preview_->setText(tr("无法打开图片"));
        return;
    }
    preview_->setPixmap(QPixmap::fromImage(image_).scaled(
        640, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void PlateReviewDialog::recognize(){
    stopRecognition();
    plate_->clear();
    confidence_->clear();
    crop_->clear();
    use_->setEnabled(false);
    updatePreview();
    if (image_.isNull()){
        status_->setText(tr("图片无法读取，请更换图片。"));
        return;
    }
    const QString detectorPython = qEnvironmentVariable("SMARTPARK_LPR_PY");
    const QString ocrPython = qEnvironmentVariable("SMARTPARK_OCR_PY");
    const QString script = qEnvironmentVariable(
        "SMARTPARK_LPR_SCRIPT", QStringLiteral(SMARTPARK_LPR_SCRIPT_PATH));
    if (!QFileInfo::exists(detectorPython) || !QFileInfo::exists(ocrPython)
        || !QFileInfo::exists(script)){
        status_->setText(tr("请配置 SMARTPARK_LPR_PY、SMARTPARK_OCR_PY 与识别脚本路径。"));
        return;
    }
    process_ = new QProcess(this);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    process_->setProcessEnvironment(environment);
    retry_->setEnabled(false);
    use_->setEnabled(false);
    status_->setText(tr("正在识别 %1…").arg(QFileInfo(imagePath_).fileName()));
    auto *running = process_;
    connect(running, &QProcess::errorOccurred, this, [this, running](QProcess::ProcessError error){
        if (error == QProcess::FailedToStart && process_ == running){
            status_->setText(tr("无法启动识别程序：%1").arg(running->errorString()));
            retry_->setEnabled(true);
            stopRecognition();
        }
    });
    connect(running, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, running](int exitCode, QProcess::ExitStatus exitStatus){
        if (process_ != running){
            return;
        }
        const QByteArray output = running->readAllStandardOutput();
        const QString error = QString::fromUtf8(running->readAllStandardError()).trimmed();
        const bool timedOut = running->property("timedOut").toBool();
        stopRecognition();
        retry_->setEnabled(true);
        if (exitStatus != QProcess::NormalExit || exitCode != 0){
            status_->setText(timedOut ? tr("识别超时，请重试。")
                                      : error.isEmpty() ? tr("识别程序异常退出。") : error);
            return;
        }
        showResult(output);
    });
    QTimer::singleShot(210000, this, [this, running]{
        if (process_ == running && running->state() != QProcess::NotRunning){
            running->setProperty("timedOut", true);
            running->kill();
        }
    });
    running->start(detectorPython, {script, imagePath_, QStringLiteral("--ocr-python"), ocrPython});
}

void PlateReviewDialog::showResult(const QByteArray &output){
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(output, &error);
    const QJsonObject result = document.object();
    const QJsonValue text = result.value(QStringLiteral("plate"));
    const QJsonValue detector = result.value(QStringLiteral("detection_confidence"));
    const QJsonValue recognizer = result.value(QStringLiteral("recognition_confidence"));
    const QJsonValue valid = result.value(QStringLiteral("valid"));
    const QJsonArray box = result.value(QStringLiteral("bounding_box")).toArray();
    const double det = detector.toDouble();
    const double rec = recognizer.toDouble();
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || !text.isString() || text.toString().trimmed().isEmpty()
        || !detector.isDouble() || !recognizer.isDouble() || !valid.isBool()
        || !std::isfinite(det) || !std::isfinite(rec)
        || det < 0 || det > 1 || rec < 0 || rec > 1 || box.size() != 4){
        status_->setText(tr("识别返回的数据格式无效。"));
        return;
    }
    int coords[4];
    for (int i = 0; i < 4; ++i){
        if (!box[i].isDouble() || !std::isfinite(box[i].toDouble())
            || box[i].toDouble() != std::floor(box[i].toDouble())){
            status_->setText(tr("识别框数据无效。"));
            return;
        }
        coords[i] = box[i].toInt(-1);
        if (coords[i] < 0 || static_cast<double>(coords[i]) != box[i].toDouble()){
            status_->setText(tr("识别框数据无效。"));
            return;
        }
    }
    const QRect bounds(coords[0], coords[1], coords[2] - coords[0], coords[3] - coords[1]);
    if (bounds.width() <= 0 || bounds.height() <= 0 || !image_.rect().contains(bounds)){
        status_->setText(tr("识别框超出图片范围。"));
        return;
    }
    QImage marked = image_;
    QPainter painter(&marked);
    painter.setPen(QPen(QColor("#d64135"), qMax(2, image_.width() / 250)));
    painter.drawRect(bounds.adjusted(0, 0, -1, -1));
    painter.end();
    preview_->setPixmap(QPixmap::fromImage(marked).scaled(
        640, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    crop_->setPixmap(QPixmap::fromImage(image_.copy(bounds)).scaled(
        420, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    plate_->setText(text.toString().trimmed());
    confidence_->setText(tr("检测 %1%  ·  识别 %2%")
                         .arg(det * 100, 0, 'f', 1).arg(rec * 100, 0, 'f', 1));
    status_->setText(!valid.toBool() || det < 0.5 || rec < 0.8
                         ? tr("候选车牌格式或置信度需复核；请检查图片并修改后采用。")
                         : tr("请核对车牌与图片，再选择使用车牌。"));
    use_->setEnabled(true);
}
