#include "UserStore.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QCryptographicHash>

namespace smartpark_ui{
namespace{
constexpr int kMinUserNameLength = 2;
constexpr int kMaxUserNameLength = 24;
constexpr int kMinPasswordLength = 6;
constexpr int kMaxPasswordLength = 64;
constexpr int kDigestIterations = 12000;
constexpr auto kSeedUserName = "admin";
constexpr auto kSeedPassword = "smartpark";
}

UserStore::UserStore(const QString &databasePath){
    connectionName_ = QStringLiteral("smartpark-users-%1").arg(
        QString::number(reinterpret_cast<quintptr>(this), 16));
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath);
    if (!database_.open()){
        lastError_ = database_.lastError().text();
        database_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName_);
        connectionName_.clear();
        return;
    }
    createSchema();
    if (lastError_.isEmpty()){
        ensureSeedAccount();
    }
}

UserStore::~UserStore(){
    if (database_.isValid()){
        database_.close();
        database_ = QSqlDatabase();
    }
    if (!connectionName_.isEmpty()){
        QSqlDatabase::removeDatabase(connectionName_);
        connectionName_.clear();
    }
}

void UserStore::createSchema(){
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS users ("
            "username TEXT PRIMARY KEY,"
            "salt TEXT NOT NULL,"
            "password_hash TEXT NOT NULL,"
            "created_at_ms INTEGER NOT NULL,"
            "last_login_ms INTEGER)"))){
        lastError_ = query.lastError().text();
    }
}

void UserStore::ensureSeedAccount(){
    QSqlQuery countQuery(database_);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM users"))){
        lastError_ = countQuery.lastError().text();
        return;
    }
    if (!countQuery.next() || countQuery.value(0).toInt() > 0){
        return;
    }
    QByteArray salt(16, 0);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(salt.data()), 4);
    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT INTO users(username, salt, password_hash, created_at_ms)"
        " VALUES(:userName, :salt, :digest, :createdAt)"));
    insert.bindValue(QStringLiteral(":userName"), QString::fromLatin1(kSeedUserName));
    insert.bindValue(QStringLiteral(":salt"), QString::fromLatin1(salt.toBase64()));
    insert.bindValue(QStringLiteral(":digest"),
                     hashPassword(QString::fromLatin1(kSeedPassword), salt));
    insert.bindValue(QStringLiteral(":createdAt"),
                     QDateTime::currentMSecsSinceEpoch());
    if (!insert.exec()){
        lastError_ = insert.lastError().text();
    }
}

bool UserStore::userExists(const QString &userName){
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT 1 FROM users WHERE username = :userName"));
    query.bindValue(QStringLiteral(":userName"), userName);
    if (!query.exec()){
        lastError_ = query.lastError().text();
        return false;
    }
    return query.next();
}

QString UserStore::hashPassword(const QString &password, const QByteArray &salt) const{
    QByteArray digest = salt + password.toUtf8();
    for (int index = 0; index < kDigestIterations; ++index){
        digest = QCryptographicHash::hash(digest, QCryptographicHash::Sha256);
    }
    return QString::fromLatin1(digest.toBase64());
}

bool UserStore::verifyDigest(const QString &password, const QString &storedDigest) const{
    const int separator = storedDigest.indexOf(QLatin1Char(':'));
    if (separator < 0){
        return false;
    }
    const QByteArray salt = QByteArray::fromBase64(
        storedDigest.left(separator).toLatin1());
    if (salt.isEmpty()){
        return false;
    }
    // 常量时间比较，防时序侧信道。
    const QByteArray actual = hashPassword(password, salt).toLatin1();
    const QByteArray expected = storedDigest.mid(separator + 1).toLatin1();
    if (expected.size() != actual.size() || expected.isEmpty()){
        return false;
    }
    char difference = 0;
    for (int index = 0; index < actual.size(); ++index){
        difference |= static_cast<char>(actual[index] ^ expected[index]);
    }
    return difference == 0;
}

UserStore::LoginResult UserStore::verifyLogin(const QString &userName,
                                              const QString &password){
    if (userName.trimmed().isEmpty() || password.isEmpty()){
        return LoginResult::EmptyFields;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT salt, password_hash FROM users WHERE username = :userName"));
    query.bindValue(QStringLiteral(":userName"), userName.trimmed());
    if (!query.exec() || !query.next()){
        return LoginResult::UnknownUser;
    }
    const QString storedDigest = QStringLiteral("%1:%2")
                                     .arg(query.value(0).toString(),
                                          query.value(1).toString());
    if (!verifyDigest(password, storedDigest)){
        return LoginResult::WrongPassword;
    }
    if (!touchLastLogin(userName.trimmed())){
        return LoginResult::StorageError;
    }
    return LoginResult::Success;
}

bool UserStore::touchLastLogin(const QString &userName){
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE users SET last_login_ms = :lastLogin WHERE username = :userName"));
    query.bindValue(QStringLiteral(":lastLogin"), QDateTime::currentMSecsSinceEpoch());
    query.bindValue(QStringLiteral(":userName"), userName);
    if (!query.exec()){
        lastError_ = query.lastError().text();
        return false;
    }
    return true;
}

UserStore::RegisterResult UserStore::registerUser(const QString &userName,
                                                  const QString &password){
    const QString normalized = userName.trimmed();
    if (normalized.isEmpty() || password.isEmpty()){
        return RegisterResult::EmptyFields;
    }
    if (!isValidUserName(normalized)){
        return RegisterResult::InvalidUserName;
    }
    if (!isValidPassword(password)){
        return RegisterResult::InvalidPassword;
    }
    if (userExists(normalized)){
        return RegisterResult::DuplicateUser;
    }
    if (!lastError_.isEmpty()){
        return RegisterResult::StorageError;
    }
    QByteArray salt(16, 0);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(salt.data()), 4);
    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT INTO users(username, salt, password_hash, created_at_ms)"
        " VALUES(:userName, :salt, :digest, :createdAt)"));
    insert.bindValue(QStringLiteral(":userName"), normalized);
    insert.bindValue(QStringLiteral(":salt"), QString::fromLatin1(salt.toBase64()));
    insert.bindValue(QStringLiteral(":digest"), hashPassword(password, salt));
    insert.bindValue(QStringLiteral(":createdAt"), QDateTime::currentMSecsSinceEpoch());
    if (!insert.exec()){
        lastError_ = insert.lastError().text();
        return RegisterResult::StorageError;
    }
    return RegisterResult::Success;
}

QString UserStore::loginErrorText(LoginResult result){
    switch (result){
    case LoginResult::Success:
        return QString();
    case LoginResult::EmptyFields:
        return QStringLiteral("请输入账号和密码。");
    case LoginResult::UnknownUser:
        return QStringLiteral("账号不存在，请先注册新账号。");
    case LoginResult::WrongPassword:
        return QStringLiteral("密码不正确，请检查后重试。");
    case LoginResult::StorageError:
        return QStringLiteral("账号数据读写失败，请检查数据库文件。");
    }
    return QStringLiteral("登录失败，请稍后重试。");
}

QString UserStore::registerErrorText(RegisterResult result){
    switch (result){
    case RegisterResult::Success:
        return QString();
    case RegisterResult::EmptyFields:
        return QStringLiteral("请填写账号和密码。");
    case RegisterResult::InvalidUserName:
        return QStringLiteral("账号需为 2-24 个字符，且不能包含空格。");
    case RegisterResult::InvalidPassword:
        return QStringLiteral("密码需为 6-64 个字符。");
    case RegisterResult::DuplicateUser:
        return QStringLiteral("该账号已被注册，请换一个账号名。");
    case RegisterResult::StorageError:
        return QStringLiteral("注册数据写入失败，请检查数据库文件。");
    }
    return QStringLiteral("注册失败，请稍后重试。");
}

bool UserStore::isValidUserName(const QString &userName){
    if (userName.size() < kMinUserNameLength || userName.size() > kMaxUserNameLength){
        return false;
    }
    if (userName != userName.trimmed()){
        return false;
    }
    static const QRegularExpression whitespace(QStringLiteral("\\s"));
    static const QRegularExpression control(QStringLiteral("[\\x00-\\x1F]"));
    return !whitespace.match(userName).hasMatch()
        && !control.match(userName).hasMatch();
}

bool UserStore::isValidPassword(const QString &password){
    return password.size() >= kMinPasswordLength
        && password.size() <= kMaxPasswordLength;
}

const QString &UserStore::lastError() const noexcept{
    return lastError_;
}

} // namespace smartpark_ui
