#import "SidebarViewController.h"

@interface SidebarViewController () <NSTableViewDataSource, NSTableViewDelegate>
@property (nonatomic, strong) NSTableView *tableView;
@property (nonatomic, copy) NSArray<NSString *> *items;
@property (nonatomic, strong) NSTextField *userLabel;
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

    // 底部账号区：当前登录账号 + 退出登录（对齐 Qt 版顶栏的「管理员：X / 退出登录」）。
    NSBox *separator = [[NSBox alloc] init];
    separator.boxType = NSBoxSeparator;

    self.userLabel = [NSTextField wrappingLabelWithString:@""];
    self.userLabel.font = [NSFont systemFontOfSize:11];
    self.userLabel.textColor = [NSColor secondaryLabelColor];

    NSButton *logoutButton = [NSButton buttonWithTitle:@"退出登录"
                                                target:self
                                                action:@selector(requestLogout:)];
    logoutButton.bezelStyle = NSBezelStyleRounded;
    logoutButton.controlSize = NSControlSizeSmall;
    logoutButton.font = [NSFont systemFontOfSize:11];
    logoutButton.accessibilityIdentifier = @"smartpark.logout";

    NSStackView *footer = [NSStackView stackViewWithViews:@[
        separator, self.userLabel, logoutButton
    ]];
    footer.orientation = NSUserInterfaceLayoutOrientationVertical;
    footer.alignment = NSLayoutAttributeLeading;
    footer.spacing = 6.0;
    footer.edgeInsets = NSEdgeInsetsMake(8, 12, 10, 12);

    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    footer.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:scroll];
    [self.view addSubview:footer];

    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [scroll.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:footer.topAnchor],

        [footer.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [footer.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [footer.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],

        [separator.widthAnchor constraintEqualToAnchor:footer.widthAnchor
                                              constant:-24],
        [self.userLabel.widthAnchor constraintEqualToAnchor:footer.widthAnchor
                                                   constant:-24],
    ]];

    // userName 可能在 loadView 之前就被赋值（MainWindowController 在 init 里设置），
    // 那时 userLabel 还不存在，所以在控件建好之后补一次，否则底部会一直空着。
    [self setUserName:_userName];

    // 默认选中第一项（仪表盘）。
    [self.tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:0]
                byExtendingSelection:NO];
}

- (void)setUserName:(NSString *)userName{
    _userName = [userName copy];
    self.userLabel.stringValue = userName.length > 0
        ? [NSString stringWithFormat:@"管理员：%@", userName]
        : @"未登录";
}

- (void)selectIndex:(NSInteger)index{
    if (index < 0 || index >= (NSInteger)self.items.count){
        return;
    }
    [self.tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)index]
                byExtendingSelection:NO];
    [self.tableView scrollRowToVisible:index];
}

- (void)requestLogout:(id)sender{
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"退出登录";
    alert.informativeText = @"确认退出当前管理员会话并返回登录界面吗？";
    [alert addButtonWithTitle:@"退出登录"];
    [alert addButtonWithTitle:@"取消"];
    alert.alertStyle = NSAlertStyleWarning;

    NSWindow *window = self.view.window;
    void (^proceed)(NSModalResponse) = ^(NSModalResponse response){
        if (response != NSAlertFirstButtonReturn){
            return;
        }
        void (^handler)(void) = self.logoutHandler;
        if (handler != nil){
            handler();
        }
    };
    if (window != nil){
        [alert beginSheetModalForWindow:window completionHandler:proceed];
        return;
    }
    proceed([alert runModal]);
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
    // 用容器 + 显式 centerY 约束保证文字在行内垂直居中。
    // 直接把 NSTextField 当单元格时，单元格默认 wraps=YES，文字会贴顶显示。
    NSView *cell = [tableView makeViewWithIdentifier:@"NavCell" owner:self];
    NSTextField *label = nil;
    if (cell == nil){
        cell = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 200, 40)];
        cell.identifier = @"NavCell";

        label = [NSTextField labelWithString:@""];
        label.font = [NSFont systemFontOfSize:14];
        label.textColor = [NSColor labelColor];
        label.lineBreakMode = NSLineBreakByTruncatingTail;
        label.translatesAutoresizingMaskIntoConstraints = NO;
        [cell addSubview:label];

        [NSLayoutConstraint activateConstraints:@[
            [label.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:14],
            [label.trailingAnchor constraintLessThanOrEqualToAnchor:cell.trailingAnchor
                                                           constant:-8],
            [label.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        ]];
    } else{
        label = cell.subviews.firstObject;
    }
    label.stringValue = self.items[(NSUInteger)row];
    return cell;
}

@end
