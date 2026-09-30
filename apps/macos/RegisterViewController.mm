#import "RegisterViewController.h"

#include "core/service/UserStore.h"

#include <QString>

// 注册表单：账号 / 口令 / 确认口令 + 强度与错误提示。
// 校验用 NSTextFieldDelegate 的 controlTextDidChange: 实时触发，
// 提交时再走一次 UserStore 的权威校验。
@interface RegisterViewController () <NSTextFieldDelegate>
@end

@implementation RegisterViewController{
    smartpark::UserStore *_userStore;   // 不持有所有权
    NSTextField *_userNameField;
    NSSecureTextField *_passwordField;
    NSSecureTextField *_confirmField;
    NSTextField *_strengthLabel;
    NSTextField *_errorLabel;
    NSButton *_registerButton;
}

- (instancetype)initWithUserStore:(smartpark::UserStore *)userStore{
    if ((self = [super initWithNibName:nil bundle:nil])){
        _userStore = userStore;
    }
    return self;
}

- (void)loadView{
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 420, 320)];

    NSTextField *title = [NSTextField labelWithString:@"注册新账号"];
    title.font = [NSFont systemFontOfSize:16 weight:NSFontWeightSemibold];
    title.textColor = [NSColor labelColor];

    NSTextField *hint = [NSTextField wrappingLabelWithString:
        @"账号 2-24 个字符且不含空格；口令至少 6 位。"];
    hint.textColor = [NSColor secondaryLabelColor];
    hint.font = [NSFont systemFontOfSize:11];

    _userNameField = [[NSTextField alloc] init];
    _userNameField.placeholderString = @"账号（2-24 个字符，不含空格）";
    _userNameField.delegate = self;

    _passwordField = [[NSSecureTextField alloc] init];
    _passwordField.placeholderString = @"口令（至少 6 位）";
    _passwordField.delegate = self;

    _confirmField = [[NSSecureTextField alloc] init];
    _confirmField.placeholderString = @"再次输入口令";
    _confirmField.delegate = self;

    _strengthLabel = [NSTextField wrappingLabelWithString:@""];
    _strengthLabel.textColor = [NSColor secondaryLabelColor];
    _strengthLabel.font = [NSFont systemFontOfSize:11];

    _errorLabel = [NSTextField wrappingLabelWithString:@""];
    _errorLabel.textColor = [NSColor systemRedColor];
    _errorLabel.font = [NSFont systemFontOfSize:11];
    _errorLabel.hidden = YES;

    _registerButton = [NSButton buttonWithTitle:@"注册账号"
                                         target:self
                                         action:@selector(register:)];
    _registerButton.bezelStyle = NSBezelStyleRounded;
    _registerButton.keyEquivalent = @"\r";

    NSButton *cancelButton = [NSButton buttonWithTitle:@"取消"
                                                target:self
                                                action:@selector(cancel:)];
    cancelButton.bezelStyle = NSBezelStyleRounded;
    cancelButton.keyEquivalent = @"\033";

    NSStackView *buttons = [NSStackView stackViewWithViews:@[_registerButton, cancelButton]];
    buttons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    buttons.spacing = 8.0;

    NSStackView *stack = [NSStackView stackViewWithViews:@[
        title, hint, _userNameField, _passwordField, _confirmField,
        _strengthLabel, _errorLabel, buttons
    ]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 8.0;
    stack.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20);
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:stack];

    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:root.topAnchor],
        [stack.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],
        [_userNameField.widthAnchor constraintEqualToConstant:360],
        [_passwordField.widthAnchor constraintEqualToConstant:360],
        [_confirmField.widthAnchor constraintEqualToConstant:360],
        [hint.widthAnchor constraintEqualToConstant:360],
        [_strengthLabel.widthAnchor constraintEqualToConstant:360],
        [_errorLabel.widthAnchor constraintEqualToConstant:360],
    ]];

    self.view = root;
    [self refreshStrength];
}

- (void)viewDidAppear{
    [super viewDidAppear];
    [self.view.window makeFirstResponder:_userNameField];
}

#pragma mark - 实时校验

- (void)controlTextDidChange:(NSNotification *)notification{
    [self refreshStrength];
    // 编辑后收起上一次的错误提示，避免误导。
    _errorLabel.hidden = YES;
}

- (void)refreshStrength{
    const QString password = QString::fromUtf8(
        _passwordField.stringValue.UTF8String);
    QString strength;
    if (password.isEmpty()){
        strength = QStringLiteral("口令至少 6 位，建议 12 位以上。");
    } else if (password.size() < 8){
        strength = QStringLiteral("口令强度：弱");
    } else if (password.size() < 12){
        strength = QStringLiteral("口令强度：中");
    } else{
        strength = QStringLiteral("口令强度：强");
    }
    _strengthLabel.stringValue = [NSString stringWithUTF8String:
        strength.toUtf8().constData()];
}

- (void)showError:(NSString *)message focus:(NSTextField *)field{
    _errorLabel.stringValue = message;
    _errorLabel.hidden = (message.length == 0);
    if (field != nil){
        [self.view.window makeFirstResponder:field];
    }
}

#pragma mark - 提交

- (void)register:(id)sender{
    if (_userStore == nullptr){
        [self showError:@"账号库不可用，无法注册。" focus:nil];
        return;
    }
    const QString userName = QString::fromUtf8(
        _userNameField.stringValue.UTF8String).trimmed();
    const QString password = QString::fromUtf8(_passwordField.stringValue.UTF8String);
    const QString confirm = QString::fromUtf8(_confirmField.stringValue.UTF8String);

    if (userName.isEmpty() || password.isEmpty() || confirm.isEmpty()){
        [self showError:[NSString stringWithUTF8String:
            smartpark::UserStore::registerErrorText(
                smartpark::UserStore::RegisterResult::EmptyFields)
                .toUtf8().constData()]
                  focus:nil];
        return;
    }
    if (!smartpark::UserStore::isValidUserName(userName)){
        [self showError:[NSString stringWithUTF8String:
            smartpark::UserStore::registerErrorText(
                smartpark::UserStore::RegisterResult::InvalidUserName)
                .toUtf8().constData()]
                  focus:_userNameField];
        return;
    }
    if (!smartpark::UserStore::isValidPassword(password)){
        [self showError:[NSString stringWithUTF8String:
            smartpark::UserStore::registerErrorText(
                smartpark::UserStore::RegisterResult::InvalidPassword)
                .toUtf8().constData()]
                  focus:_passwordField];
        return;
    }
    if (password != confirm){
        _confirmField.stringValue = @"";
        [self showError:@"两次输入的密码不一致，请检查后重试。" focus:_confirmField];
        return;
    }

    const auto result = _userStore->registerUser(userName, password);
    if (result == smartpark::UserStore::RegisterResult::Success){
        void (^callback)(NSString *) = self.onRegistered;
        NSString *name = [NSString stringWithUTF8String:userName.toUtf8().constData()];
        [self dismissIfPresented];
        if (callback != nil){
            callback(name);
        }
        return;
    }
    if (result == smartpark::UserStore::RegisterResult::DuplicateUser){
        [self showError:[NSString stringWithUTF8String:
            smartpark::UserStore::registerErrorText(result).toUtf8().constData()]
                  focus:_userNameField];
        return;
    }
    [self showError:[NSString stringWithUTF8String:
        smartpark::UserStore::registerErrorText(result).toUtf8().constData()]
              focus:_passwordField];
}

- (void)cancel:(id)sender{
    [self dismissIfPresented];
}

// 只有确实以 sheet 呈现时才调用 dismissViewController:，
// 否则 AppKit 会抛 NSInternalInconsistencyException。
- (void)dismissIfPresented{
    if (self.presentingViewController != nil){
        [self dismissViewController:self];
    }
}

@end
