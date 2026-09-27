#include "RegisterDialog.h"
#include "NativeEffects.h"
#include "Theme.h"

#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

RegisterDialog::RegisterDialog(smartpark_ui::UserStore &userStore, QWidget *parent)
    : QDialog(parent)
    , userStore_(userStore){
    setWindowTitle(tr("注册 SmartPark 账号"));
    setModal(true);
    setFixedSize(460, 560);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

#ifdef Q_OS_MAC

    // 与 LoginDialog 相同：默认自绘光斑，SMARTPARK_NATIVE_BLUR=1 才走原生毛玻璃。
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

    auto *title = new QLabel(tr("创建管理员账号"), this);
    title->setObjectName("loginTitle");
    QFont titleFont = title->font();
    titleFont.setPointSize(21);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto *subtitle = new QLabel(tr("注册后即可登录管理后台。"), this);
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
    root->addSpacing(24);

    auto *card = new QFrame(this);
    card->setObjectName("loginCard");
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 22, 24, 20);
    cardLayout->setSpacing(10);

    userNameInput_ = new QLineEdit(card);
    userNameInput_->setObjectName("registerUserNameInput");
    userNameInput_->setPlaceholderText(tr("账号（2-24 个字符，不含空格）"));
    userNameInput_->setClearButtonEnabled(true);

    passwordInput_ = new QLineEdit(card);
    passwordInput_->setObjectName("registerPasswordInput");
    passwordInput_->setPlaceholderText(tr("密码（至少 6 位）"));
    passwordInput_->setEchoMode(QLineEdit::Password);

    confirmInput_ = new QLineEdit(card);
    confirmInput_->setObjectName("registerConfirmInput");
    confirmInput_->setPlaceholderText(tr("再次输入密码"));
    confirmInput_->setEchoMode(QLineEdit::Password);

    cardLayout->addWidget(userNameInput_);
    cardLayout->addWidget(passwordInput_);
    cardLayout->addWidget(confirmInput_);

    strengthLabel_ = new QLabel(tr("密码至少 6 位，建议 12 位以上。"), card);
    strengthLabel_->setObjectName("strengthLabel");
    strengthLabel_->setWordWrap(true);
    cardLayout->addWidget(strengthLabel_);

    errorLabel_ = new QLabel(card);
    errorLabel_->setObjectName("loginErrorLabel");
    errorLabel_->setWordWrap(true);
    errorLabel_->setVisible(false);
    cardLayout->addWidget(errorLabel_);

    registerButton_ = new QPushButton(tr("注册账号"), card);
    registerButton_->setObjectName("loginButton");
    registerButton_->setDefault(true);
    registerButton_->setAutoDefault(true);
    registerButton_->setMinimumHeight(38);
    cardLayout->addWidget(registerButton_);

    root->addWidget(card);
    root->addStretch(1);

    auto *footer = new QLabel(tr("SmartPark · 本地运营终端"), this);
    footer->setObjectName("loginFooter");
    footer->setAlignment(Qt::AlignCenter);
    root->addWidget(footer);

    connect(userNameInput_, &QLineEdit::textChanged, this, [this]{
        markInvalid(userNameInput_, false);
    });
    connect(passwordInput_, &QLineEdit::textChanged, this, [this]{
        markInvalid(passwordInput_, false);
        refreshPasswordStrength();
    });
    connect(confirmInput_, &QLineEdit::textChanged, this, [this]{
        markInvalid(confirmInput_, false);
    });
    connect(registerButton_, &QPushButton::clicked, this, &RegisterDialog::attemptRegister);
    connect(confirmInput_, &QLineEdit::returnPressed, this, &RegisterDialog::attemptRegister);

    userNameInput_->setFocus();
}

QString RegisterDialog::registeredUserName() const{
    return userNameInput_->text().trimmed();
}

void RegisterDialog::paintEvent(QPaintEvent *){
    if (vibrancyActive_){
        return;  // 原生毛玻璃负责背景
    }
    if (backdrop_.isNull() || backdrop_.size() != size()){
        backdrop_ = theme::auroraBackdrop(size());
    }
    QPainter painter(this);
    painter.drawPixmap(0, 0, backdrop_);
}

void RegisterDialog::refreshPasswordStrength(){
    const QString password = passwordInput_->text();
    QString strength;
    if (password.isEmpty()){
        strength = tr("密码至少 6 位，建议 12 位以上。");
    } else if (password.size() < 8){
        strength = tr("密码强度：弱");
    } else if (password.size() < 12){
        strength = tr("密码强度：中");
    } else{
        strength = tr("密码强度：强");
    }
    strengthLabel_->setText(strength);
}

void RegisterDialog::markInvalid(QLineEdit *input, bool invalid) const{
    input->setProperty("invalid", invalid);
    input->style()->unpolish(input);
    input->style()->polish(input);
}

void RegisterDialog::attemptRegister(){
    const QString userName = userNameInput_->text().trimmed();
    const QString password = passwordInput_->text();
    const QString confirm = confirmInput_->text();

    errorLabel_->setVisible(false);
    markInvalid(userNameInput_, false);
    markInvalid(passwordInput_, false);
    markInvalid(confirmInput_, false);

    if (userName.isEmpty() || password.isEmpty()){
        errorLabel_->setText(smartpark_ui::UserStore::registerErrorText(
            smartpark_ui::UserStore::RegisterResult::EmptyFields));
        errorLabel_->setVisible(true);
        return;
    }
    if (!smartpark_ui::UserStore::isValidUserName(userName)){
        markInvalid(userNameInput_, true);
        errorLabel_->setText(smartpark_ui::UserStore::registerErrorText(
            smartpark_ui::UserStore::RegisterResult::InvalidUserName));
        errorLabel_->setVisible(true);
        return;
    }
    if (!smartpark_ui::UserStore::isValidPassword(password)){
        markInvalid(passwordInput_, true);
        errorLabel_->setText(smartpark_ui::UserStore::registerErrorText(
            smartpark_ui::UserStore::RegisterResult::InvalidPassword));
        errorLabel_->setVisible(true);
        return;
    }
    if (password != confirm){
        markInvalid(confirmInput_, true);
        errorLabel_->setText(tr("两次输入的密码不一致，请检查后重试。"));
        errorLabel_->setVisible(true);
        confirmInput_->selectAll();
        confirmInput_->setFocus();
        return;
    }

    const auto result = userStore_.registerUser(userName, password);
    if (result != smartpark_ui::UserStore::RegisterResult::Success){
        markInvalid(result == smartpark_ui::UserStore::RegisterResult::DuplicateUser
                        ? userNameInput_ : passwordInput_,
                    true);
        errorLabel_->setText(smartpark_ui::UserStore::registerErrorText(result));
        errorLabel_->setVisible(true);
        return;
    }
    accept();
}
