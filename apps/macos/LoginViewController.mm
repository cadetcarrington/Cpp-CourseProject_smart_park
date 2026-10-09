#import "LoginViewController.h"

#include "network/ServerSession.h"
#include <QCoreApplication>
#include <QEventLoop>

#import "RegisterViewController.h"

#include "core/service/UserStore.h"

#import <Security/Security.h>

#include <QString>

#include <memory>

// 与 Qt 版 LoginDialog 一致的失败上限与锁定时长。
static const NSInteger kMaxFailedAttempts = 5;
static const NSInteger kLockdownSeconds = 30;

// NSUserDefaults 键：只保存账号文本与两个开关；口令一律进钥匙串。
static NSString *const kRememberUserKey = @"SmartParkRememberUser";
static NSString *const kRememberPasswordKey = @"SmartParkRememberPassword";
static NSString *const kLastUserKey = @"SmartParkLastUser";

// 钥匙串条目：service 固定为应用标识，account 为账号名。
static NSString *const kKeychainService = @"com.smartpark.admin.macos";

namespace{

NSMutableDictionary *keychainQuery(NSString *account, BOOL wantsData){
    NSMutableDictionary *query = [@{
        (__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: kKeychainService,
    } mutableCopy];
    if (account.length > 0){
        query[(__bridge id)kSecAttrAccount] = account;
    }
    if (wantsData){
        query[(__bridge id)kSecReturnData] = @YES;
        query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
    }
    return query;
}

// 口令写入系统钥匙串（加密存储），而不是明文落到 plist。
BOOL keychainStore(NSString *account, NSString *password){
    if (account.length == 0 || password.length == 0){
        return NO;
    }
    NSData *data = [password dataUsingEncoding:NSUTF8StringEncoding];
    NSMutableDictionary *query = keychainQuery(account, NO);
    const OSStatus updated = SecItemUpdate(
        (__bridge CFDictionaryRef)query,
        (__bridge CFDictionaryRef)@{(__bridge id)kSecValueData: data});
    if (updated == errSecSuccess){
        return YES;
    }
    if (updated != errSecItemNotFound){
        return NO;
    }
    query[(__bridge id)kSecValueData] = data;
    return SecItemAdd((__bridge CFDictionaryRef)query, NULL) == errSecSuccess;
}

NSString *keychainLoad(NSString *account){
    if (account.length == 0){
        return nil;
    }
    CFTypeRef result = NULL;
    const OSStatus status = SecItemCopyMatching(
        (__bridge CFDictionaryRef)keychainQuery(account, YES), &result);
    if (status != errSecSuccess || result == NULL){
        return nil;
    }
    NSData *data = (__bridge_transfer NSData *)result;
    return [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
}

void keychainDelete(NSString *account){
    if (account.length == 0){
        return;
    }
    SecItemDelete((__bridge CFDictionaryRef)keychainQuery(account, NO));
}

} // namespace

@interface LoginViewController () <NSTextFieldDelegate>
@end

// 远程模式的默认服务端。这是本项目当前部署（s1）的地址；换部署不需要改代码，
// 因为填过一次之后会按 NSUserDefaults 记住上次用的地址与端口。
static NSString *const kDefaultRemoteHost = @"10.108.17.55";
static const NSInteger kDefaultRemotePort = 9527;
static NSString *const kRemoteHostDefaultsKey = @"smartpark.remote.host";
static NSString *const kRemotePortDefaultsKey = @"smartpark.remote.port";

@implementation LoginViewController{
    // 远程模式控件（勾选后才显示地址/端口行）。
    NSButton *_remoteCheck;
    NSTextField *_hostField;
    NSTextField *_portField;
    NSStackView *_remoteRow;
    smartpark::UserStore *_userStore;   // 不持有所有权
    NSTextField *_userNameField;
    NSTextField *_passwordField;        // 在 NSSecureTextField / NSTextField 之间切换
    NSStackView *_passwordRow;
    NSButton *_showPasswordCheck;
    NSButton *_rememberCheck;
    NSButton *_rememberPasswordCheck;
    NSTextField *_errorLabel;
    NSButton *_loginButton;
    NSButton *_registerButton;
    NSTimer *_lockdownTimer;
    NSInteger _failedAttempts;
    NSInteger _lockdownRemaining;
    // 启动时从钥匙串回填的账号，用于在换账号登录后清掉旧条目。
    NSString *_restoredAccount;
}

- (instancetype)initWithUserStore:(smartpark::UserStore *)userStore{
    if ((self = [super initWithNibName:nil bundle:nil])){
        _userStore = userStore;
    }
    return self;
}

- (void)dealloc{
    [_lockdownTimer invalidate];
}

#pragma mark - 视图构建

- (void)loadView{
    NSVisualEffectView *root = [[NSVisualEffectView alloc]
        initWithFrame:NSMakeRect(0, 0, 420, 494)];
    // UnderWindowBackground + BehindWindow：模糊窗口后方（桌面）的毛玻璃。
    root.material = NSVisualEffectMaterialUnderWindowBackground;
    root.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    root.state = NSVisualEffectStateActive;

    NSTextField *title = [NSTextField labelWithString:@"智能停车系统"];
    title.font = [NSFont systemFontOfSize:25 weight:NSFontWeightSemibold];
    title.textColor = [NSColor labelColor];

    NSTextField *subtitle = [NSTextField labelWithString:@"智能停车场管理系统"];
    subtitle.font = [NSFont systemFontOfSize:12];
    subtitle.textColor = [NSColor secondaryLabelColor];

    // 登录卡片：在窗口内层再叠一层毛玻璃，形成分层的玻璃卡片质感。
    NSVisualEffectView *card = [[NSVisualEffectView alloc] init];
    card.material = NSVisualEffectMaterialContentBackground;
    card.blendingMode = NSVisualEffectBlendingModeWithinWindow;
    card.state = NSVisualEffectStateActive;
    card.wantsLayer = YES;
    card.layer.cornerRadius = 14.0;
    card.layer.masksToBounds = YES;

    NSTextField *cardTitle = [NSTextField labelWithString:@"管理端登录"];
    cardTitle.font = [NSFont systemFontOfSize:15 weight:NSFontWeightSemibold];
    cardTitle.textColor = [NSColor labelColor];

    NSTextField *cardHint = [NSTextField labelWithString:@"使用管理员账号登录。"];
    cardHint.font = [NSFont systemFontOfSize:11];
    cardHint.textColor = [NSColor secondaryLabelColor];

    _userNameField = [[NSTextField alloc] init];
    _userNameField.placeholderString = @"请输入账号";
    _userNameField.accessibilityIdentifier = @"smartpark.login.username";
    _userNameField.delegate = self;

    _passwordField = [[NSSecureTextField alloc] init];
    [self configurePasswordField:_passwordField];

    _showPasswordCheck = [NSButton checkboxWithTitle:@"显示密码"
                                              target:self
                                              action:@selector(togglePasswordVisibility:)];
    _showPasswordCheck.state = NSControlStateValueOff;
    _showPasswordCheck.accessibilityIdentifier = @"smartpark.login.showPassword";

    _passwordRow = [NSStackView stackViewWithViews:@[_passwordField, _showPasswordCheck]];
    _passwordRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    _passwordRow.spacing = 8.0;
    _passwordRow.distribution = NSStackViewDistributionFill;

    _rememberCheck = [NSButton checkboxWithTitle:@"记住账号"
                                          target:self
                                          action:@selector(rememberAccountChanged:)];
    _rememberCheck.accessibilityIdentifier = @"smartpark.login.rememberUser";
    _rememberPasswordCheck = [NSButton checkboxWithTitle:@"记住密码"
                                                  target:self
                                                  action:@selector(rememberPasswordChanged:)];
    _rememberPasswordCheck.accessibilityIdentifier = @"smartpark.login.rememberPassword";
    _rememberPasswordCheck.toolTip =
        @"口令保存在系统钥匙串（加密），下次打开自动填入，无需再次输入。";

    NSStackView *rememberRow = [NSStackView stackViewWithViews:@[
        _rememberCheck, _rememberPasswordCheck
    ]];
    rememberRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    rememberRow.spacing = 14.0;

    _errorLabel = [NSTextField wrappingLabelWithString:@""];
    _errorLabel.textColor = [NSColor systemRedColor];
    _errorLabel.font = [NSFont systemFontOfSize:11];
    _errorLabel.accessibilityIdentifier = @"smartpark.login.error";
    _errorLabel.hidden = YES;

    _loginButton = [NSButton buttonWithTitle:@"登录"
                                      target:self
                                      action:@selector(attemptLogin:)];
    _loginButton.bezelStyle = NSBezelStyleRounded;
    _loginButton.accessibilityIdentifier = @"smartpark.login.submit";
    _loginButton.keyEquivalent = @"\r";

    // 远程模式：勾选后连服务端，账号口令由服务端校验，本机数据库不参与。
    _remoteCheck = [NSButton checkboxWithTitle:@"连接远程服务端"
                                        target:self
                                        action:@selector(remoteModeChanged:)];
    _remoteCheck.accessibilityIdentifier = @"smartpark.login.remote";
    _remoteCheck.toolTip =
        @"勾选后通过 TCP 连接服务端，账号与口令由服务端校验，本机数据库不参与；"
        @"停车记录与预约随快照下发，与本地模式一致。"
        @"仅设施配置（布局由服务端 --layout 决定）不可用。";

    _hostField = [[NSTextField alloc] init];
    _hostField.placeholderString = @"服务端地址";
    // 默认填好部署地址：勾上「连接远程服务端」就能直接登录，不用再手输。
    NSUserDefaults *remoteDefaults = [NSUserDefaults standardUserDefaults];
    NSString *rememberedHost = [remoteDefaults stringForKey:kRemoteHostDefaultsKey];
    const NSInteger rememberedPort = [remoteDefaults integerForKey:kRemotePortDefaultsKey];
    _hostField.stringValue = rememberedHost.length > 0 ? rememberedHost : kDefaultRemoteHost;
    _hostField.accessibilityIdentifier = @"smartpark.login.host";
    _hostField.delegate = self;

    _portField = [[NSTextField alloc] init];
    _portField.placeholderString = @"端口";
    _portField.stringValue = [NSString stringWithFormat:@"%ld",
        (long)(rememberedPort > 0 ? rememberedPort : kDefaultRemotePort)];
    _portField.accessibilityIdentifier = @"smartpark.login.port";
    _portField.delegate = self;

    _remoteRow = [NSStackView stackViewWithViews:@[_hostField, _portField]];
    _remoteRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    _remoteRow.spacing = 8.0;
    _remoteRow.distribution = NSStackViewDistributionFill;
    _remoteRow.hidden = YES;

    _registerButton = [NSButton buttonWithTitle:@"注册新账号"
                                         target:self
                                         action:@selector(openRegister:)];
    _registerButton.bezelStyle = NSBezelStyleRounded;
    _registerButton.accessibilityIdentifier = @"smartpark.login.register";

    NSStackView *cardStack = [NSStackView stackViewWithViews:@[
        cardTitle, cardHint, _userNameField, _passwordRow, rememberRow,
        _remoteCheck, _remoteRow, _errorLabel, _loginButton, _registerButton
    ]];
    cardStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    cardStack.alignment = NSLayoutAttributeLeading;
    cardStack.spacing = 9.0;
    cardStack.edgeInsets = NSEdgeInsetsMake(18, 18, 18, 18);
    cardStack.translatesAutoresizingMaskIntoConstraints = NO;
    [card addSubview:cardStack];

    NSTextField *demoHint = [NSTextField wrappingLabelWithString:
        @"账号：admin　密码：smartpark。可注册新账号。"];
    demoHint.font = [NSFont systemFontOfSize:11];
    demoHint.textColor = [NSColor tertiaryLabelColor];
    demoHint.alignment = NSTextAlignmentCenter;

    for (NSView *view in @[title, subtitle, card, demoHint]){
        view.translatesAutoresizingMaskIntoConstraints = NO;
        [root addSubview:view];
    }

    [NSLayoutConstraint activateConstraints:@[
        // 顶部留出无标题栏窗口的交通灯区域。
        [title.topAnchor constraintEqualToAnchor:root.topAnchor constant:44],
        [title.centerXAnchor constraintEqualToAnchor:root.centerXAnchor],
        [subtitle.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:4],
        [subtitle.centerXAnchor constraintEqualToAnchor:root.centerXAnchor],

        [card.topAnchor constraintEqualToAnchor:subtitle.bottomAnchor constant:22],
        [card.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:32],
        [card.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-32],

        [cardStack.topAnchor constraintEqualToAnchor:card.topAnchor],
        [cardStack.leadingAnchor constraintEqualToAnchor:card.leadingAnchor],
        [cardStack.trailingAnchor constraintEqualToAnchor:card.trailingAnchor],
        [cardStack.bottomAnchor constraintEqualToAnchor:card.bottomAnchor],

        [_userNameField.widthAnchor constraintEqualToConstant:292],
        [_passwordRow.widthAnchor constraintEqualToConstant:292],
        [_remoteRow.widthAnchor constraintEqualToConstant:292],
        // 端口固定宽度、地址占满剩余：只给 >= 会让 Fill 分布下的宽度无解，
        // 出现歧义约束（登录页夹具会直接报出来）。
        [_portField.widthAnchor constraintEqualToConstant:88],
        [_hostField.widthAnchor constraintGreaterThanOrEqualToConstant:180],
        // 父栈是 leading 对齐、只钉住左边，不给宽度会留下歧义约束。
        [rememberRow.widthAnchor constraintEqualToConstant:292],
        [_errorLabel.widthAnchor constraintEqualToConstant:292],

        [demoHint.topAnchor constraintEqualToAnchor:card.bottomAnchor constant:16],
        [demoHint.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:32],
        [demoHint.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-32],
    ]];

    self.view = root;
    [self restoreRememberedUser];
}

- (void)configurePasswordField:(NSTextField *)field{
    field.placeholderString = @"请输入密码";
    field.accessibilityIdentifier = @"smartpark.login.password";
    field.delegate = self;
    [field setContentHuggingPriority:NSLayoutPriorityDefaultLow
                      forOrientation:NSLayoutConstraintOrientationHorizontal];
}

- (void)viewDidAppear{
    [super viewDidAppear];
    if (_userNameField.stringValue.length > 0){
        [self.view.window makeFirstResponder:_passwordField];
    } else{
        [self.view.window makeFirstResponder:_userNameField];
    }
}

#pragma mark - 记住账号 / 记住密码

- (IBAction)rememberAccountChanged:(id)sender{
    // 不记住账号时，口令无从对应，一并取消。
    if (_rememberCheck.state != NSControlStateValueOn){
        _rememberPasswordCheck.state = NSControlStateValueOff;
    }
}

- (IBAction)rememberPasswordChanged:(id)sender{
    // 要记住口令就必须记住账号。
    if (_rememberPasswordCheck.state == NSControlStateValueOn){
        _rememberCheck.state = NSControlStateValueOn;
    }
}

- (void)restoreRememberedUser{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    if (![defaults boolForKey:kRememberUserKey]){
        return;
    }
    NSString *lastUser = [defaults stringForKey:kLastUserKey];
    if (lastUser.length > 0){
        _userNameField.stringValue = lastUser;
        _restoredAccount = [lastUser copy];
    }
    _rememberCheck.state = NSControlStateValueOn;

    // 只在用户此前明确勾选过「记住密码」时才去读钥匙串。
    if ([defaults boolForKey:kRememberPasswordKey] && lastUser.length > 0){
        NSString *password = keychainLoad(lastUser);
        if (password.length > 0){
            _passwordField.stringValue = password;
            _rememberPasswordCheck.state = NSControlStateValueOn;
        }
    }
}

- (void)persistRememberedUser:(NSString *)userName password:(NSString *)password{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    const BOOL rememberUser = (_rememberCheck.state == NSControlStateValueOn);
    const BOOL rememberPassword =
        (rememberUser && _rememberPasswordCheck.state == NSControlStateValueOn);

    [defaults setBool:rememberUser forKey:kRememberUserKey];
    [defaults setBool:rememberPassword forKey:kRememberPasswordKey];
    if (rememberUser){
        [defaults setObject:userName forKey:kLastUserKey];
    } else{
        [defaults removeObjectForKey:kLastUserKey];
    }

    // 换账号或取消记住口令时，不保留上次账号的钥匙串条目。
    if (_restoredAccount.length > 0 &&
        (![_restoredAccount isEqualToString:userName] || !rememberPassword)){
        keychainDelete(_restoredAccount);
    }
    _restoredAccount = rememberUser ? [userName copy] : nil;

    if (rememberPassword){
        if (!keychainStore(userName, password)){
            [defaults setBool:NO forKey:kRememberPasswordKey];
            _rememberPasswordCheck.state = NSControlStateValueOff;
            NSAlert *alert = [[NSAlert alloc] init];
            alert.messageText = @"未能记住密码";
            alert.informativeText = @"系统钥匙串写入失败；本次登录仍然有效，下次需重新输入密码。";
            [alert addButtonWithTitle:@"确定"];
            [alert runModal];
        }
    } else{
        keychainDelete(userName);
    }
}

#pragma mark - 口令可见切换

- (IBAction)remoteModeChanged:(id)sender{
    const BOOL remote = _remoteCheck.state == NSControlStateValueOn;
    _remoteRow.hidden = !remote;
    if (remote){
        // 远程模式下本机账号只用于回填，真正的校验在服务端。
        [_hostField becomeFirstResponder];
    }
}

// 启动参数预填：勾上远程模式、填好地址端口账号，焦点直接落在口令框。
- (void)prefillRemoteHost:(NSString *)host port:(NSInteger)port user:(NSString *)user{
    if (host.length == 0){
        return;
    }
    _hostField.stringValue = host;
    if (port > 0){
        _portField.stringValue = [NSString stringWithFormat:@"%ld", (long)port];
    }
    if (user.length > 0){
        _userNameField.stringValue = user;
    }
    _remoteCheck.state = NSControlStateValueOn;
    [self remoteModeChanged:_remoteCheck];   // 显示地址/端口行
    [_passwordField becomeFirstResponder];   // 只剩口令要填
}

// 远程登录：连服务端并用同一个 login action 校验账号口令。
// ServerSession 是异步的，这里用局部 run loop 等结果（与 Gate 端
// TcpClient::request 同样的取舍）；超时按失败处理，不假装成功。
- (BOOL)connectRemoteWithHost:(NSString *)host
                         port:(NSInteger)port
                         user:(NSString *)user
                     password:(NSString *)password
                        error:(NSString **)error{
    auto session = std::make_unique<smartpark::ServerSession>();
    // 普通 C++ 局部：lambda 在本函数内的 run loop 中同步执行，按引用捕获安全；
    // __block 变量无法被 C++ lambda 捕获。
    BOOL settled = NO;
    BOOL succeeded = NO;
    QString message;
    QObject::connect(session.get(), &smartpark::ServerSession::stateChanged,
                     [&](smartpark::ServerSession::State state){
        if (state == smartpark::ServerSession::State::Online){
            succeeded = YES;
            settled = YES;
        }
    });
    QObject::connect(session.get(), &smartpark::ServerSession::authFailed,
                     [&](const QString &text){
        message = text;
        settled = YES;
    });
    session->start(QString::fromUtf8(host.UTF8String),
                   static_cast<quint16>(port),
                   QString::fromUtf8(user.UTF8String),
                   QString::fromUtf8(password.UTF8String));

    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:8.0];
    while (!settled && [deadline timeIntervalSinceNow] > 0.0){
        @autoreleasepool{
            QCoreApplication::processEvents();
            [[NSRunLoop currentRunLoop]
                runMode:NSDefaultRunLoopMode
             beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        }
    }
    session->stop();
    if (succeeded){
        return YES;
    }
    if (error != nil){
        *error = settled
            ? [NSString stringWithFormat:@"服务端拒绝登录：%s",
                                         message.toUtf8().constData()]
            : @"连接服务端超时，请检查地址与端口。";
    }
    return NO;
}

- (IBAction)togglePasswordVisibility:(id)sender{
    const BOOL wantsPlainText = (_showPasswordCheck.state == NSControlStateValueOn);
    const BOOL isPlainText =
        ![_passwordField isKindOfClass:[NSSecureTextField class]];
    if (wantsPlainText == isPlainText){
        return;
    }

    NSString *text = _passwordField.stringValue;
    NSTextField *replacement = wantsPlainText
        ? [[NSTextField alloc] initWithFrame:_passwordField.frame]
        : [[NSSecureTextField alloc] initWithFrame:_passwordField.frame];
    [self configurePasswordField:replacement];
    replacement.stringValue = text;

    const NSUInteger index = [_passwordRow.arrangedSubviews indexOfObject:_passwordField];
    [_passwordRow removeArrangedSubview:_passwordField];
    [_passwordField removeFromSuperview];
    [_passwordRow insertArrangedSubview:replacement
                                atIndex:(index == NSNotFound ? 0 : index)];
    _passwordField = replacement;

    [self.view.window makeFirstResponder:_passwordField];
}

#pragma mark - 提示与锁定

- (void)setError:(NSString *)message focus:(NSTextField *)field{
    _errorLabel.stringValue = message;
    _errorLabel.hidden = (message.length == 0);
    if (field != nil){
        [self.view.window makeFirstResponder:field];
    }
}

- (void)markInvalid:(NSTextField *)field invalid:(BOOL)invalid{
    field.backgroundColor = invalid
        ? [[NSColor systemRedColor] colorWithAlphaComponent:0.12]
        : [NSColor textBackgroundColor];
}

- (void)setLockdown:(BOOL)locked{
    if (locked){
        _lockdownRemaining = kLockdownSeconds;
        _loginButton.enabled = NO;
        _userNameField.enabled = NO;
        _passwordField.enabled = NO;
        _showPasswordCheck.enabled = NO;
        _registerButton.enabled = NO;
        [self updateLockdownTitle];

        __weak LoginViewController *weakSelf = self;
        _lockdownTimer = [NSTimer scheduledTimerWithTimeInterval:1.0
                                                         repeats:YES
                                                           block:^(NSTimer *timer){
            [weakSelf tickLockdown];
        }];
        return;
    }

    [_lockdownTimer invalidate];
    _lockdownTimer = nil;
    _failedAttempts = 0;
    _userNameField.enabled = YES;
    _passwordField.enabled = YES;
    _showPasswordCheck.enabled = YES;
    _registerButton.enabled = YES;
    _loginButton.enabled = YES;
    _loginButton.title = @"登录";
    [self.view.window makeFirstResponder:_passwordField];
}

- (void)updateLockdownTitle{
    _loginButton.title = [NSString stringWithFormat:
        @"尝试过于频繁，%ld 秒后可重试", (long)_lockdownRemaining];
}

- (void)tickLockdown{
    --_lockdownRemaining;
    if (_lockdownRemaining > 0){
        [self updateLockdownTitle];
        return;
    }
    [self setLockdown:NO];
}

#pragma mark - 登录

- (void)controlTextDidChange:(NSNotification *)notification{
    [self markInvalid:(NSTextField *)notification.object invalid:NO];
    _errorLabel.hidden = YES;
}

- (IBAction)attemptLogin:(id)sender{
    if (!_loginButton.enabled){
        return;
    }
    NSString *userName = [_userNameField.stringValue
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    NSString *password = _passwordField.stringValue;

    if (userName.length == 0){
        [self markInvalid:_userNameField invalid:YES];
        [self setError:@"请输入账号。" focus:_userNameField];
        return;
    }
    if (password.length == 0){
        [self markInvalid:_passwordField invalid:YES];
        [self setError:@"请输入密码。" focus:_passwordField];
        return;
    }

    // 远程模式：账号口令交给服务端校验，本机数据库不参与。
    if (_remoteCheck.state == NSControlStateValueOn){
        NSString *host = [_hostField.stringValue
            stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        const NSInteger port = _portField.integerValue;
        if (host.length == 0 || port <= 0 || port > 65535){
            [self setError:@"请填写有效的服务端地址与端口（1-65535）。"
                     focus:host.length == 0 ? _hostField : _portField];
            return;
        }
        _loginButton.enabled = NO;
        _loginButton.title = @"连接中…";
        NSString *failure = nil;
        const BOOL connected = [self connectRemoteWithHost:host port:port
                                                      user:userName
                                                  password:password
                                                     error:&failure];
        _loginButton.enabled = YES;
        _loginButton.title = @"登录";
        if (!connected){
            ++_failedAttempts;
            [self setError:failure focus:nil];
            return;
        }
        [self persistRememberedUser:userName password:password];
        // 记住这次用的地址端口，下次启动直接带出来。
        NSUserDefaults *remoteDefaults = [NSUserDefaults standardUserDefaults];
        [remoteDefaults setObject:host forKey:kRemoteHostDefaultsKey];
        [remoteDefaults setInteger:port forKey:kRemotePortDefaultsKey];
        void (^remoteCallback)(NSString *, NSString *, NSInteger, NSString *) =
            self.onAuthenticated;
        if (remoteCallback != nil){
            remoteCallback(userName, host, port, password);
        }
        return;
    }

    const auto result = _userStore != nullptr
        ? _userStore->verifyLogin(QString::fromUtf8(userName.UTF8String),
                                  QString::fromUtf8(password.UTF8String))
        : smartpark::UserStore::LoginResult::StorageError;

    if (result == smartpark::UserStore::LoginResult::Success){
        [self persistRememberedUser:userName password:password];
        void (^callback)(NSString *, NSString *, NSInteger, NSString *) =
            self.onAuthenticated;
        if (callback != nil){
            callback(userName, nil, 0, nil);
        }
        return;
    }

    ++_failedAttempts;
    if (result == smartpark::UserStore::LoginResult::WrongPassword){
        [self markInvalid:_passwordField invalid:YES];
        [_passwordField selectText:nil];
        if (_failedAttempts >= kMaxFailedAttempts){
            [self setError:[NSString stringWithFormat:
                @"连续 %ld 次密码错误，已临时锁定登录。", (long)_failedAttempts]
                     focus:nil];
            [self setLockdown:YES];
            return;
        }
        [self setError:[NSString stringWithFormat:@"%@（还可尝试 %ld 次）",
            [self loginErrorText:result],
            (long)(kMaxFailedAttempts - _failedAttempts)]
                 focus:_passwordField];
        return;
    }
    if (result == smartpark::UserStore::LoginResult::UnknownUser){
        [self markInvalid:_userNameField invalid:YES];
        [self setError:[self loginErrorText:result] focus:_userNameField];
        return;
    }
    [self setError:[self loginErrorText:result] focus:nil];
}

- (NSString *)loginErrorText:(smartpark::UserStore::LoginResult)result{
    return [NSString stringWithUTF8String:
        smartpark::UserStore::loginErrorText(result).toUtf8().constData()];
}

#pragma mark - 注册

- (IBAction)openRegister:(id)sender{
    if (_userStore == nullptr){
        return;
    }
    RegisterViewController *registerPage =
        [[RegisterViewController alloc] initWithUserStore:_userStore];

    __weak LoginViewController *weakSelf = self;
    registerPage.onRegistered = ^(NSString *userName){
        LoginViewController *strongSelf = weakSelf;
        if (strongSelf == nil){
            return;
        }
        // 与 Qt 版一致：注册成功后回填账号并把焦点移到口令框。
        strongSelf->_userNameField.stringValue = userName;
        strongSelf->_userNameField.backgroundColor = [NSColor textBackgroundColor];
        [strongSelf setError:@"" focus:strongSelf->_passwordField];
    };

    [self presentViewControllerAsSheet:registerPage];
}

@end
