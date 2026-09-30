#import "SidebarViewController.h"

@interface SidebarViewController () <NSTableViewDataSource, NSTableViewDelegate>
@property (nonatomic, strong) NSTableView *tableView;
@property (nonatomic, copy) NSArray<NSString *> *items;
@end

@implementation SidebarViewController

- (void)loadView{
    NSVisualEffectView *effect =
        [[NSVisualEffectView alloc] initWithFrame:NSMakeRect(0, 0, 200, 600)];
    effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    effect.material = NSVisualEffectMaterialSidebar;
    effect.state = NSVisualEffectStateActive;
    self.view = effect;

    self.items = @[
        @"仪表盘", @"车位地图", @"车辆作业", @"当前车位",
        @"预约管理", @"停车记录", @"设施配置"
    ];

    NSScrollView *scroll =
        [[NSScrollView alloc] initWithFrame:self.view.bounds];
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;

    self.tableView = [[NSTableView alloc] initWithFrame:scroll.contentView.bounds];
    self.tableView.dataSource = self;
    self.tableView.delegate = self;
    self.tableView.headerView = nil;
    self.tableView.backgroundColor = [NSColor clearColor];
    self.tableView.rowHeight = 40.0;

    NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:@"nav"];
    column.width = 200.0;
    [self.tableView addTableColumn:column];

    scroll.documentView = self.tableView;
    [self.view addSubview:scroll];

    // 默认选中第一项（仪表盘）。
    [self.tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:0]
                byExtendingSelection:NO];
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification{
    NSInteger row = self.tableView.selectedRow;
    if (row < 0){
        return;
    }
    if ([self.delegate respondsToSelector:@selector(sidebar:didSelectIndex:)]){
        [self.delegate sidebar:self didSelectIndex:row];
    }
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView{
    return (NSInteger)self.items.count;
}

- (NSView *)tableView:(NSTableView *)tableView
   viewForTableColumn:(NSTableColumn *)tableColumn
                  row:(NSInteger)row{
    NSTextField *cell = [tableView makeViewWithIdentifier:@"NavCell" owner:self];
    if (cell == nil){
        cell = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 200, 40)];
        cell.identifier = @"NavCell";
        cell.bordered = NO;
        cell.editable = NO;
        cell.selectable = NO;
        cell.drawsBackground = NO;
        cell.textColor = [NSColor labelColor];
        cell.font = [NSFont systemFontOfSize:14];
    }
    cell.stringValue = self.items[(NSUInteger)row];
    return cell;
}

@end
