#pragma once
#include <QSqlDatabase>
#include <QString>

namespace smartpark{

// 管理端账号存储：SQLite users 表 + 盐化迭代 SHA-256 口令摘要。
// 登录与注册共用与停车数据相同的数据库文件；空库首次打开时自动播种
// 演示账号 admin（密码 smartpark），与登录界面的演示提示保持一致。
// GUI 与 TCP 服务端共用本类做登录验证；后续接入服务端账号体系时
// 以本类为迁移起点。曾位于 apps/admin，因服务端复用迁入 core。
class UserStore{
public:
    explicit UserStore(const QString &databasePath);
    ~UserStore();

    UserStore(const UserStore &) = delete;
    UserStore &operator=(const UserStore &) = delete;

    enum class LoginResult{
        Success,
        EmptyFields,
        UnknownUser,
        WrongPassword,
        StorageError
    };
    enum class RegisterResult{
        Success,
        EmptyFields,
        InvalidUserName,
        InvalidPassword,
        DuplicateUser,
        StorageError
    };

    LoginResult verifyLogin(const QString &userName, const QString &password);
    RegisterResult registerUser(const QString &userName, const QString &password);

    static QString loginErrorText(LoginResult result);
    static QString registerErrorText(RegisterResult result);
    // 账号：2-24 个字符，不含空白；密码：6-64 个字符。
    static bool isValidUserName(const QString &userName);
    static bool isValidPassword(const QString &password);

    const QString &lastError() const noexcept;

private:
    void createSchema();
    void ensureSeedAccount();
    bool userExists(const QString &userName);
    // 摘要格式：base64(salt) + ':' + base64(迭代 SHA-256)。
    QString hashPassword(const QString &password, const QByteArray &salt) const;
    bool verifyDigest(const QString &password, const QString &storedDigest) const;
    bool touchLastLogin(const QString &userName);

    QSqlDatabase database_;
    QString connectionName_;
    QString lastError_;
};

} // namespace smartpark
