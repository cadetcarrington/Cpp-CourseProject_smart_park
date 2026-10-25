#pragma once
#include <QSqlDatabase>
#include <QString>
#include <optional>

namespace smartpark{

// 账号存储：SQLite users 表 + 盐化迭代 SHA-256 口令摘要。
// 服务端/测试场景默认在空库播种演示账号 admin（密码 smartpark），与登录
// 界面的演示提示保持一致；macOS 管理端的本地库用 NoSeed 打开——空库首启
// 由注册页创建第一个管理员账号（见 LoginViewController 首启流程）。
// GUI / TCP / REST 网关共用本类做登录验证、角色判定与服务端 token 签发。
class UserStore{
public:
    enum class SeedPolicy{
        Seed,    // 空库播种 admin / gate / user 演示账号（服务端、测试）
        NoSeed   // 空库保持无账号（macOS 管理端本地库）
    };

    explicit UserStore(const QString &databasePath,
                       SeedPolicy seedPolicy = SeedPolicy::Seed);
    ~UserStore();

    UserStore(const UserStore &) = delete;
    UserStore &operator=(const UserStore &) = delete;

    enum class LoginResult{
        Success,
        EmptyFields,
        UnknownUser,
        WrongPassword,
        StorageError,
        Locked
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
    // role：admin / gate / user。REST 网关（H5 用户注册）用默认的 user；
    // macOS 管理端本地库的首个账号传 admin。
    RegisterResult registerUser(const QString &userName, const QString &password,
                                const QString &role = QString::fromLatin1("user"));
    // users 表当前账号数；查询失败返回 -1。登录界面据此判断是否首启。
    int userCount() const;

    // ---- 服务端 token（docs/rest-api.md §2）----
    // 签发明文仅返回一次，库中 auth_tokens 只存 SHA-256 摘要；
    // 过期与吊销在 verifyToken 时判定。会话与连接解耦，供 REST 多端复用。
    struct TokenIdentity{
        QString username;
        QString role;
    };
    QString issueToken(const QString &userName, qint64 ttlMs);
    std::optional<TokenIdentity> verifyToken(const QString &token) const;
    bool revokeToken(const QString &token);
    bool revokeUserTokens(const QString &userName);

    // 角色：admin / gate / user（历史库由迁移语句按种子账号名归一）。
    QString roleOf(const QString &userName) const;
    // 账号级连续失败锁定（5 次锁 10 分钟）剩余毫秒数；未锁定返回 0。
    qint64 lockedRemainderMs(const QString &userName) const;

    static QString loginErrorText(LoginResult result);
    static QString registerErrorText(RegisterResult result);
    // 账号：2-24 个字符，不含空白；密码：6-64 个字符。
    static bool isValidUserName(const QString &userName);
    static bool isValidPassword(const QString &password);

    const QString &lastError() const noexcept;

private:
    void createSchema();
    void migrateSchema();
    void ensureSeedAccount();
    bool userExists(const QString &userName);
    // 摘要格式：base64(salt) + ':' + base64(迭代 SHA-256)。
    QString hashPassword(const QString &password, const QByteArray &salt) const;
    bool verifyDigest(const QString &password, const QString &storedDigest) const;
    bool touchLastLogin(const QString &userName);
    void noteLoginFailure(const QString &userName);
    void clearLoginFailures(const QString &userName);

    QSqlDatabase database_;
    QString connectionName_;
    QString lastError_;
};

} // namespace smartpark
