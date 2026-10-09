#pragma once

#import <Cocoa/Cocoa.h>

namespace smartpark{
class UserStore;
}

// 新管理员注册（原生 AppKit，以 sheet 形式呈现）。
// 账号 + 口令 + 确认口令，实时校验并写入 UserStore；
// 与 Qt 版 RegisterDialog 行为一致：空字段、账号格式、口令长度、
// 两次不一致、重复账号分别提示，并显示口令强度。
@interface RegisterViewController : NSViewController

- (instancetype)initWithUserStore:(smartpark::UserStore *)userStore;

// 注册成功后回调，参数为新账号名（用于登录页预填）。
@property (nonatomic, copy) void (^onRegistered)(NSString *userName);

@end
