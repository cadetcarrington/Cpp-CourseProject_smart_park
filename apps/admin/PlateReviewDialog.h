#pragma once

#include <QDialog>
#include <QImage>
#include <QString>

class QLabel;
class QLineEdit;
class QProcess;
class QPushButton;

class PlateReviewDialog : public QDialog{
    Q_OBJECT

public:
    explicit PlateReviewDialog(QString imagePath, QWidget *parent = nullptr);
    ~PlateReviewDialog() override;
    QString plate() const;

private slots:
    void showResult(const QByteArray &output);

private:
    void chooseImage();
    void recognize();
    void stopRecognition();
    void updatePreview();

    QString imagePath_;
    QImage image_;
    QProcess *process_{nullptr};
    QLabel *preview_{nullptr};
    QLabel *crop_{nullptr};
    QLabel *status_{nullptr};
    QLabel *confidence_{nullptr};
    QLineEdit *plate_{nullptr};
    QPushButton *retry_{nullptr};
    QPushButton *use_{nullptr};
};
