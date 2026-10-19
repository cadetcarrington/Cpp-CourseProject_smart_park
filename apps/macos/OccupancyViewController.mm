#import "OccupancyViewController.h"

#import "TableView.h"
#import "TextUtil.h"
#import "bridge/ParkingBridge.h"

#include <string>
#include <vector>

namespace{

// 状态徽章配色：空闲绿、占用红、预订橙、停用灰。
NSColor *statusTint(NSString *statusText){
    if ([statusText isEqualToString:@"空闲"]){
        return [NSColor systemGreenColor];
    }
    if ([statusText isEqualToString:@"占用"]){
        return [NSColor systemRedColor];
    }
    if ([statusText isEqualToString:@"预订"]){
        return [NSColor systemOrangeColor];
    }
    if ([statusText isEqualToString:@"已预约"]){
        return [NSColor systemOrangeColor];   // 已预约、尚未锁位
    }
    return [NSColor systemGrayColor];   // 停用
}

// 页面内容宽度小于该值时只显示一张表（单列、覆盖全部车位），否则左右两列
// 各显示一半。阈值取 1000pt：窗口默认 1120pt 宽时内容区约 900–950pt，仍是双表；
// 窗口缩到最小 960pt（内容区约 700–800pt）时切到单表，避免两张表都把
// 「车位 / 类型」「预约时段」压成省略号。
const CGFloat kOccupancySingleTableWidthThreshold = 1000.0;

} // namespace

@implementation OccupancyViewController{
    TableView *_table;        // 宽窗口：左列前半；窄窗口：唯一一张表（全部车位）
    TableView *_secondTable;  // 宽窗口：右列后半；窄窗口：隐藏
    NSArray<NSString *> *_plates;
    // refresh 生成的完整行数据（含所有车位）；宽/窄两种模式下按当前模式切分。
    NSArray<NSArray<NSString *> *> *_allRows;
    BOOL _singleTableMode;
    BOOL _modeInitialized;
}

- (void)loadView{
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];


    NSTextField *hint = [NSTextField wrappingLabelWithString:
        @"选择一条“占用”记录后，可前往“车辆出入库”点击“车辆出库”；车牌和车辆类型随状态实时刷新。"
         "“已预约”表示该车位已被预约并已锁位（创建预约即锁定），"
         "车牌列是预约车牌，“预约时段”列是订单时间段。"];
    hint.textColor = [NSColor secondaryLabelColor];
    hint.font = [NSFont systemFontOfSize:13];
    hint.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:hint];

    _table = [[TableView alloc] initWithFrame:NSMakeRect(0, 0, 800, 440)];
    _table.translatesAutoresizingMaskIntoConstraints = NO;
    _secondTable = [[TableView alloc] initWithFrame:NSMakeRect(0, 0, 800, 440)];
    _secondTable.translatesAutoresizingMaskIntoConstraints = NO;

    // 车位与类型合并为一栏，状态紧随其后并以胶囊徽章呈现。
    // 75 个车位单列要滚动很久，因此宽窗口左右两列各显示一半，一屏能看到两倍
    // 的车位；窄窗口自动切回单列（见 kOccupancySingleTableWidthThreshold）。
    NSArray<NSString *> *columns = @[@"车位 / 类型", @"状态", @"车牌", @"车辆类型", @"预约时段"];
    NSColor *(^tintBlock)(NSString *) = ^NSColor *(NSString *value){
        return statusTint(value);
    };
    for (TableView *table in @[_table, _secondTable]){
        [table setColumns:columns];
        [table setBadgeColumn:1 tintBlock:tintBlock];
        // 每列给足最小宽度：表宽不够时横向滚动，而不是把预约时段压成省略号。
        // 必须在 setColumns: 之后调用。
        [table setMinimumColumnWidths:@[@96, @72, @84, @80, @112]];
    }
    __weak OccupancyViewController *weakSelf = self;
    _table.selectionHandler = ^(NSInteger row){
        [weakSelf selectRow:row offset:0];
    };
    _secondTable.selectionHandler = ^(NSInteger row){
        OccupancyViewController *controller = weakSelf;
        if (controller != nil){
            [controller selectRow:row offset:(controller->_plates.count + 1) / 2];
        }
    };

    NSStackView *columnsRow = [NSStackView stackViewWithViews:@[_table, _secondTable]];
    columnsRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    columnsRow.distribution = NSStackViewDistributionFillEqually;
    columnsRow.spacing = 16.0;
    // 窄窗口隐藏右表后，栈视图让左表独占整行。
    columnsRow.detachesHiddenViews = YES;
    columnsRow.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:columnsRow];

    [NSLayoutConstraint activateConstraints:@[
        // 标题已交给窗口工具栏（原生）显示，内容从安全区（栏下方）开始。
        [hint.topAnchor constraintEqualToAnchor:root.safeAreaLayoutGuide.topAnchor constant:18],
        [hint.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [hint.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [columnsRow.topAnchor constraintEqualToAnchor:hint.bottomAnchor constant:14],
        [columnsRow.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [columnsRow.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [columnsRow.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-24],
    ]];

    self.view = root;
}

- (void)viewDidLoad{
    [super viewDidLoad];
    [self refresh];
}

// 尺寸变化时重新决定单表/双表；行号映射随之变化，因此切换模式要清掉旧选中。
- (void)viewDidLayout{
    [super viewDidLayout];
    const CGFloat width = NSWidth(self.view.bounds);
    if (width <= 0.0){
        return;
    }
    const BOOL single = width < kOccupancySingleTableWidthThreshold;
    if (_modeInitialized && single == _singleTableMode){
        return;
    }
    _modeInitialized = YES;
    _singleTableMode = single;
    _secondTable.hidden = single;
    [_table.tableView deselectAll:nil];
    [_secondTable.tableView deselectAll:nil];
    [self applyRowsForCurrentMode];
}

// 按当前模式把完整行数据切给一张表或两张表。
- (void)applyRowsForCurrentMode{
    if (_allRows == nil){
        return;
    }
    if (_singleTableMode){
        // 窄窗口：一张表显示全部车位，选中偏移为 0。
        [_table setRows:_allRows];
        [_secondTable setRows:@[]];
        return;
    }
    // 宽窗口：前一半在左列、后一半在右列，两列各自独立滚动。
    const NSUInteger split = (_allRows.count + 1) / 2;
    [_table setRows:[_allRows subarrayWithRange:NSMakeRange(0, split)]];
    [_secondTable setRows:[_allRows subarrayWithRange:
        NSMakeRange(split, _allRows.count - split)]];
}

- (void)selectRow:(NSInteger)row offset:(NSUInteger)offset{
    if (row < 0 || offset + (NSUInteger)row >= _plates.count){
        return;
    }
    NSString *plate = _plates[offset + (NSUInteger)row];
    if (![plate isEqualToString:@"-"]){
        [[NSNotificationCenter defaultCenter] postNotificationName:@"SmartParkVehicleSelected"
                                                    object:nil userInfo:@{@"plate": plate}];
    }
}

- (void)refresh{
    if (self.bridge == nullptr || !self.bridge->ready()){
        return;
    }
    // 预约创建即锁位：车位状态虽然已是「预订」，但订单上的车牌、车型、时段
    // 比车位本身更完整。这里按 spotId 把未到场的时段预约并进来，状态列统一
    // 显示「已预约」，车牌与车型字段取订单上的值（与页面提示一致）。
    const std::vector<smartpark::Reservation> &reservations =
        self.bridge->reservations();
    auto reservationFor = [&reservations](const std::string &spotId)
        -> const smartpark::Reservation *{
        for (const smartpark::Reservation &item : reservations){
            if (item.spotId() != spotId){
                continue;
            }
            // 已到场/已取消的订单不算：到场的车辆会在车位对象上，取消的车位早释放。
            if (item.status() != smartpark::ReservationStatus::Confirmed
                && item.status() != smartpark::ReservationStatus::PendingPayment){
                continue;
            }
            return &item;
        }
        return nullptr;
    };
    auto clockText = [](smartpark::Reservation::TimePoint time){
        return smartpark_ui::formatTime(
            smartpark::ParkingRecord::TimePoint(time.time_since_epoch()));
    };

    NSMutableArray<NSArray<NSString *> *> *rows = [NSMutableArray array];
    NSMutableArray<NSString *> *plates = [NSMutableArray array];
    for (const smartpark::ParkingSpot &spot : self.bridge->spots()){
        const smartpark::Reservation *reserved = reservationFor(spot.identifier());
        NSString *plate = spot.parkedVehicle()
            ? smartpark_ui::toNSString(spot.parkedVehicle()->plateNumber())
            : (reserved != nullptr
                   ? smartpark_ui::toNSString(reserved->plateNumber())
                   : @"-");
        [plates addObject:plate];
        NSString *vehicle = spot.parkedVehicle()
            ? smartpark_ui::toNSString(smartpark_ui::vehicleTypeText(spot.parkedVehicle()->type()))
            : (reserved != nullptr
                   ? smartpark_ui::toNSString(smartpark_ui::vehicleTypeText(reserved->vehicleType()))
                   : @"-");
        NSString *status = reserved != nullptr
            ? @"已预约"
            : smartpark_ui::toNSString(smartpark_ui::statusText(spot.status()));
        NSString *window = @"-";
        if (reserved != nullptr){
            NSString *from = smartpark_ui::toNSString(clockText(reserved->startTime()));
            NSString *to = smartpark_ui::toNSString(clockText(reserved->endTime()));
            window = [NSString stringWithFormat:@"%@ – %@",
                      [from substringFromIndex:MAX(0, (NSInteger)from.length - 5)],
                      [to substringFromIndex:MAX(0, (NSInteger)to.length - 5)]];
        }
        [rows addObject:@[
            [NSString stringWithFormat:@"%@ · %@",
                smartpark_ui::toNSString(spot.identifier()),
                smartpark_ui::toNSString(smartpark_ui::spotTypeText(spot.type()))],
            status,
            plate,
            vehicle,
            window,
        ]];
    }
    _plates = [plates copy];
    _allRows = [rows copy];
    [self applyRowsForCurrentMode];
}

@end
