#import <Cocoa/Cocoa.h>

// 通用只读表格：封装 NSScrollView + NSTableView，单元格文本可选中复制。
// 供「当前车位 / 预约管理 / 停车记录」等页面复用。
@interface TableView : NSView
@property (nonatomic, readonly) NSTableView *tableView;
@property (nonatomic, copy) void (^selectionHandler)(NSInteger row);
- (void)setColumns:(NSArray<NSString *> *)columns;
- (void)setRows:(NSArray<NSArray<NSString *> *> *)rows;
// 把第 column 列渲染成胶囊徽章（圆角底色 + 同色系文字）；
// tintBlock 按取值返回主题色，返回 nil 时退化为普通文本。
- (void)setBadgeColumn:(NSInteger)column
             tintBlock:(NSColor *(^)(NSString *value))tintBlock;
@end
