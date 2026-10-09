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

    _mapView = [[ParkingMapView alloc] initWithFrame:NSMakeRect(0, 0, 800, 500)];
    _mapView.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_mapView];

    [NSLayoutConstraint activateConstraints:@[
        [title.topAnchor constraintEqualToAnchor:root.topAnchor constant:24],
        [title.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [_mapView.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:16],
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
