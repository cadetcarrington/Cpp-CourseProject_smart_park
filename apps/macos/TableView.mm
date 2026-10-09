#import "TableView.h"

// 普通文本单元格：容器 + 居中标签。
// 直接把 NSTextField 当单元格用会因默认 wraps=YES 而贴顶显示。
@interface SPTableTextCell : NSView
@property (nonatomic, strong) NSTextField *label;
@end

@implementation SPTableTextCell

- (instancetype)initWithFrame:(NSRect)frameRect{
    if ((self = [super initWithFrame:frameRect])){
        _label = [NSTextField labelWithString:@""];
        _label.font = [NSFont systemFontOfSize:11];
        _label.lineBreakMode = NSLineBreakByTruncatingTail;
        _label.selectable = YES;
        _label.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:_label];
        [NSLayoutConstraint activateConstraints:@[
            [_label.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:4],
            [_label.trailingAnchor constraintLessThanOrEqualToAnchor:self.trailingAnchor
                                                            constant:-4],
            [_label.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        ]];
    }
    return self;
}

@end

// 胶囊徽章单元格：圆角矩形底色 + 同色系文字，宽度随文字长度收缩。
@interface SPTableBadgeCell : NSView
@property (nonatomic, strong) NSView *pill;
@property (nonatomic, strong) NSTextField *label;
@end

@implementation SPTableBadgeCell

- (instancetype)initWithFrame:(NSRect)frameRect{
    if ((self = [super initWithFrame:frameRect])){
        _pill = [[NSView alloc] initWithFrame:NSZeroRect];
        _pill.wantsLayer = YES;
        _pill.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:_pill];

        _label = [NSTextField labelWithString:@""];
        _label.font = [NSFont systemFontOfSize:11 weight:NSFontWeightMedium];
        _label.alignment = NSTextAlignmentCenter;
        _label.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:_label];

        [NSLayoutConstraint activateConstraints:@[
            // 高度决定胶囊半径（= 高度/2），左右各留 9pt 内边距。
            [_pill.heightAnchor constraintEqualToConstant:18],
            [_pill.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_pill.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:2],
            [_pill.trailingAnchor constraintEqualToAnchor:_label.trailingAnchor constant:9],

            [_label.leadingAnchor constraintEqualToAnchor:_pill.leadingAnchor constant:9],
            [_label.centerYAnchor constraintEqualToAnchor:_pill.centerYAnchor],
        ]];
    }
    return self;
}

- (void)applyText:(NSString *)text tint:(NSColor *)tint{
    _label.stringValue = text;
    _label.textColor = tint;
    _pill.layer.backgroundColor =
        [tint colorWithAlphaComponent:0.16].CGColor;
    _pill.layer.cornerRadius = 9.0;
}

@end

@interface TableView () <NSTableViewDataSource, NSTableViewDelegate>
@property (nonatomic, strong) NSScrollView *scroll;
@property (nonatomic, strong) NSTableView *tableView;
@property (nonatomic, copy) NSArray<NSString *> *columns;
@property (nonatomic, copy) NSArray<NSArray<NSString *> *> *rows;
// 各列的期望宽度：布局时按此比例缩放铺满整个表宽。
@property (nonatomic, copy) NSArray<NSNumber *> *preferredWidths;
@property (nonatomic, assign) NSInteger badgeColumn;
@property (nonatomic, copy) NSColor *(^badgeTintBlock)(NSString *value);
@end

@implementation TableView

- (instancetype)initWithFrame:(NSRect)frameRect{
    if ((self = [super initWithFrame:frameRect])){
        _columns = @[];
        _rows = @[];
        _badgeColumn = -1;

        _scroll = [[NSScrollView alloc] initWithFrame:self.bounds];
        _scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        _scroll.hasVerticalScroller = YES;
        _scroll.hasHorizontalScroller = NO;
        _scroll.drawsBackground = NO;
        // 叠加式滚动条：不占用固定槽位，列宽可以铺满整个可见宽度。
        _scroll.scrollerStyle = NSScrollerStyleOverlay;
        _scroll.autohidesScrollers = YES;
        [self addSubview:_scroll];

        _tableView = [[NSTableView alloc] initWithFrame:_scroll.contentView.bounds];
        _tableView.dataSource = self;
        _tableView.delegate = self;
        _tableView.usesAlternatingRowBackgroundColors = YES;
        _tableView.rowHeight = 24.0;
        _tableView.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
        _tableView.allowsMultipleSelection = NO;
        // 宽度交给下面的 layout 精确设置，不用 autoresizing：否则它跟随滚动视图
        // 外框而不是可视区域，会多出约 30pt 的横向溢出。
        _scroll.documentView = _tableView;
    }
    return self;
}

// 表格宽度必须跟随滚动视图，否则列会停在初始宽度，
// 只铺满左边一半、右边留出大片空白。
- (void)layout{
    [super layout];
    const CGFloat width = NSWidth(_scroll.contentView.bounds);
    if (width <= 0.0){
        return;
    }
    if (fabs(NSWidth(_tableView.frame) - width) > 0.5){
        NSRect frame = _tableView.frame;
        frame.size.width = width;
        _tableView.frame = frame;
    }
    [self distributeColumnsToWidth:width];
}

// 按 preferredWidths 的比例把可用宽度分给各列，保证铺满。
- (void)distributeColumnsToWidth:(CGFloat)width{
    NSArray<NSTableColumn *> *tableColumns = _tableView.tableColumns;
    if (tableColumns.count == 0 || _preferredWidths.count != tableColumns.count){
        return;
    }
    const CGFloat spacing =
        _tableView.intercellSpacing.width * (CGFloat)(tableColumns.count - 1);
    const CGFloat available = MAX(0.0, width - spacing);
    CGFloat totalWeight = 0.0;
    for (NSNumber *weight in _preferredWidths){
        totalWeight += weight.doubleValue;
    }
    if (totalWeight <= 0.0){
        return;
    }
    CGFloat assigned = 0.0;
    for (NSUInteger index = 0; index < tableColumns.count; ++index){
        const CGFloat target =
            available * _preferredWidths[index].doubleValue / totalWeight;
        // 末列吃掉取整误差，避免右侧留下几像素缝隙。
        const CGFloat finalWidth = (index + 1 == tableColumns.count)
            ? MAX(0.0, available - assigned) : floor(target);
        assigned += finalWidth;
        if (fabs(tableColumns[index].width - finalWidth) > 0.5){
            tableColumns[index].width = finalWidth;
        }
    }
}

- (void)setColumns:(NSArray<NSString *> *)columns{
    _columns = [columns copy];
    for (NSTableColumn *column in [_tableView.tableColumns copy]){
        [_tableView removeTableColumn:column];
    }
    NSMutableArray<NSNumber *> *widths = [NSMutableArray array];
    for (NSString *title in columns){
        NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:title];
        column.title = title;
        column.width = 110.0;
        column.minWidth = 56.0;
        [_tableView addTableColumn:column];
        [widths addObject:@110.0];
    }
    _preferredWidths = widths;
    [self setNeedsLayout:YES];
}

- (void)setRows:(NSArray<NSArray<NSString *> *> *)rows{
    _rows = [rows copy];
    [_tableView reloadData];
}

- (void)setBadgeColumn:(NSInteger)column tintBlock:(NSColor *(^)(NSString *))tintBlock{
    _badgeColumn = column;
    _badgeTintBlock = [tintBlock copy];
    // 徽章列不需要和其它列一样宽。
    if (column >= 0 && column < (NSInteger)_tableView.tableColumns.count){
        NSTableColumn *tableColumn = _tableView.tableColumns[(NSUInteger)column];
        tableColumn.minWidth = 72.0;
        NSMutableArray<NSNumber *> *widths = [_preferredWidths mutableCopy];
        widths[(NSUInteger)column] = @92.0;
        _preferredWidths = widths;
        [self setNeedsLayout:YES];
    }
    [_tableView reloadData];
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification{
    if (self.selectionHandler != nil){
        self.selectionHandler(_tableView.selectedRow);
    }
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView{
    return (NSInteger)_rows.count;
}

- (NSView *)tableView:(NSTableView *)tableView
   viewForTableColumn:(NSTableColumn *)tableColumn
                  row:(NSInteger)row{
    const NSInteger columnIndex = [tableView.tableColumns indexOfObject:tableColumn];
    NSArray<NSString *> *rowData = _rows[(NSUInteger)row];
    NSString *value = (columnIndex >= 0 && columnIndex < (NSInteger)rowData.count)
        ? rowData[(NSUInteger)columnIndex] : @"";

    if (columnIndex == _badgeColumn && _badgeTintBlock != nil){
        SPTableBadgeCell *cell = [tableView makeViewWithIdentifier:@"BadgeCell" owner:self];
        if (cell == nil){
            cell = [[SPTableBadgeCell alloc] initWithFrame:NSMakeRect(0, 0, 92, 24)];
            cell.identifier = @"BadgeCell";
        }
        [cell applyText:value tint:_badgeTintBlock(value)];
        return cell;
    }

    SPTableTextCell *cell = [tableView makeViewWithIdentifier:@"TextCell" owner:self];
    if (cell == nil){
        cell = [[SPTableTextCell alloc] initWithFrame:NSMakeRect(0, 0, 120, 24)];
        cell.identifier = @"TextCell";
    }
    cell.label.stringValue = value;
    return cell;
}

@end
