#pragma once

#include <QDialog>
#include <QString>
#include <QTimer>

#include "UserStore.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

// 管理端登录入口：认证数据来自 UserStore（SQLite users 表）。
// 登录逻辑包含空字段校验、账号不存在 / 密码错误分别提示、
// 连续失败 5 次锁定 30 秒、密码可见切换、记住账号与注册入口。
// QSettings 只保存"记住账号"的账号文本，不保存密码。
class LoginDialog : public QDialog{
    Q_OBJECT

public:
    explicit LoginDialog(smartpark_ui::UserStore &userStore,
                         QWidget *parent = nullptr);

    QString userName() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void attemptLogin();
    void togglePasswordVisible();
    void openRegisterDialog();
    void tickLockdown();

private:
    void setError(const QString &message, QLineEdit *focusTarget);
    void markInvalid(QLineEdit *input, bool invalid) const;
    void setLockdown(bool locked);

    smartpark_ui::UserStore &userStore_;
    QLineEdit *userNameInput_{nullptr};
    QLineEdit *passwordInput_{nullptr};
    QCheckBox *rememberUserCheck_{nullptr};
    QLabel *errorLabel_{nullptr};
    QPushButton *loginButton_{nullptr};
    QPushButton *registerLink_{nullptr};
    QTimer lockdownTimer_;
    int failedAttempts_{0};
    int lockdownSeconds_{0};
    QPixmap backdrop_;
    bool vibrancyActive_{false};
};
