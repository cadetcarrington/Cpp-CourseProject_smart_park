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
        _label.font = [NSFont systemFontOfSize:12];
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
        _label.font = [NSFont systemFontOfSize:12 weight:NSFontWeightMedium];
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
// 各列的最小宽度：表宽不足时不再压扁，而是让表格横向滚动。
@property (nonatomic, copy) NSArray<NSNumber *> *minimumWidths;
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
        // 允许横向滚动：窗口变窄时各列保持最小宽度，内容比可视区域宽就横向滚动，
        // 而不是把日期/类型压成省略号（叠加式滚动条不占固定槽位）。
        _scroll.hasHorizontalScroller = YES;
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
        // 列宽完全由 layout 按「期望宽度 + 最小宽度」算好，交给 NSTableView
        // 自动分配会覆盖最小宽度（窄窗口下又压扁成省略号）。
        _tableView.columnAutoresizingStyle = NSTableViewNoColumnAutoresizing;
        _tableView.allowsMultipleSelection = NO;
        // 宽度交给下面的 layout 精确设置，不用 autoresizing：否则它跟随滚动视图
        // 外框而不是可视区域，会多出约 30pt 的横向溢出。
        _scroll.documentView = _tableView;
    }
    return self;
}

// 表格宽度跟随滚动视图；宽度不够容纳全部列的最小宽度时把文档视图撑宽，
// 由横向滚动条兜底（列宽不再按比例压扁）。
- (void)layout{
    [super layout];
    const CGFloat viewport = NSWidth(_scroll.contentView.bounds);
    if (viewport <= 0.0){
        return;
    }
    const CGFloat spacing = [self columnSpacing];
    const CGFloat minTotal = [self minimumWidthsTotal];
    // 文档宽度 = max(可视宽度, 最小宽度之和 + 列间距)：
    // 宽窗口铺满可见区域，窄窗口保留最小宽度并可横向滚动。
    const CGFloat documentWidth = MAX(viewport, minTotal + spacing);
    if (fabs(NSWidth(_tableView.frame) - documentWidth) > 0.5){
        NSRect frame = _tableView.frame;
        frame.size.width = documentWidth;
        _tableView.frame = frame;
    }
    [self distributeColumnsToWidth:documentWidth];
}

// 列间距总和（NSTableView 各列之间的 intercellSpacing）。
- (CGFloat)columnSpacing{
    const NSUInteger count = _tableView.tableColumns.count;
    return count > 1 ? _tableView.intercellSpacing.width * (CGFloat)(count - 1) : 0.0;
}

// 各列最小宽度之和；未设置最小宽度时退回 NSTableColumn.minWidth（默认 56）。
- (CGFloat)minimumWidthsTotal{
    CGFloat total = 0.0;
    const NSUInteger count = _tableView.tableColumns.count;
    for (NSUInteger index = 0; index < count; ++index){
        total += [self minimumWidthForColumn:index];
    }
    return total;
}

- (CGFloat)minimumWidthForColumn:(NSUInteger)index{
    if (index < _minimumWidths.count){
        return MAX(0.0, _minimumWidths[index].doubleValue);
    }
    NSArray<NSTableColumn *> *tableColumns = _tableView.tableColumns;
    return index < tableColumns.count ? MAX(0.0, tableColumns[index].minWidth) : 0.0;
}

// 在「每列不低于最小宽度」的前提下按期望宽度比例分配宽度。
// 先给每列最小宽度，再把剩余宽度按「期望宽度 – 最小宽度」的可伸缩空间
// 比例分给各列；窗口窄到没有剩余宽度时各列正好停在最小宽度，由横向滚动展示。
- (void)distributeColumnsToWidth:(CGFloat)width{
    NSArray<NSTableColumn *> *tableColumns = _tableView.tableColumns;
    const NSUInteger count = tableColumns.count;
    if (count == 0 || _preferredWidths.count != count){
        return;
    }
    const CGFloat spacing = [self columnSpacing];
    const CGFloat available = MAX(0.0, width - spacing);

    NSMutableArray<NSNumber *> *minimums = [NSMutableArray arrayWithCapacity:count];
    NSMutableArray<NSNumber *> *flexible = [NSMutableArray arrayWithCapacity:count];
    CGFloat minTotal = 0.0;
    CGFloat flexibleTotal = 0.0;
    for (NSUInteger index = 0; index < count; ++index){
        const CGFloat minWidth = [self minimumWidthForColumn:index];
        // 期望宽度低于最小宽度时按最小宽度算；可伸缩空间至少留 1pt，
        // 保证窗口变宽时各列都还能长。
        const CGFloat preferred = MAX(minWidth, _preferredWidths[index].doubleValue);
        const CGFloat grow = MAX(1.0, preferred - minWidth);
        [minimums addObject:@(minWidth)];
        [flexible addObject:@(grow)];
        minTotal += minWidth;
        flexibleTotal += grow;
    }

    const CGFloat target = MAX(available, minTotal);
    const CGFloat extra = MAX(0.0, target - minTotal);
    CGFloat assigned = 0.0;
    for (NSUInteger index = 0; index < count; ++index){
        const CGFloat minWidth = minimums[index].doubleValue;
        const CGFloat grow = extra * (flexible[index].doubleValue / flexibleTotal);
        // 末列吃掉取整误差，避免右侧留下几像素缝隙；同时不低于最小宽度。
        const CGFloat finalWidth = (index + 1 == count)
            ? MAX(minWidth, target - assigned)
            : MAX(minWidth, floor(minWidth + grow));
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
    // 列集合变了，之前设置的最小宽度不再对应，回到默认（setColumns: 的旧行为）。
    _minimumWidths = @[];
    [self setNeedsLayout:YES];
}

- (void)setMinimumColumnWidths:(NSArray<NSNumber *> *)widths{
    _minimumWidths = [widths copy];
    // 同步到 NSTableColumn.minWidth，让 AppKit 侧的尺寸计算也知道下限。
    NSArray<NSTableColumn *> *tableColumns = _tableView.tableColumns;
    for (NSUInteger index = 0; index < tableColumns.count && index < widths.count; ++index){
        const CGFloat minWidth = MAX(0.0, widths[index].doubleValue);
        if (minWidth > 0.0){
            tableColumns[index].minWidth = minWidth;
        }
    }
    [self setNeedsLayout:YES];
}

- (nullable NSArray<NSString *> *)rowAtIndex:(NSInteger)row{
    if (row < 0 || (NSUInteger)row >= _rows.count){
        return nil;
    }
    return _rows[(NSUInteger)row];
}

- (nullable NSString *)valueAtRow:(NSInteger)row column:(NSInteger)column{
    NSArray<NSString *> *rowData = [self rowAtIndex:row];
    if (rowData == nil || column < 0 || (NSUInteger)column >= rowData.count){
        return nil;
    }
    return rowData[(NSUInteger)column];
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
        // 列宽不够时徽章文字会被截断，悬停显示完整取值。
        cell.toolTip = value;
        cell.label.toolTip = value;
        return cell;
    }

    SPTableTextCell *cell = [tableView makeViewWithIdentifier:@"TextCell" owner:self];
    if (cell == nil){
        cell = [[SPTableTextCell alloc] initWithFrame:NSMakeRect(0, 0, 120, 24)];
        cell.identifier = @"TextCell";
    }
    cell.label.stringValue = value;
    // 窄窗口下日期等长文本会显示省略号，悬停时给出完整内容。
    cell.toolTip = value;
    cell.label.toolTip = value;
    return cell;
}

@end
