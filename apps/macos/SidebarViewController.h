#import <Cocoa/Cocoa.h>

@class SidebarViewController;

@protocol SidebarDelegate <NSObject>
- (void)sidebar:(SidebarViewController *)sidebar didSelectIndex:(NSInteger)index;
@end

@interface SidebarViewController : NSViewController
@property (nonatomic, weak) id<SidebarDelegate> delegate;
@end
