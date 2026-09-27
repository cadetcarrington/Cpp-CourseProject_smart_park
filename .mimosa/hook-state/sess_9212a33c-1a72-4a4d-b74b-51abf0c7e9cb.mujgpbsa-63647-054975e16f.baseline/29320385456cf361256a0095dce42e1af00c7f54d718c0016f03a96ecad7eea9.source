#pragma once

#include <QDialog>
#include <QString>

#include "UserStore.h"

class QLabel;
class QLineEdit;
class QPushButton;

// 新管理员注册：账号 + 密码 + 确认密码，实时校验并写入 UserStore。
// 注册成功后 accept()，登录界面会用 registeredUserName() 预填账号。
class RegisterDialog : public QDialog{
    Q_OBJECT

public:
    explicit RegisterDialog(smartpark_ui::UserStore &userStore,
                            QWidget *parent = nullptr);

    QString registeredUserName() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void refreshPasswordStrength();
    void attemptRegister();

private:
    void markInvalid(QLineEdit *input, bool invalid) const;

    smartpark_ui::UserStore &userStore_;
    QLineEdit *userNameInput_{nullptr};
    QLineEdit *passwordInput_{nullptr};
    QLineEdit *confirmInput_{nullptr};
    QLabel *strengthLabel_{nullptr};
    QLabel *errorLabel_{nullptr};
    QPushButton *registerButton_{nullptr};
    QPixmap backdrop_;
    bool vibrancyActive_{false};
};
