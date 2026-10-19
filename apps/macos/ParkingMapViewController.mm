#import "ParkingMapViewController.h"
#import "ParkingMapView.h"

#import "bridge/ParkingBridge.h"

#include <optional>
#include <string>

NSNotificationName const SmartParkFindCarRequestNotification = @"SmartParkFindCarRequestNotification";
NSString *const SmartParkFindCarPlateKey = @"plate";

@implementation ParkingMapViewController{
    ParkingMapView *_mapView;
    NSComboBox *_plateCombo;
    NSTextField *_routeStatus;
}

- (instancetype)init{
    self = [super init];
    if (self != nil){
        // 仪表盘弹窗确认后只发通知，不用把 page 与 payload 一起塞进快捷动作回调。
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(handleFindCarRequest:)
                                                     name:SmartParkFindCarRequestNotification
                                                   object:nil];
    }
    return self;
}

- (void)dealloc{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)loadView{
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];

    // 图例：延迟锁位期间车位本身还是空闲，必须说明虚线框是什么意思。
    NSTextField *legend = [NSTextField wrappingLabelWithString:
        @"橙色框 = 已被预约（预约创建即锁位）；悬停可看预约车牌与时段。"];
    legend.textColor = [NSColor secondaryLabelColor];
    legend.font = [NSFont systemFontOfSize:13];
    legend.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:legend];

    // 反向寻车：输入或选择在场车牌 → 灰色是入口到车位的步行路线，蓝色是车位到出口。
    NSTextField *finderTitle = [NSTextField labelWithString:@"反向寻车"];
    finderTitle.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];

    _plateCombo = [[NSComboBox alloc] init];
    _plateCombo.translatesAutoresizingMaskIntoConstraints = NO;
    _plateCombo.placeholderString = @"输入或选择在场车牌，例如 晋A12345";
    _plateCombo.completes = YES;
    _plateCombo.numberOfVisibleItems = 8;
    _plateCombo.controlSize = NSControlSizeRegular;
    _plateCombo.font = [NSFont systemFontOfSize:13];
    _plateCombo.target = self;
    _plateCombo.action = @selector(beginCarFinding:);
    [_plateCombo.widthAnchor constraintGreaterThanOrEqualToConstant:220].active = YES;

    NSButton *findButton = [NSButton buttonWithTitle:@"开始寻车"
                                              target:self
                                              action:@selector(beginCarFinding:)];
    findButton.bezelStyle = NSBezelStyleRounded;
    NSButton *clearButton = [NSButton buttonWithTitle:@"清除路线"
                                               target:self
                                               action:@selector(clearCarFinding:)];
    clearButton.bezelStyle = NSBezelStyleRounded;

    NSStackView *finderRow = [NSStackView stackViewWithViews:@[
        finderTitle, _plateCombo, findButton, clearButton,
    ]];
    finderRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    finderRow.alignment = NSLayoutAttributeCenterY;
    finderRow.spacing = 8.0;
    finderRow.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:finderRow];

    _routeStatus = [NSTextField labelWithString:
        @"选一个在场车牌：灰色 = 入口到车位，蓝色 = 车位到最近出口。"];
    _routeStatus.font = [NSFont systemFontOfSize:12];
    _routeStatus.textColor = [NSColor secondaryLabelColor];
    _routeStatus.lineBreakMode = NSLineBreakByTruncatingTail;
    _routeStatus.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_routeStatus];

    _mapView = [[ParkingMapView alloc] initWithFrame:NSMakeRect(0, 0, 800, 500)];
    _mapView.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_mapView];

    [NSLayoutConstraint activateConstraints:@[
        // 标题已交给窗口工具栏（原生）显示，内容从安全区（栏下方）开始。
        [legend.topAnchor constraintEqualToAnchor:root.safeAreaLayoutGuide.topAnchor constant:18],
        [legend.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [legend.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [finderRow.topAnchor constraintEqualToAnchor:legend.bottomAnchor constant:10],
        [finderRow.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [finderRow.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-24],
        [_routeStatus.topAnchor constraintEqualToAnchor:finderRow.bottomAnchor constant:6],
        [_routeStatus.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [_routeStatus.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [_mapView.topAnchor constraintEqualToAnchor:_routeStatus.bottomAnchor constant:10],
        [_mapView.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [_mapView.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [_mapView.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-24],
    ]];

    self.view = root;
}

- (void)viewDidLoad{
    [super viewDidLoad];
    _mapView.bridge = self.bridge;
    [self reloadPlates];
    [_mapView setNeedsDisplay:YES];
}

- (void)refresh{
    [self reloadPlates];
    [_mapView setNeedsDisplay:YES];
}

- (void)handleFindCarRequest:(NSNotification *)notification{
    NSString *plate = notification.userInfo[SmartParkFindCarPlateKey];
    if (![plate isKindOfClass:[NSString class]] || plate.length == 0){
        return;
    }
    [self findRouteForPlate:plate];
}

- (void)findRouteForPlate:(NSString *)plate{
    (void)self.view;   // 快捷动作先切页、再发通知，这里保证视图与地图已经建好
    [self reloadPlates];
    _plateCombo.stringValue = plate;
    [self beginCarFinding:nil];
}

// 在下拉里列出当前在场车牌；保留用户已经输入/选择的文字，避免刷新把它清掉。
- (void)reloadPlates{
    if (self.bridge == nullptr){
        return;
    }
    NSString *current = _plateCombo.stringValue;
    [_plateCombo removeAllItems];
    for (const std::string &plate : self.bridge->parkedPlateNumbers()){
        [_plateCombo addItemWithObjectValue:[NSString stringWithUTF8String:plate.c_str()]];
    }
    if (current.length > 0){
        _plateCombo.stringValue = current;
    }
}

- (void)beginCarFinding:(id)sender{
    (void)sender;
    if (self.bridge == nullptr){
        return;
    }
    NSString *plate = [_plateCombo.stringValue stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (plate.length == 0){
        NSBeep();
        _routeStatus.stringValue = @"请先输入或选择一个在场车牌。";
        return;
    }
    std::optional<ParkingBridge::CarFinderData> found =
        self.bridge->findCarForPlate(std::string(plate.UTF8String));
    if (!found.has_value()){
        // 车牌按录入原样存库，大小写不一致时再用全大写试一次。
        const std::string upper = std::string([plate uppercaseString].UTF8String);
        if (upper != std::string(plate.UTF8String)){
            found = self.bridge->findCarForPlate(upper);
        }
    }
    if (!found.has_value()){
        NSBeep();
        [_mapView clearWalkRoute];
        _routeStatus.stringValue = [NSString stringWithFormat:
            @"未找到在场车辆「%@」：请确认车牌，或该车已经出库。", plate];
        return;
    }

    ParkingWalkRoute route;
    route.entryPoints = found->entryToCar.points;
    route.exitPoints = found->carToExit.points;
    route.spotId = found->spotId;
    route.target = found->target;
    [_mapView setWalkRoute:route];

    NSMutableArray<NSString *> *legs = [NSMutableArray array];
    if (!found->entryToCar.points.empty()){
        [legs addObject:[NSString stringWithFormat:@"%@→车位 步行 %.1f 米（%d 个转弯）",
            [NSString stringWithUTF8String:found->entranceName.c_str()],
            found->entryToCar.distance,
            found->entryToCar.turnCount]];
    }
    if (!found->carToExit.points.empty()){
        [legs addObject:[NSString stringWithFormat:@"车位→%@ 步行 %.1f 米（%d 个转弯）",
            [NSString stringWithUTF8String:found->exitName.c_str()],
            found->carToExit.distance,
            found->carToExit.turnCount]];
    }
    _routeStatus.stringValue = [NSString stringWithFormat:@"%@ 停在 %@（%@ 区）｜%@。",
        [NSString stringWithUTF8String:found->plateNumber.c_str()],
        [NSString stringWithUTF8String:found->spotId.c_str()],
        [NSString stringWithUTF8String:found->zone.c_str()],
        [legs componentsJoinedByString:@" · "]];
}

- (void)clearCarFinding:(id)sender{
    (void)sender;
    [_mapView clearWalkRoute];
    // 图上还有一层「最近一次规划的入场/出场路线」（蓝实线 + 橙虚线），
    // 光清步行路线会留下它，所以这里一起清掉。
    if (self.bridge != nullptr){
        self.bridge->clearPlannedRoute();
    }
    [_mapView setNeedsDisplay:YES];
    _routeStatus.stringValue = @"已清除路线。";
}

@end
