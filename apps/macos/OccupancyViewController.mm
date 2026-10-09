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

} // namespace

@implementation OccupancyViewController{
    TableView *_table;        // 左列：车位前一半
    TableView *_secondTable;  // 右列：车位后一半
    NSArray<NSString *> *_plates;
}

- (void)loadView{
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];

    NSTextField *title = [NSTextField labelWithString:@"当前车位"];
    title.font = [NSFont systemFontOfSize:26 weight:NSFontWeightSemibold];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:title];

    NSTextField *hint = [NSTextField wrappingLabelWithString:
        @"选择一条“占用”记录后，可前往“车辆作业”点击“车辆出库”；车牌和车辆类型随状态实时刷新。"
         "“已预约”表示该车位已被预约但还没锁位（延迟锁位：开始前 30 分钟才锁定），"
         "车牌列是预约车牌，“预约时段”列是订单时间段。"];
    hint.textColor = [NSColor secondaryLabelColor];
    hint.font = [NSFont systemFontOfSize:12];
    hint.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:hint];

    _table = [[TableView alloc] initWithFrame:NSMakeRect(0, 0, 800, 440)];
    _table.translatesAutoresizingMaskIntoConstraints = NO;
    _secondTable = [[TableView alloc] initWithFrame:NSMakeRect(0, 0, 800, 440)];
    _secondTable.translatesAutoresizingMaskIntoConstraints = NO;

    // 车位与类型合并为一栏，状态紧随其后并以胶囊徽章呈现。
    // 75 个车位单列要滚动很久，因此左右两列各显示一半，一屏能看到两倍的车位。
    NSArray<NSString *> *columns = @[@"车位 / 类型", @"状态", @"车牌", @"车辆类型", @"预约时段"];
    NSColor *(^tintBlock)(NSString *) = ^NSColor *(NSString *value){
        return statusTint(value);
    };
    for (TableView *table in @[_table, _secondTable]){
        [table setColumns:columns];
        [table setBadgeColumn:1 tintBlock:tintBlock];
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
    columnsRow.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:columnsRow];

    [NSLayoutConstraint activateConstraints:@[
        [title.topAnchor constraintEqualToAnchor:root.topAnchor constant:24],
        [title.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [hint.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:10],
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
    // 延迟锁位期间车位状态还是「空闲」，光看车位状态看不出有预约：这里把
    // 「已预约（未锁位）」的订单并进来，状态、车牌、车型取预约订单上的值。
    const std::vector<ParkingBridge::PendingReservation> pending =
        self.bridge->pendingReservations();
    auto pendingFor = [&pending](const std::string &spotId)
        -> const ParkingBridge::PendingReservation *{
        for (const ParkingBridge::PendingReservation &item : pending){
            if (item.spotId == spotId){
                return &item;
            }
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
        const ParkingBridge::PendingReservation *reserved = pendingFor(spot.identifier());
        NSString *plate = spot.parkedVehicle()
            ? smartpark_ui::toNSString(spot.parkedVehicle()->plateNumber())
            : (reserved != nullptr ? smartpark_ui::toNSString(reserved->plate) : @"-");
        [plates addObject:plate];
        NSString *vehicle = spot.parkedVehicle()
            ? smartpark_ui::toNSString(smartpark_ui::vehicleTypeText(spot.parkedVehicle()->type()))
            : (reserved != nullptr
                   ? smartpark_ui::toNSString(smartpark_ui::vehicleTypeText(reserved->vehicleType))
                   : @"-");
        NSString *status = reserved != nullptr
            ? @"已预约"
            : smartpark_ui::toNSString(smartpark_ui::statusText(spot.status()));
        NSString *window = @"-";
        if (reserved != nullptr){
            NSString *from = smartpark_ui::toNSString(clockText(reserved->start));
            NSString *to = smartpark_ui::toNSString(clockText(reserved->end));
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
    // 前一半在左列、后一半在右列，两列各自独立滚动。
    _plates = [plates copy];
    const NSUInteger split = (rows.count + 1) / 2;
    [_table setRows:[rows subarrayWithRange:NSMakeRange(0, split)]];
    [_secondTable setRows:[rows subarrayWithRange:
        NSMakeRange(split, rows.count - split)]];
}

@end
