#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

// 本地演示认证入口。当前项目尚未引入账号服务，因此仅在客户端校验
// 演示管理员凭据；QSettings 只保存“记住账号”的账号文本，不保存密码。
class LoginDialog : public QDialog{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);

    QString userName() const;

private slots:
    void attemptLogin();

private:
    QLineEdit *userNameInput_{nullptr};
    QLineEdit *passwordInput_{nullptr};
    QCheckBox *rememberUserCheck_{nullptr};
    QLabel *errorLabel_{nullptr};
    QPushButton *loginButton_{nullptr};
};
