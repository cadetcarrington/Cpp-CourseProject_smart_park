#include "LoginDialog.h"
#include "NativeEffects.h"
#include "RegisterDialog.h"
#include "Theme.h"

#include <QAction>
#include <QCheckBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QVBoxLayout>
#include <chrono>

namespace{
constexpr int kMaxFailedAttempts = 5;
constexpr int kLockdownSeconds = 30;
}

LoginDialog::LoginDialog(smartpark_ui::UserStore &userStore, QWidget *parent)
    : QDialog(parent)
    , userStore_(userStore){
    setWindowTitle(tr("登录 SmartPark 管理后台"));
    setModal(true);
    setFixedSize(460, 620);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

#ifdef Q_OS_MAC
    // 原生毛玻璃在 Qt 6.8 半透明窗口下会盖住控件层（实机复现），
    // 默认自绘光斑背景；SMARTPARK_NATIVE_BLUR=1 时作为实验路径启用。
    if (qEnvironmentVariableIsSet("SMARTPARK_NATIVE_BLUR")){
        setAttribute(Qt::WA_TranslucentBackground);
        vibrancyActive_ = smartpark_ui::applyNativeVibrancy(this, false);
    }
#endif
    setStyleSheet(theme::authDialogStyleSheet(vibrancyActive_));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(34, 32, 34, 26);
    root->setSpacing(0);

    auto *brandMark = new QLabel(QStringLiteral("SP"), this);
    brandMark->setObjectName("loginBrandMark");
    brandMark->setAlignment(Qt::AlignCenter);
    brandMark->setFixedSize(52, 52);

    auto *title = new QLabel(tr("SmartPark"), this);
    title->setObjectName("loginTitle");
    QFont titleFont = title->font();
    titleFont.setPointSize(25);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto *subtitle = new QLabel(tr("智能停车场管理系统"), this);
    subtitle->setObjectName("loginSubtitle");
    subtitle->setWordWrap(true);

    auto *heading = new QHBoxLayout;
    heading->setSpacing(14);
    heading->addWidget(brandMark, 0, Qt::AlignTop);
    auto *headingText = new QVBoxLayout;
    headingText->setSpacing(3);
    headingText->addWidget(title);
    headingText->addWidget(subtitle);
    heading->addLayout(headingText, 1);
    root->addLayout(heading);
    root->addSpacing(28);

    auto *card = new QFrame(this);
    card->setObjectName("loginCard");
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 22, 24, 20);
    cardLayout->setSpacing(12);

    auto *cardTitle = new QLabel(tr("管理员登录"), card);
    cardTitle->setObjectName("loginCardTitle");
    QFont cardTitleFont = cardTitle->font();
    cardTitleFont.setPointSize(15);
    cardTitleFont.setBold(true);
    cardTitle->setFont(cardTitleFont);
    cardLayout->addWidget(cardTitle);

    auto *instruction = new QLabel(
        tr("使用管理员账号登录。"), card);
    instruction->setObjectName("loginInstruction");
    instruction->setWordWrap(true);
    cardLayout->addWidget(instruction);
    cardLayout->addSpacing(2);

    userNameInput_ = new QLineEdit(card);
    userNameInput_->setObjectName("loginUserNameInput");
    userNameInput_->setPlaceholderText(tr("请输入账号"));
    userNameInput_->setClearButtonEnabled(true);
    userNameInput_->setAccessibleName(tr("账号"));

    passwordInput_ = new QLineEdit(card);
    passwordInput_->setObjectName("loginPasswordInput");
    passwordInput_->setPlaceholderText(tr("请输入密码"));
    passwordInput_->setEchoMode(QLineEdit::Password);
    passwordInput_->setAccessibleName(tr("密码"));

    auto *togglePasswordAction = passwordInput_->addAction(
        QStringLiteral("👁"), QLineEdit::TrailingPosition);
    togglePasswordAction->setToolTip(tr("显示 / 隐藏密码"));
    togglePasswordAction->setCheckable(true);
    connect(togglePasswordAction, &QAction::toggled, this,
            &LoginDialog::togglePasswordVisible);

    cardLayout->addWidget(userNameInput_);
    cardLayout->addWidget(passwordInput_);

    rememberUserCheck_ = new QCheckBox(tr("记住账号"), card);
    rememberUserCheck_->setObjectName("rememberUserCheck");
    cardLayout->addWidget(rememberUserCheck_);

    errorLabel_ = new QLabel(card);
    errorLabel_->setObjectName("loginErrorLabel");
    errorLabel_->setWordWrap(true);
    errorLabel_->setVisible(false);
    cardLayout->addWidget(errorLabel_);

    loginButton_ = new QPushButton(tr("登录"), card);
    loginButton_->setObjectName("loginButton");
    loginButton_->setDefault(true);
    loginButton_->setAutoDefault(true);
    loginButton_->setMinimumHeight(38);
    cardLayout->addWidget(loginButton_);

    registerLink_ = new QPushButton(tr("注册新账号"), card);
    registerLink_->setObjectName("registerLink");
    registerLink_->setCursor(Qt::PointingHandCursor);
    registerLink_->setFlat(true);
    cardLayout->addWidget(registerLink_);

    auto *demoHint = new QLabel(
        tr("演示账号：admin　密码：smartpark。也可注册新账号。"), card);
    demoHint->setObjectName("demoAccountHint");
    demoHint->setWordWrap(true);
    cardLayout->addWidget(demoHint);

    root->addWidget(card);
    root->addStretch(1);

    auto *footer = new QLabel(tr("SmartPark Admin"), this);
    footer->setObjectName("loginFooter");
    footer->setAlignment(Qt::AlignCenter);
    root->addWidget(footer);

    QSettings settings;
    const bool rememberUser =
        settings.value(QStringLiteral("Session/rememberUser"), false).toBool();
    rememberUserCheck_->setChecked(rememberUser);
    if (rememberUser){
        userNameInput_->setText(
            settings.value(QStringLiteral("Session/lastUser")).toString());
        passwordInput_->setFocus();
    } else{
        userNameInput_->setFocus();
    }

    lockdownTimer_.setInterval(std::chrono::seconds(1));
    connect(&lockdownTimer_, &QTimer::timeout, this, &LoginDialog::tickLockdown);

    connect(loginButton_, &QPushButton::clicked, this, &LoginDialog::attemptLogin);
    connect(registerLink_, &QPushButton::clicked, this,
            &LoginDialog::openRegisterDialog);
    connect(userNameInput_, &QLineEdit::textChanged, this, [this]{
        userNameInput_->setProperty("invalid", false);
    });
    connect(userNameInput_, &QLineEdit::returnPressed, passwordInput_,
            qOverload<>(&QWidget::setFocus));
    connect(passwordInput_, &QLineEdit::returnPressed, this,
            &LoginDialog::attemptLogin);
    connect(passwordInput_, &QLineEdit::textChanged, this, [this]{
        passwordInput_->setProperty("invalid", false);
    });
}

QString LoginDialog::userName() const{
    return userNameInput_->text().trimmed();
}

void LoginDialog::paintEvent(QPaintEvent *){
    if (vibrancyActive_){
        return;  // macOS 原生毛玻璃负责背景
    }
    if (backdrop_.isNull() || backdrop_.size() != size()){
        backdrop_ = theme::auroraBackdrop(size());
    }
    QPainter painter(this);
    painter.drawPixmap(0, 0, backdrop_);
}

void LoginDialog::togglePasswordVisible(){
    auto *action = qobject_cast<QAction *>(sender());
    passwordInput_->setEchoMode(action && action->isChecked()
                                    ? QLineEdit::Normal
                                    : QLineEdit::Password);
}

void LoginDialog::openRegisterDialog(){
    RegisterDialog dialog(userStore_, this);
    if (dialog.exec() == QDialog::Accepted){
        userNameInput_->setText(dialog.registeredUserName());
        passwordInput_->setFocus();
        setError(QString(), nullptr);
    }
}

void LoginDialog::tickLockdown(){
    --lockdownSeconds_;
    if (lockdownSeconds_ > 0){
        loginButton_->setText(tr("尝试过于频繁，%1 秒后可重试").arg(lockdownSeconds_));
        return;
    }
    setLockdown(false);
}

void LoginDialog::setLockdown(bool locked){
    if (locked){
        lockdownSeconds_ = kLockdownSeconds;
        loginButton_->setEnabled(false);
        userNameInput_->setEnabled(false);
        passwordInput_->setEnabled(false);
        loginButton_->setText(
            tr("尝试过于频繁，%1 秒后可重试").arg(lockdownSeconds_));
        lockdownTimer_.start();
        return;
    }
    lockdownTimer_.stop();
    failedAttempts_ = 0;
    userNameInput_->setEnabled(true);
    passwordInput_->setEnabled(true);
    loginButton_->setEnabled(true);
    loginButton_->setText(tr("登录"));
    passwordInput_->setFocus();
}

void LoginDialog::setError(const QString &message, QLineEdit *focusTarget){
    errorLabel_->setText(message);
    errorLabel_->setVisible(!message.isEmpty());
    if (focusTarget != nullptr){
        focusTarget->setFocus();
    }
}

void LoginDialog::markInvalid(QLineEdit *input, bool invalid) const{
    input->setProperty("invalid", invalid);
    input->style()->unpolish(input);
    input->style()->polish(input);
}

void LoginDialog::attemptLogin(){
    if (!loginButton_->isEnabled()){
        return;
    }
    const QString userName = userNameInput_->text().trimmed();
    const QString password = passwordInput_->text();

    if (userName.isEmpty()){
        markInvalid(userNameInput_, true);
        setError(tr("请输入账号。"), userNameInput_);
        return;
    }
    if (password.isEmpty()){
        markInvalid(passwordInput_, true);
        setError(tr("请输入密码。"), passwordInput_);
        return;
    }

    const auto result = userStore_.verifyLogin(userName, password);
    if (result == smartpark_ui::UserStore::LoginResult::Success){
        QSettings settings;
        settings.setValue(QStringLiteral("Session/rememberUser"),
                          rememberUserCheck_->isChecked());
        if (rememberUserCheck_->isChecked()){
            settings.setValue(QStringLiteral("Session/lastUser"), userName);
        } else{
            settings.remove(QStringLiteral("Session/lastUser"));
        }
        accept();
        return;
    }

    ++failedAttempts_;
    if (result == smartpark_ui::UserStore::LoginResult::WrongPassword){
        markInvalid(passwordInput_, true);
        passwordInput_->selectAll();
        if (failedAttempts_ >= kMaxFailedAttempts){
            setError(tr("连续 %1 次密码错误，已临时锁定登录。").arg(failedAttempts_),
                     nullptr);
            setLockdown(true);
            return;
        }
        setError(smartpark_ui::UserStore::loginErrorText(result)
                     + tr("（还可尝试 %1 次）").arg(kMaxFailedAttempts - failedAttempts_),
                 passwordInput_);
        return;
    }
    if (result == smartpark_ui::UserStore::LoginResult::UnknownUser){
        markInvalid(userNameInput_, true);
        setError(smartpark_ui::UserStore::loginErrorText(result), userNameInput_);
        return;
    }
    setError(smartpark_ui::UserStore::loginErrorText(result), nullptr);
}
