#pragma once

#import <Cocoa/Cocoa.h>

namespace smartpark{
class UserStore;
}

// 管理端登录界面（原生 AppKit）。
// 认证走 core 的 UserStore（SQLite users 表），行为与 Qt 版 LoginDialog 对齐：
// 空字段校验、账号不存在 / 口令错误分别提示、连续 5 次失败锁定 30 秒、
// 口令可见切换、记住账号（NSUserDefaults，只存账号不存口令）、注册入口。
@interface LoginViewController : NSViewController

- (instancetype)initWithUserStore:(smartpark::UserStore *)userStore;

// 登录成功后回调，参数为账号名。
@property (nonatomic, copy) void (^onAuthenticated)(NSString *userName);

@end
