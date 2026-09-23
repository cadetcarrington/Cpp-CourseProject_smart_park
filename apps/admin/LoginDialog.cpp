#include "LoginDialog.h"

#include <QCheckBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace{
constexpr auto kDemoUserName = "admin";
constexpr auto kDemoPassword = "smartpark";
}

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent){
    setWindowTitle(tr("登录 SmartPark"));
    setModal(true);
    setFixedSize(460, 530);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(34, 34, 34, 30);
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

    auto *subtitle = new QLabel(tr("停车场运营管理平台"), this);
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
    root->addSpacing(34);

    auto *card = new QFrame(this);
    card->setObjectName("loginCard");
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 24, 24, 22);
    cardLayout->setSpacing(14);

    auto *cardTitle = new QLabel(tr("管理员登录"), card);
    cardTitle->setObjectName("loginCardTitle");
    QFont cardTitleFont = cardTitle->font();
    cardTitleFont.setPointSize(15);
    cardTitleFont.setBold(true);
    cardTitle->setFont(cardTitleFont);
    cardLayout->addWidget(cardTitle);

    auto *instruction = new QLabel(
        tr("使用管理员账号进入停车场实时运营控制台。"), card);
    instruction->setObjectName("loginInstruction");
    instruction->setWordWrap(true);
    cardLayout->addWidget(instruction);
    cardLayout->addSpacing(4);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);

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

    form->addRow(tr("账号"), userNameInput_);
    form->addRow(tr("密码"), passwordInput_);
    cardLayout->addLayout(form);

    rememberUserCheck_ = new QCheckBox(tr("记住账号"), card);
    rememberUserCheck_->setObjectName("rememberUserCheck");
    cardLayout->addWidget(rememberUserCheck_);

    errorLabel_ = new QLabel(card);
    errorLabel_->setObjectName("loginErrorLabel");
    errorLabel_->setWordWrap(true);
    errorLabel_->setVisible(false);
    cardLayout->addWidget(errorLabel_);

    loginButton_ = new QPushButton(tr("登录管理控制台"), card);
    loginButton_->setObjectName("loginButton");
    loginButton_->setDefault(true);
    loginButton_->setAutoDefault(true);
    loginButton_->setMinimumHeight(38);
    cardLayout->addWidget(loginButton_);

    auto *demoHint = new QLabel(
        tr("演示账号：admin　密码：smartpark\n"
           "此版本仅提供本地演示认证，后续可接入服务端账号体系。"), card);
    demoHint->setObjectName("demoAccountHint");
    demoHint->setWordWrap(true);
    cardLayout->addWidget(demoHint);

    root->addWidget(card);
    root->addStretch(1);

    auto *footer = new QLabel(tr("SmartPark · 本地运营终端"), this);
    footer->setObjectName("loginFooter");
    footer->setAlignment(Qt::AlignCenter);
    root->addWidget(footer);

    QSettings settings;
    const bool rememberUser = settings.value(QStringLiteral("Session/rememberUser"), false).toBool();
    rememberUserCheck_->setChecked(rememberUser);
    if (rememberUser){
        userNameInput_->setText(settings.value(QStringLiteral("Session/lastUser")).toString());
        passwordInput_->setFocus();
    } else{
        userNameInput_->setFocus();
    }

    connect(loginButton_, &QPushButton::clicked, this, &LoginDialog::attemptLogin);
    connect(passwordInput_, &QLineEdit::returnPressed, this, &LoginDialog::attemptLogin);

    setStyleSheet(QStringLiteral(R"(
        LoginDialog { background: #F4F6F8; }
        QLabel#loginBrandMark {
            color: #FFFFFF; background: #1D4E89; border-radius: 12px;
            font-size: 17pt; font-weight: 700;
        }
        QLabel#loginTitle { color: #102A43; }
        QLabel#loginSubtitle, QLabel#loginInstruction, QLabel#loginFooter {
            color: #52606D; font-size: 10pt;
        }
        QFrame#loginCard {
            background: #FFFFFF; border: 1px solid #D9E2EC; border-radius: 12px;
        }
        QLabel#loginCardTitle { color: #102A43; }
        QLineEdit {
            background: #FFFFFF; border: 1px solid #BCCCDC; border-radius: 6px;
            min-height: 30px; padding: 0 9px; color: #102A43;
        }
        QLineEdit:focus { border: 2px solid #1D4E89; padding: 0 8px; }
        QCheckBox { color: #334E68; }
        QLabel#loginErrorLabel { color: #B42318; font-weight: 600; }
        QPushButton#loginButton {
            color: #FFFFFF; background: #1D4E89; border: 1px solid #1D4E89;
            border-radius: 6px; font-weight: 700; padding: 0 14px;
        }
        QPushButton#loginButton:hover { background: #163E6D; }
        QPushButton#loginButton:focus { border: 2px solid #102A43; }
        QLabel#demoAccountHint {
            color: #52606D; background: #F0F4F8; border-radius: 6px;
            padding: 9px; font-size: 9pt;
        }
    )"));
}

QString LoginDialog::userName() const{
    return userNameInput_->text().trimmed();
}

void LoginDialog::attemptLogin(){
    const QString userName = userNameInput_->text().trimmed();
    const QString password = passwordInput_->text();
    if (userName == QString::fromLatin1(kDemoUserName)
        && password == QString::fromLatin1(kDemoPassword)){
        QSettings settings;
        settings.setValue(QStringLiteral("Session/rememberUser"), rememberUserCheck_->isChecked());
        if (rememberUserCheck_->isChecked()){
            settings.setValue(QStringLiteral("Session/lastUser"), userName);
        } else{
            settings.remove(QStringLiteral("Session/lastUser"));
        }
        accept();
        return;
    }

    errorLabel_->setText(tr("账号或密码不正确，请检查后重试。"));
    errorLabel_->setVisible(true);
    passwordInput_->selectAll();
    passwordInput_->setFocus();
}
