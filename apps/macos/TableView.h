#import <Cocoa/Cocoa.h>

// 通用只读表格：封装 NSScrollView + NSTableView，单元格文本可选中复制。
// 供「当前车位 / 预约管理 / 停车记录」等页面复用。
@interface TableView : NSView
@property (nonatomic, readonly) NSTableView *tableView;
- (void)setColumns:(NSArray<NSString *> *)columns;
- (void)setRows:(NSArray<NSArray<NSString *> *> *)rows;
@end
