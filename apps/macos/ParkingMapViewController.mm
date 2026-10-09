#import "ParkingMapViewController.h"
#import "ParkingMapView.h"

@implementation ParkingMapViewController{
    ParkingMapView *_mapView;
}

- (void)loadView{
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];

    NSTextField *title = [NSTextField labelWithString:@"车位地图"];
    title.font = [NSFont systemFontOfSize:26 weight:NSFontWeightSemibold];
    title.textColor = [NSColor labelColor];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:title];

    // 图例：延迟锁位期间车位本身还是空闲，必须说明虚线框是什么意思。
    NSTextField *legend = [NSTextField wrappingLabelWithString:
        @"橙色虚线框 = 已被预约、尚未锁位（开始前 30 分钟才锁定车位）；实心橙 = 已锁位的预订；悬停可看预约车牌与时段。"];
    legend.textColor = [NSColor secondaryLabelColor];
    legend.font = [NSFont systemFontOfSize:12];
    legend.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:legend];

    _mapView = [[ParkingMapView alloc] initWithFrame:NSMakeRect(0, 0, 800, 500)];
    _mapView.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_mapView];

    [NSLayoutConstraint activateConstraints:@[
        [title.topAnchor constraintEqualToAnchor:root.topAnchor constant:24],
        [title.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [legend.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:6],
        [legend.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [legend.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [_mapView.topAnchor constraintEqualToAnchor:legend.bottomAnchor constant:10],
        [_mapView.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [_mapView.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [_mapView.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-24],
    ]];

    self.view = root;
}

- (void)viewDidLoad{
    [super viewDidLoad];
    _mapView.bridge = self.bridge;
    [_mapView setNeedsDisplay:YES];
}

- (void)refresh{
    [_mapView setNeedsDisplay:YES];
}

@end
