#import <Cocoa/Cocoa.h>

// 通用只读表格：封装 NSScrollView + NSTableView，单元格文本可选中复制。
// 供「当前车位 / 预约管理 / 停车记录」等页面复用。
NS_ASSUME_NONNULL_BEGIN

@interface TableView : NSView
@property (nonatomic, readonly) NSTableView *tableView;
// 未设置时为 nil（未选中任何行时无回调）。
@property (nonatomic, copy, nullable) void (^selectionHandler)(NSInteger row);
- (void)setColumns:(NSArray<NSString *> *)columns;
- (void)setRows:(NSArray<NSArray<NSString *> *> *)rows;
// 把第 column 列渲染成胶囊徽章（圆角底色 + 同色系文字）；
// tintBlock 按取值返回主题色，返回 nil 时退化为普通文本。
- (void)setBadgeColumn:(NSInteger)column
             tintBlock:(NSColor * _Nullable (^)(NSString *value))tintBlock;
// 每列的最小宽度（按列顺序，个数应与 setColumns: 一致；缺省或 0 表示不限制）。
// 表宽不足以容纳全部最小宽度时不再压扁各列，而是横向滚动 —— 窄窗口下
// 日期、类型这类长文本仍完整可读。必须在 setColumns: 之后调用。
- (void)setMinimumColumnWidths:(NSArray<NSNumber *> *)widths;
// 行数据访问：选中一行后，页面据此拿到该行的完整字段再做业务操作。
// row 越界返回 nil。
- (nullable NSArray<NSString *> *)rowAtIndex:(NSInteger)row;
- (nullable NSString *)valueAtRow:(NSInteger)row column:(NSInteger)column;
@end

NS_ASSUME_NONNULL_END
