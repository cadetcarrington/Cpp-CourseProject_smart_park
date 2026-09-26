#include "MainWindow.h"
#include "LoginDialog.h"
#include "UserStore.h"

#include <QtTest/qtest.h>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QLabel>

namespace{
int rowForPlate(QTableWidget *table, const QString &plate){
    // 当前车位表的车牌在第 3 列；停车记录表的车牌在第 0 列。
    const int plateColumn = table->objectName() == QStringLiteral("recordsTable") ? 0 : 3;
    for (int row = 0; row < table->rowCount(); ++row){
        if (table->item(row, plateColumn) && table->item(row, plateColumn)->text() == plate){
            return row;
        }
    }
    return -1;
}
} // namespace

class AdminMainWindowTests : public QObject{
    Q_OBJECT

private slots:
    void updatesAndPersistsOccupiedVehicleType();
    void releasesVehicleAndKeepsCompletionStatus();
    void releasesSelectedVehicleInsteadOfLastAllocation();
    void seedsDemoAccountAndVerifiesLogin();
    void registersUsersWithValidation();
    void loginDialogValidatesAndAuthenticates();
};

void AdminMainWindowTests::updatesAndPersistsOccupiedVehicleType(){
    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    const QString databasePath = databaseDir.filePath("admin-vehicle-type.db");
    const QString plate = QStringLiteral("京B00001");

    QString spotId;
    {
        MainWindow window(databasePath);
        auto *plateInput = window.findChild<QLineEdit *>("plateInput");
        auto *vehicleTypeInput = window.findChild<QComboBox *>("vehicleTypeInput");
        auto *allocateButton = window.findChild<QPushButton *>("allocateButton");
        auto *updateButton = window.findChild<QPushButton *>("updateVehicleTypeButton");
        auto *occupancyTable = window.findChild<QTableWidget *>("occupancyTable");
        QVERIFY(plateInput && vehicleTypeInput && allocateButton && updateButton);
        QVERIFY(occupancyTable);

        vehicleTypeInput->setCurrentIndex(0);
        plateInput->setText(plate);
        allocateButton->click();



        const int initialRow = rowForPlate(occupancyTable, plate);
        QVERIFY(initialRow >= 0);
        QCOMPARE(occupancyTable->item(initialRow, 4)->text(), QStringLiteral("轿车"));
        spotId = occupancyTable->item(initialRow, 0)->text();
        QVERIFY(!spotId.isEmpty());

        vehicleTypeInput->setCurrentIndex(2);
        updateButton->click();

        QCOMPARE(rowForPlate(occupancyTable, plate), initialRow);
        QCOMPARE(occupancyTable->item(initialRow, 4)->text(), QStringLiteral("卡车"));
    }

    {
        MainWindow window(databasePath);
        auto *occupancyTable = window.findChild<QTableWidget *>("occupancyTable");
        QVERIFY(occupancyTable);
        const int restoredRow = rowForPlate(occupancyTable, plate);
        QVERIFY(restoredRow >= 0);
        QCOMPARE(occupancyTable->item(restoredRow, 0)->text(), spotId);
        QCOMPARE(occupancyTable->item(restoredRow, 4)->text(), QStringLiteral("卡车"));
    }
}


void AdminMainWindowTests::releasesVehicleAndKeepsCompletionStatus(){
    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    const QString databasePath = databaseDir.filePath("admin-release.db");
    const QString plate = QStringLiteral("京B00002");
    {
        MainWindow window(databasePath);
        auto *plateInput = window.findChild<QLineEdit *>("plateInput");
        auto *allocateButton = window.findChild<QPushButton *>("allocateButton");
        QVERIFY(plateInput && allocateButton);
        plateInput->setText(plate);
        allocateButton->click();
    }

    MainWindow window(databasePath);
    auto *plateInput = window.findChild<QLineEdit *>("plateInput");
    auto *releaseButton = window.findChild<QPushButton *>("releaseButton");
    auto *statusLabel = window.findChild<QLabel *>("statusLabel");
    QVERIFY(plateInput && releaseButton && statusLabel);

    plateInput->setText(plate);
    releaseButton->click();

    QVERIFY(statusLabel->text().contains(QStringLiteral("离场完成")));
    QVERIFY(statusLabel->text().contains(QStringLiteral("本次费用")));
}

void AdminMainWindowTests::releasesSelectedVehicleInsteadOfLastAllocation(){
    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    const QString databasePath = databaseDir.filePath("admin-selected-release.db");
    const QString firstPlate = QStringLiteral("京B00003");
    const QString secondPlate = QStringLiteral("京B00004");

    MainWindow window(databasePath);
    auto *plateInput = window.findChild<QLineEdit *>("plateInput");
    auto *allocateButton = window.findChild<QPushButton *>("allocateButton");
    auto *releaseButton = window.findChild<QPushButton *>("releaseButton");
    auto *occupancyTable = window.findChild<QTableWidget *>("occupancyTable");
    auto *recordsTable = window.findChild<QTableWidget *>("recordsTable");
    QVERIFY(plateInput && allocateButton && releaseButton && occupancyTable && recordsTable);

    plateInput->setText(firstPlate);
    allocateButton->click();
    plateInput->setText(secondPlate);
    allocateButton->click();

    const int firstRow = rowForPlate(occupancyTable, firstPlate);
    QVERIFY(firstRow >= 0);
    occupancyTable->selectRow(firstRow);
    plateInput->clear();
    releaseButton->click();

    QVERIFY(rowForPlate(occupancyTable, firstPlate) < 0);
    QVERIFY(rowForPlate(occupancyTable, secondPlate) >= 0);
    QVERIFY(rowForPlate(recordsTable, firstPlate) >= 0);
    QVERIFY(rowForPlate(recordsTable, secondPlate) >= 0);
}

void AdminMainWindowTests::seedsDemoAccountAndVerifiesLogin(){
    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    {
        smartpark_ui::UserStore store(databaseDir.filePath("users.db"));
        QVERIFY(store.lastError().isEmpty());
        // 空库自动播种演示账号，登录界面的提示与实际凭据保持一致。
        QCOMPARE(store.verifyLogin(QStringLiteral("admin"), QStringLiteral("smartpark")),
                 smartpark_ui::UserStore::LoginResult::Success);
        QCOMPARE(store.verifyLogin(QStringLiteral("admin"), QStringLiteral("wrong")),
                 smartpark_ui::UserStore::LoginResult::WrongPassword);
        QCOMPARE(store.verifyLogin(QStringLiteral("ghost"), QStringLiteral("smartpark")),
                 smartpark_ui::UserStore::LoginResult::UnknownUser);
        QCOMPARE(store.verifyLogin(QString(), QStringLiteral("x")),
                 smartpark_ui::UserStore::LoginResult::EmptyFields);
        QCOMPARE(store.verifyLogin(QStringLiteral("admin"), QString()),
                 smartpark_ui::UserStore::LoginResult::EmptyFields);
    }
    {
        // 重开连接后凭据仍然有效（持久化）。
        smartpark_ui::UserStore store(databaseDir.filePath("users.db"));
        QVERIFY(store.lastError().isEmpty());
        QCOMPARE(store.verifyLogin(QStringLiteral("admin"), QStringLiteral("smartpark")),
                 smartpark_ui::UserStore::LoginResult::Success);
    }
}

void AdminMainWindowTests::registersUsersWithValidation(){
    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    smartpark_ui::UserStore store(databaseDir.filePath("users.db"));
    QVERIFY(store.lastError().isEmpty());

    QCOMPARE(store.registerUser(QStringLiteral("op01"), QStringLiteral("secret123")),
             smartpark_ui::UserStore::RegisterResult::Success);
    QCOMPARE(store.verifyLogin(QStringLiteral("op01"), QStringLiteral("secret123")),
             smartpark_ui::UserStore::LoginResult::Success);
    QCOMPARE(store.verifyLogin(QStringLiteral("op01"), QStringLiteral("secret124")),
             smartpark_ui::UserStore::LoginResult::WrongPassword);

    // 重复账号、非法输入被拒绝且不影响已有数据。
    QCOMPARE(store.registerUser(QStringLiteral("op01"), QStringLiteral("secret123")),
             smartpark_ui::UserStore::RegisterResult::DuplicateUser);
    QCOMPARE(store.registerUser(QStringLiteral("a"), QStringLiteral("secret123")),
             smartpark_ui::UserStore::RegisterResult::InvalidUserName);
    QCOMPARE(store.registerUser(QStringLiteral("has space"), QStringLiteral("secret123")),
             smartpark_ui::UserStore::RegisterResult::InvalidUserName);
    QCOMPARE(store.registerUser(QStringLiteral("op02"), QStringLiteral("12345")),
             smartpark_ui::UserStore::RegisterResult::InvalidPassword);
    QCOMPARE(store.registerUser(QString(), QStringLiteral("secret123")),
             smartpark_ui::UserStore::RegisterResult::EmptyFields);
    QCOMPARE(store.verifyLogin(QStringLiteral("op02"), QStringLiteral("12345")),
             smartpark_ui::UserStore::LoginResult::UnknownUser);

    // 注册后的账号与种子账号互不影响；盐化摘要让同密码产生不同存储。
    QCOMPARE(store.registerUser(QStringLiteral("op03"), QStringLiteral("smartpark")),
             smartpark_ui::UserStore::RegisterResult::Success);
    QCOMPARE(store.verifyLogin(QStringLiteral("op03"), QStringLiteral("smartpark")),
             smartpark_ui::UserStore::LoginResult::Success);
    QVERIFY(smartpark_ui::UserStore::isValidUserName(QStringLiteral("管理员01")));
    QVERIFY(!smartpark_ui::UserStore::isValidUserName(QStringLiteral(" bad")));
}

void AdminMainWindowTests::loginDialogValidatesAndAuthenticates(){
    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    smartpark_ui::UserStore store(databaseDir.filePath("users.db"));
    QVERIFY(store.lastError().isEmpty());
    LoginDialog dialog(store);
    auto *userNameInput = dialog.findChild<QLineEdit *>("loginUserNameInput");
    auto *passwordInput = dialog.findChild<QLineEdit *>("loginPasswordInput");
    auto *loginButton = dialog.findChild<QPushButton *>("loginButton");
    auto *registerLink = dialog.findChild<QPushButton *>("registerLink");
    auto *errorLabel = dialog.findChild<QLabel *>("loginErrorLabel");
    QVERIFY(userNameInput && passwordInput && loginButton && registerLink
            && errorLabel);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QVERIFY(!dialog.grab().isNull());
    dialog.grab().save(QStringLiteral("/tmp/login-grab.png"));

    // 空字段逐项提示，不关闭对话框。
    loginButton->click();
    QVERIFY(errorLabel->isVisible() && !errorLabel->text().isEmpty());
    QCOMPARE(dialog.result(), QDialog::Rejected);
    QVERIFY(dialog.findChild<QLineEdit *>("loginUserNameInput")->property("invalid")
                .toBool());

    // 错误密码给出剩余次数提示。
    userNameInput->setText(QStringLiteral("admin"));
    passwordInput->setText(QStringLiteral("wrong-pass"));
    loginButton->click();
    QVERIFY(errorLabel->text().contains(QStringLiteral("4")));
    QCOMPARE(dialog.result(), QDialog::Rejected);

    // 演示账号通过 UserStore 验证后接受并关闭。
    passwordInput->setText(QStringLiteral("smartpark"));
    loginButton->click();
    QCOMPARE(dialog.result(), QDialog::Accepted);
    QCOMPARE(dialog.userName(), QStringLiteral("admin"));
}


QTEST_MAIN(AdminMainWindowTests)
#include "admin_main_window_tests.moc"
