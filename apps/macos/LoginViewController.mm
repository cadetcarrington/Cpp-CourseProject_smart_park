#import "LoginViewController.h"

#import "RegisterViewController.h"

#include "core/service/UserStore.h"

#include <QString>

#include <memory>

// 与 Qt 版 LoginDialog 一致的失败上限与锁定时长。
static const NSInteger kMaxFailedAttempts = 5;
static const NSInteger kLockdownSeconds = 30;

// NSUserDefaults 键：只保存「记住账号」的账号文本，绝不保存口令。
static NSString *const kRememberUserKey = @"SmartParkRememberUser";
static NSString *const kLastUserKey = @"SmartParkLastUser";

@interface LoginViewController () <NSTextFieldDelegate>
@end

@implementation LoginViewController{
    smartpark::UserStore *_userStore;   // 不持有所有权
    NSTextField *_userNameField;
    NSTextField *_passwordField;        // 在 NSSecureTextField / NSTextField 之间切换
    NSStackView *_passwordRow;
    NSButton *_showPasswordCheck;
    NSButton *_rememberCheck;
    NSTextField *_errorLabel;
    NSButton *_loginButton;
    NSButton *_registerButton;
    NSTimer *_lockdownTimer;
    NSInteger _failedAttempts;
    NSInteger _lockdownRemaining;
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
    _userNameField.delegate = self;

    _passwordField = [[NSSecureTextField alloc] init];
    [self configurePasswordField:_passwordField];

    _showPasswordCheck = [NSButton checkboxWithTitle:@"显示密码"
                                              target:self
                                              action:@selector(togglePasswordVisibility:)];
    _showPasswordCheck.state = NSControlStateValueOff;

    _passwordRow = [NSStackView stackViewWithViews:@[_passwordField, _showPasswordCheck]];
    _passwordRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    _passwordRow.spacing = 8.0;
    _passwordRow.distribution = NSStackViewDistributionFill;

    _rememberCheck = [NSButton checkboxWithTitle:@"记住账号"
                                          target:nil
                                          action:nil];

    _errorLabel = [NSTextField wrappingLabelWithString:@""];
    _errorLabel.textColor = [NSColor systemRedColor];
    _errorLabel.font = [NSFont systemFontOfSize:11];
    _errorLabel.hidden = YES;

    _loginButton = [NSButton buttonWithTitle:@"登录"
                                      target:self
                                      action:@selector(attemptLogin:)];
    _loginButton.bezelStyle = NSBezelStyleRounded;
    _loginButton.keyEquivalent = @"\r";

    _registerButton = [NSButton buttonWithTitle:@"注册新账号"
                                         target:self
                                         action:@selector(openRegister:)];
    _registerButton.bezelStyle = NSBezelStyleRounded;

    NSStackView *cardStack = [NSStackView stackViewWithViews:@[
        cardTitle, cardHint, _userNameField, _passwordRow, _rememberCheck,
        _errorLabel, _loginButton, _registerButton
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

#pragma mark - 记住账号（NSUserDefaults）

- (void)restoreRememberedUser{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    if (![defaults boolForKey:kRememberUserKey]){
        return;
    }
    NSString *lastUser = [defaults stringForKey:kLastUserKey];
    if (lastUser.length > 0){
        _userNameField.stringValue = lastUser;
    }
    _rememberCheck.state = NSControlStateValueOn;
}

- (void)persistRememberedUser:(NSString *)userName{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    const BOOL remember = (_rememberCheck.state == NSControlStateValueOn);
    [defaults setBool:remember forKey:kRememberUserKey];
    if (remember){
        [defaults setObject:userName forKey:kLastUserKey];
    } else{
        [defaults removeObjectForKey:kLastUserKey];
    }
}

#pragma mark - 口令可见切换

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

    const auto result = _userStore != nullptr
        ? _userStore->verifyLogin(QString::fromUtf8(userName.UTF8String),
                                  QString::fromUtf8(password.UTF8String))
        : smartpark::UserStore::LoginResult::StorageError;

    if (result == smartpark::UserStore::LoginResult::Success){
        [self persistRememberedUser:userName];
        void (^callback)(NSString *) = self.onAuthenticated;
        if (callback != nil){
            callback(userName);
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
