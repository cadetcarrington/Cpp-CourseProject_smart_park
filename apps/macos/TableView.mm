#import "TableView.h"

@interface TableView () <NSTableViewDataSource, NSTableViewDelegate>
@property (nonatomic, strong) NSTableView *tableView;
@property (nonatomic, copy) NSArray<NSString *> *columns;
@property (nonatomic, copy) NSArray<NSArray<NSString *> *> *rows;
@end

@implementation TableView

- (instancetype)initWithFrame:(NSRect)frameRect{
    if ((self = [super initWithFrame:frameRect])){
        _columns = @[];
        _rows = @[];

        NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:self.bounds];
        scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        scroll.hasVerticalScroller = YES;
        scroll.drawsBackground = NO;
        [self addSubview:scroll];

        _tableView = [[NSTableView alloc] initWithFrame:scroll.contentView.bounds];
        _tableView.dataSource = self;
        _tableView.delegate = self;
        _tableView.usesAlternatingRowBackgroundColors = YES;
        _tableView.rowHeight = 24.0;
        _tableView.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
        _tableView.allowsMultipleSelection = NO;
        scroll.documentView = _tableView;
    }
    return self;
}

- (void)setColumns:(NSArray<NSString *> *)columns{
    _columns = [columns copy];
    for (NSTableColumn *column in [_tableView.tableColumns copy]){
        [_tableView removeTableColumn:column];
    }
    for (NSString *title in columns){
        NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:title];
        column.title = title;
        column.width = 110.0;
        column.minWidth = 56.0;
        [_tableView addTableColumn:column];
    }
}

- (void)setRows:(NSArray<NSArray<NSString *> *> *)rows{
    _rows = [rows copy];
    [_tableView reloadData];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView{
    return (NSInteger)_rows.count;
}

- (NSView *)tableView:(NSTableView *)tableView
   viewForTableColumn:(NSTableColumn *)tableColumn
                  row:(NSInteger)row{
    NSTextField *cell = [tableView makeViewWithIdentifier:@"Cell" owner:self];
    if (cell == nil){
        cell = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 120, 0)];
        cell.identifier = @"Cell";
        cell.bordered = NO;
        cell.editable = NO;
        cell.selectable = YES;
        cell.drawsBackground = NO;
        cell.lineBreakMode = NSLineBreakByTruncatingTail;
        cell.font = [NSFont systemFontOfSize:11];
    }
    NSInteger columnIndex = [tableView.tableColumns indexOfObject:tableColumn];
    NSArray<NSString *> *rowData = _rows[(NSUInteger)row];
    cell.stringValue = (columnIndex >= 0 && columnIndex < (NSInteger)rowData.count)
        ? rowData[(NSUInteger)columnIndex] : @"";
    return cell;
}

@end
