#import "OccupancyViewController.h"

#import "TableView.h"
#import "TextUtil.h"
#import "bridge/ParkingBridge.h"

@implementation OccupancyViewController{
    TableView *_table;
}

- (void)loadView{
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];

    NSTextField *title = [NSTextField labelWithString:@"当前车位"];
    title.font = [NSFont systemFontOfSize:26 weight:NSFontWeightSemibold];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:title];

    NSTextField *hint = [NSTextField wrappingLabelWithString:
        @"选择一条“占用”记录后，可前往“车辆作业”点击“车辆出库”；车牌和车辆类型随状态实时刷新。"];
    hint.textColor = [NSColor secondaryLabelColor];
    hint.font = [NSFont systemFontOfSize:12];
    hint.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:hint];

    _table = [[TableView alloc] initWithFrame:NSMakeRect(0, 0, 800, 440)];
    _table.translatesAutoresizingMaskIntoConstraints = NO;
    [_table setColumns:@[@"车位", @"类型", @"状态", @"车牌", @"车辆类型"]];
    [root addSubview:_table];

    [NSLayoutConstraint activateConstraints:@[
        [title.topAnchor constraintEqualToAnchor:root.topAnchor constant:24],
        [title.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [hint.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:10],
        [hint.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [hint.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [_table.topAnchor constraintEqualToAnchor:hint.bottomAnchor constant:14],
        [_table.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [_table.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [_table.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-24],
    ]];

    self.view = root;
}

- (void)viewDidLoad{
    [super viewDidLoad];
    [self refresh];
}

- (void)refresh{
    if (self.bridge == nullptr || !self.bridge->ready()){
        return;
    }
    NSMutableArray<NSArray<NSString *> *> *rows = [NSMutableArray array];
    for (const smartpark::ParkingSpot &spot : self.bridge->spots()){
        NSString *plate = spot.parkedVehicle()
            ? [NSString stringWithUTF8String:spot.parkedVehicle()->plateNumber().c_str()] : @"-";
        NSString *vehicle = spot.parkedVehicle()
            ? [NSString stringWithUTF8String:smartpark_ui::vehicleTypeText(spot.parkedVehicle()->type())] : @"-";
        [rows addObject:@[
            [NSString stringWithUTF8String:spot.identifier().c_str()],
            [NSString stringWithUTF8String:smartpark_ui::spotTypeText(spot.type())],
            [NSString stringWithUTF8String:smartpark_ui::statusText(spot.status())],
            plate,
            vehicle,
        ]];
    }
    [_table setRows:rows];
}

@end
