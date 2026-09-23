#include "MainWindow.h"

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


QTEST_MAIN(AdminMainWindowTests)
#include "admin_main_window_tests.moc"
