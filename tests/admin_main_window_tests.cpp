#include "MainWindow.h"
#include "ChartWidgets.h"
#include "PlateReviewDialog.h"
#include "LoginDialog.h"
#include "Theme.h"
#include "core/service/UserStore.h"
#include "core/persistence/Persistence.h"
#include "core/service/ParkingService.h"

#include <QtTest/qtest.h>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDir>
#include <QFileDialog>
#include <QLineEdit>
#include <QImage>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QLabel>

namespace{
int rowForPlate(QTableWidget *table, const QString &plate){
    // 当前车位表的车牌在第 3 列；停车记录表的车牌在第 0 列。
    const int plateColumn = table->objectName() == QStringLiteral("recordsTable") ? 0 : 3;
    for (int row = 0; row < table->rowCount(); ++row){
        const auto *item = table->item(row, plateColumn);
        const auto *label = qobject_cast<QLabel *>(table->cellWidget(row, plateColumn));
        if ((item && item->text() == plate) || (label && label->text() == plate)){
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
    void tableRowsUseReadableTextInBothThemes();
    void dropdownsAndCalendarEditorsStayReadable();
    void donutChartsKeepLegendClear();
    void localRevenueUsesExitDateAndFractionalFees();
    void seedsDemoAccountAndVerifiesLogin();
    void registersUsersWithValidation();
    void loginDialogValidatesAndAuthenticates();
    void exposesOptionalImageRecognition();
    void headerRecognitionSelectsAndAppliesPlate();
    void reviewNeedsExplicitAcceptance();
    void reviewRejectsMalformedInference();
    void reviewAppliesOnlyAcceptedResult();
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

void AdminMainWindowTests::tableRowsUseReadableTextInBothThemes(){
    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    MainWindow window(databaseDir.filePath("admin-table-colors.db"));
    auto *plateInput = window.findChild<QLineEdit *>("plateInput");
    auto *allocateButton = window.findChild<QPushButton *>("allocateButton");
    auto *occupancy = window.findChild<QTableWidget *>("occupancyTable");
    auto *records = window.findChild<QTableWidget *>("recordsTable");
    QVERIFY(plateInput && allocateButton && occupancy && records);
    plateInput->setText(QStringLiteral("京B12345"));
    allocateButton->click();
    QVERIFY(occupancy->rowCount() > 1);
    QCOMPARE(records->rowCount(), 1);
    QVERIFY(occupancy->item(0, 0) != nullptr);
    QVERIFY(qobject_cast<QLabel *>(records->cellWidget(0, 0)) != nullptr);

    for (const QString &style : {theme::glassMainWindowStyleSheet(),
                                 theme::solidMainWindowStyleSheet()}){
        window.setStyleSheet(style);
        window.show();
        for (QTableWidget *table : {occupancy, records}){
            table->clearSelection();
            QCOMPARE(table->palette().color(QPalette::Text), QColor(theme::Text1));
            QCOMPARE(table->palette().color(QPalette::HighlightedText), QColor(theme::Text1));
            if (table == records){
                auto *plateLabel = qobject_cast<QLabel *>(table->cellWidget(0, 0));
                QVERIFY(plateLabel);
                QCOMPARE(plateLabel->palette().color(QPalette::WindowText),
                         QColor(theme::Text1));
            } else{
                QCOMPARE(table->item(0, 0)->foreground().style(), Qt::NoBrush);
            }
        }
    }
}

void AdminMainWindowTests::dropdownsAndCalendarEditorsStayReadable(){
    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    MainWindow window(databaseDir.filePath("admin-dropdowns.db"));
    auto *navigation = window.findChild<QListWidget *>("sideNavigation");
    QVERIFY(navigation);
    window.show();

    for (int page : {2, 4}){
        navigation->setCurrentRow(page);
        for (const char *name : page == 2
                 ? std::initializer_list<const char *>{"vehicleTypeInput", "strategyInput"}
                 : std::initializer_list<const char *>{"bookingVehicleTypeInput"}){
            auto *combo = window.findChild<QComboBox *>(name);
            QVERIFY(combo);
            QVERIFY(!combo->currentText().isEmpty());
            if (page == 2){
                QVERIFY(combo->width() >= 220);
                QCOMPARE(combo->sizePolicy().horizontalPolicy(), QSizePolicy::Expanding);
            }
            QCOMPARE(combo->palette().color(QPalette::Text), QColor(theme::Text1));
            QVERIFY(!combo->styleSheet().contains(QStringLiteral("color: transparent")));
        }
    }

    for (const char *name : {"arrivalInput", "recordFromInput", "recordToInput"}){
        auto *editor = window.findChild<QDateTimeEdit *>(name);
        QVERIFY(editor);
        QVERIFY(editor->calendarPopup());
        QCOMPARE(editor->palette().color(QPalette::Text), QColor(theme::Text1));
        QVERIFY(editor->minimumWidth() >= 190);
    }
}

void AdminMainWindowTests::donutChartsKeepLegendClear(){
    for (const QSize size : {QSize(240, 190), QSize(320, 230)}){
        DonutChartWidget chart;
        chart.resize(size);
        chart.setSlices({{QStringLiteral("普通"), 42, QColor(220, 25, 40)},
                         {QStringLiteral("充电"), 15, QColor(220, 25, 40)},
                         {QStringLiteral("无障碍"), 8, QColor(220, 25, 40)},
                         {QStringLiteral("VIP"), 10, QColor(220, 25, 40)}});
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        chart.render(&image);

        const int legendTop = size.height() - 8 - (4 * 20 + 6) + 4;
        int firstRingRow = size.height();
        int lastRingRow = -1;
        int firstLegendRow = size.height();
        for (int y = 0; y < size.height(); ++y){
            for (int x = 0; x < size.width(); ++x){
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.red() < 180 || pixel.red() > 240 ||
                    pixel.green() > 100 || pixel.blue() > 100){
                    continue;
                }
                if (x > size.width() / 4 && x < size.width() * 3 / 4 &&
                    y < legendTop){
                    firstRingRow = std::min(firstRingRow, y);
                    lastRingRow = std::max(lastRingRow, y);
                } else if (x >= 8 && x <= 20 && y >= size.height() / 2){
                    firstLegendRow = std::min(firstLegendRow, y);
                }
            }
        }
        QVERIFY(firstRingRow >= 16);
        QVERIFY(lastRingRow > firstRingRow);
        QVERIFY(firstLegendRow < size.height());
        QVERIFY(firstLegendRow - lastRingRow >= 8);
    }
}

void AdminMainWindowTests::localRevenueUsesExitDateAndFractionalFees(){
    using namespace std::chrono_literals;
    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    const QString path = databaseDir.filePath("revenue.db");
    const QDate today = QDate::currentDate();
    const auto atNoon = [](QDate date){
        return smartpark::ParkingRecord::TimePoint{}
            + std::chrono::milliseconds(QDateTime(date, QTime(12, 0))
                                            .toMSecsSinceEpoch());
    };
    smartpark::BillingRule rule;
    rule.minimumFee = 0.4;
    rule.unitFee = 0.4;
    {
        smartpark::Persistence persistence(path);
        const auto layout = smartpark::ParkingLayout::garageLayout();
        QVERIFY(persistence.repository().saveLayout(layout));
        smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                          &persistence.repository(), rule);
        const auto firstExit = atNoon(today.addDays(-6));
        QVERIFY(service.enter({u8"晋A10001", smartpark::VehicleType::Car},
                              firstExit - 30min - 1ms).has_value());
        const auto first = service.leave(u8"晋A10001", firstExit);
        QVERIFY(first && std::abs(first->fee() - 0.4) < 1e-9);
        const auto lastExit = atNoon(today);
        QVERIFY(service.enter({u8"晋A10002", smartpark::VehicleType::Car},
                              lastExit - 60min).has_value());
        const auto last = service.leave(u8"晋A10002", lastExit);
        QVERIFY(last && std::abs(last->fee() - 0.4) < 1e-9);
    }
    MainWindow window(path);
    auto *chart = dynamic_cast<LineChartWidget *>(
        window.findChild<QWidget *>("sevenDayRevenueChart"));
    QVERIFY(chart);
    QCOMPARE(chart->series().size(), 1);
    const auto &points = chart->series().first().points;
    QCOMPARE(points.size(), 7);
    QCOMPARE(points.first().label, today.addDays(-6).toString(QStringLiteral("MM-dd")));
    QCOMPARE(points.last().label, today.toString(QStringLiteral("MM-dd")));
    QVERIFY(std::abs(points.first().value - 0.4) < 1e-9);
    QVERIFY(std::abs(points.last().value - 0.4) < 1e-9);
    for (int i = 1; i < 6; ++i){
        QCOMPARE(points.at(i).value, 0.0);
    }
}

void AdminMainWindowTests::seedsDemoAccountAndVerifiesLogin(){
    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    {
        smartpark::UserStore store(databaseDir.filePath("users.db"));
        QVERIFY(store.lastError().isEmpty());
        // 空库自动播种演示账号，登录界面的提示与实际凭据保持一致。
        QCOMPARE(store.verifyLogin(QStringLiteral("admin"), QStringLiteral("smartpark")),
                 smartpark::UserStore::LoginResult::Success);
        QCOMPARE(store.verifyLogin(QStringLiteral("admin"), QStringLiteral("wrong")),
                 smartpark::UserStore::LoginResult::WrongPassword);
        QCOMPARE(store.verifyLogin(QStringLiteral("ghost"), QStringLiteral("smartpark")),
                 smartpark::UserStore::LoginResult::UnknownUser);
        QCOMPARE(store.verifyLogin(QString(), QStringLiteral("x")),
                 smartpark::UserStore::LoginResult::EmptyFields);
        QCOMPARE(store.verifyLogin(QStringLiteral("admin"), QString()),
                 smartpark::UserStore::LoginResult::EmptyFields);
    }
    {
        // 重开连接后凭据仍然有效（持久化）。
        smartpark::UserStore store(databaseDir.filePath("users.db"));
        QVERIFY(store.lastError().isEmpty());
        QCOMPARE(store.verifyLogin(QStringLiteral("admin"), QStringLiteral("smartpark")),
                 smartpark::UserStore::LoginResult::Success);
    }
}

void AdminMainWindowTests::registersUsersWithValidation(){
    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    smartpark::UserStore store(databaseDir.filePath("users.db"));
    QVERIFY(store.lastError().isEmpty());

    QCOMPARE(store.registerUser(QStringLiteral("op01"), QStringLiteral("secret123")),
             smartpark::UserStore::RegisterResult::Success);
    QCOMPARE(store.verifyLogin(QStringLiteral("op01"), QStringLiteral("secret123")),
             smartpark::UserStore::LoginResult::Success);
    QCOMPARE(store.verifyLogin(QStringLiteral("op01"), QStringLiteral("secret124")),
             smartpark::UserStore::LoginResult::WrongPassword);

    // 重复账号、非法输入被拒绝且不影响已有数据。
    QCOMPARE(store.registerUser(QStringLiteral("op01"), QStringLiteral("secret123")),
             smartpark::UserStore::RegisterResult::DuplicateUser);
    QCOMPARE(store.registerUser(QStringLiteral("a"), QStringLiteral("secret123")),
             smartpark::UserStore::RegisterResult::InvalidUserName);
    QCOMPARE(store.registerUser(QStringLiteral("has space"), QStringLiteral("secret123")),
             smartpark::UserStore::RegisterResult::InvalidUserName);
    QCOMPARE(store.registerUser(QStringLiteral("op02"), QStringLiteral("12345")),
             smartpark::UserStore::RegisterResult::InvalidPassword);
    QCOMPARE(store.registerUser(QString(), QStringLiteral("secret123")),
             smartpark::UserStore::RegisterResult::EmptyFields);
    QCOMPARE(store.verifyLogin(QStringLiteral("op02"), QStringLiteral("12345")),
             smartpark::UserStore::LoginResult::UnknownUser);

    // 注册后的账号与种子账号互不影响；盐化摘要让同密码产生不同存储。
    QCOMPARE(store.registerUser(QStringLiteral("op03"), QStringLiteral("smartpark")),
             smartpark::UserStore::RegisterResult::Success);
    QCOMPARE(store.verifyLogin(QStringLiteral("op03"), QStringLiteral("smartpark")),
             smartpark::UserStore::LoginResult::Success);
    QVERIFY(smartpark::UserStore::isValidUserName(QStringLiteral("管理员01")));
    QVERIFY(!smartpark::UserStore::isValidUserName(QStringLiteral(" bad")));
}

void AdminMainWindowTests::loginDialogValidatesAndAuthenticates(){
    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    smartpark::UserStore store(databaseDir.filePath("users.db"));
    QVERIFY(store.lastError().isEmpty());
    LoginDialog dialog(&store);
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
void AdminMainWindowTests::exposesOptionalImageRecognition(){
    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    MainWindow window(databaseDir.filePath("admin-image-recognition.db"));
    auto *plateInput = window.findChild<QLineEdit *>("plateInput");
    auto *recognizeButton = window.findChild<QPushButton *>("recognizePlateButton");
    auto *headerButton = window.findChild<QPushButton *>("headerRecognitionButton");
    QVERIFY(plateInput && recognizeButton && headerButton);
    QVERIFY(recognizeButton->isEnabled());
    QVERIFY(headerButton->isEnabled());
    QCOMPARE(headerButton->text(), QStringLiteral("识别车牌"));
    QVERIFY(QDir(QStringLiteral(SMARTPARK_PLATE_EXAMPLES_PATH)).exists());
    QVERIFY(plateInput->text().isEmpty());
}

void AdminMainWindowTests::headerRecognitionSelectsAndAppliesPlate(){
    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    MainWindow window(databaseDir.filePath("admin-header-recognition.db"));
    auto *headerButton = window.findChild<QPushButton *>("headerRecognitionButton");
    auto *plateInput = window.findChild<QLineEdit *>("plateInput");
    auto *navigation = window.findChild<QListWidget *>("sideNavigation");
    auto *pages = window.findChild<QStackedWidget *>("contentPages");
    QVERIFY(headerButton && plateInput && navigation && pages);
    window.show();
    QVERIFY(headerButton->isVisible());
    QVERIFY(headerButton->isEnabled());
    QCOMPARE(pages->currentIndex(), 0);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);

    QImage image(120, 60, QImage::Format_RGB32);
    image.fill(Qt::white);
    const QString imagePath = databaseDir.filePath("plate.png");
    QVERIFY(image.save(imagePath));
    QTimer::singleShot(0, &window, [&]{
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(dialog->directory().absolutePath(),
                 QDir(QStringLiteral(SMARTPARK_PLATE_EXAMPLES_PATH)).absolutePath());
        dialog->selectFile(imagePath);
        QCOMPARE(dialog->selectedFiles().value(0), imagePath);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTimer::singleShot(0, &window, [&]{
            auto *review = qobject_cast<PlateReviewDialog *>(QApplication::activeModalWidget());
            QVERIFY(review);
            QTimer::singleShot(0, review, [review]{
                QVERIFY(QMetaObject::invokeMethod(review, "showResult", Qt::DirectConnection,
                    Q_ARG(QByteArray, QByteArray(
                        "{\"plate\":\"京A12345\",\"detection_confidence\":0.9,"
                        "\"recognition_confidence\":0.95,\"valid\":true,"
                        "\"bounding_box\":[10,10,90,40]}"))));
                auto *use = review->findChild<QPushButton *>("useRecognizedPlate");
                QVERIFY(use && use->isEnabled());
                use->click();
            });
        });
    });
    headerButton->click();
    QCOMPARE(plateInput->text(), QStringLiteral("京A12345"));
    QCOMPARE(navigation->currentRow(), 2);
    QCOMPARE(pages->currentIndex(), 2);
}

void AdminMainWindowTests::reviewNeedsExplicitAcceptance(){
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = directory.filePath("plate.png");
    QImage image(120, 60, QImage::Format_RGB32);
    image.fill(Qt::white);
    QVERIFY(image.save(imagePath));
    PlateReviewDialog dialog(imagePath);
    auto *candidate = dialog.findChild<QLineEdit *>("recognizedPlate");
    auto *use = dialog.findChild<QPushButton *>("useRecognizedPlate");
    auto *preview = dialog.findChild<QLabel *>("platePreview");
    QVERIFY(candidate && use && preview);
    QVERIFY(!preview->pixmap().isNull());
    QVERIFY(!use->isEnabled());
    candidate->setText(QStringLiteral("京A12345"));
    QVERIFY(!use->isEnabled());
    dialog.reject();
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
}

void AdminMainWindowTests::reviewRejectsMalformedInference(){
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = directory.filePath("plate.png");
    QImage image(120, 60, QImage::Format_RGB32);
    image.fill(Qt::white);
    QVERIFY(image.save(imagePath));
    PlateReviewDialog dialog(imagePath);
    auto *use = dialog.findChild<QPushButton *>("useRecognizedPlate");
    auto *status = dialog.findChild<QLabel *>("recognitionStatus");
    QVERIFY(use && status);
    QVERIFY(QMetaObject::invokeMethod(&dialog, "showResult", Qt::DirectConnection,
                                      Q_ARG(QByteArray, QByteArray("{\"plate\":\"京A12345\"}"))));
    QVERIFY(!use->isEnabled());
    QVERIFY(status->text().contains(QStringLiteral("无效")));
}

void AdminMainWindowTests::reviewAppliesOnlyAcceptedResult(){
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = directory.filePath("plate.png");
    QImage image(120, 60, QImage::Format_RGB32);
    image.fill(Qt::white);
    QVERIFY(image.save(imagePath));
    PlateReviewDialog dialog(imagePath);
    auto *use = dialog.findChild<QPushButton *>("useRecognizedPlate");
    auto *candidate = dialog.findChild<QLineEdit *>("recognizedPlate");
    auto *crop = dialog.findChild<QLabel *>("plateCrop");
    QVERIFY(use && candidate && crop);
    QVERIFY(QMetaObject::invokeMethod(&dialog, "showResult", Qt::DirectConnection,
        Q_ARG(QByteArray, QByteArray(
            "{\"plate\":\"京A12345\",\"detection_confidence\":0.9,"
            "\"recognition_confidence\":0.95,\"valid\":true,"
            "\"bounding_box\":[10,10,90,40]}"))));
    QCOMPARE(candidate->text(), QStringLiteral("京A12345"));
    QVERIFY(!crop->pixmap().isNull());
    QVERIFY(use->isEnabled());
    QCOMPARE(dialog.result(), 0);
    use->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(dialog.plate(), QStringLiteral("京A12345"));
}

#include "admin_main_window_tests.moc"
