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

// 供外部（如仪表盘快捷操作卡片）程序化选中某个导航项。
- (void)selectIndex:(NSInteger)index;

// 隐藏若干页面（下标与 SmartParkPage / MainWindowController.pages 一致）。
// 远程模式覆盖不到的页面用它移除，避免点进去是一片空表；
// 注意行号与页下标不再相等，内部按可见页映射换算。
- (void)setHiddenPages:(NSIndexSet *)indexes;
@end
