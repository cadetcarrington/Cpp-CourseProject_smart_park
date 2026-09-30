#import <Cocoa/Cocoa.h>

@class SidebarViewController;

@protocol SidebarDelegate <NSObject>
- (void)sidebar:(SidebarViewController *)sidebar didSelectIndex:(NSInteger)index;
@end

@interface SidebarViewController : NSViewController
@property (nonatomic, weak) id<SidebarDelegate> delegate;

// 当前登录账号，显示在侧边栏底部。
@property (nonatomic, copy) NSString *userName;
// 用户确认「退出登录」后回调，由 AppDelegate 切回登录界面。
@property (nonatomic, copy) void (^logoutHandler)(void);
@end
