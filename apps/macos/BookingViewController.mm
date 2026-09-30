#import "BookingViewController.h"

#import "TableView.h"
#import "TextUtil.h"
#import "bridge/ParkingBridge.h"

#include <chrono>

@implementation BookingViewController{
    NSTextField *_plateField;
    NSPopUpButton *_typePopup;
    NSDatePicker *_arrivalPicker;
    TableView *_table;
    NSTextField *_statusLabel;
    NSTextField *_policyLabel;
}

- (void)loadView{
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];

    NSTextField *title = [NSTextField labelWithString:@"预约管理"];
    title.font = [NSFont systemFontOfSize:26 weight:NSFontWeightSemibold];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:title];

    // 表单卡片
    NSBox *form = [[NSBox alloc] init];
    form.boxType = NSBoxCustom;
    form.cornerRadius = 12.0;
    form.fillColor = [NSColor colorWithSRGBRed:1 green:1 blue:1 alpha:0.05];
    form.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:form];

    _plateField = [[NSTextField alloc] init];
    _plateField.placeholderString = @"预约车牌";
    _typePopup = [[NSPopUpButton alloc] init];
    [_typePopup addItemsWithTitles:@[@"轿车", @"摩托车", @"卡车", @"电动车"]];
    _arrivalPicker = [self datePicker];
    _arrivalPicker.dateValue = [NSDate dateWithTimeIntervalSinceNow:3600];

    NSButton *bookButton = [NSButton buttonWithTitle:@"预约" target:self action:@selector(book:)];
    NSButton *checkInButton = [NSButton buttonWithTitle:@"到场确认" target:self action:@selector(checkIn:)];
    NSButton *cancelButton = [NSButton buttonWithTitle:@"取消预约" target:self action:@selector(cancel:)];

    NSStackView *formRow = [[NSStackView alloc] init];
    formRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    formRow.spacing = 10.0;
    formRow.translatesAutoresizingMaskIntoConstraints = NO;
    [formRow addArrangedSubview:[NSTextField labelWithString:@"车牌"]];
    [formRow addArrangedSubview:_plateField];
    [formRow addArrangedSubview:[NSTextField labelWithString:@"车型"]];
    [formRow addArrangedSubview:_typePopup];
    [formRow addArrangedSubview:[NSTextField labelWithString:@"到场时间"]];
    [formRow addArrangedSubview:_arrivalPicker];
    [formRow addArrangedSubview:bookButton];
    [formRow addArrangedSubview:checkInButton];
    [formRow addArrangedSubview:cancelButton];
    [_plateField.widthAnchor constraintEqualToConstant:130].active = YES;
    [form addSubview:formRow];

    _policyLabel = [NSTextField wrappingLabelWithString:@""];
    _policyLabel.textColor = [NSColor secondaryLabelColor];
    _policyLabel.font = [NSFont systemFontOfSize:12];
    _policyLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [form addSubview:_policyLabel];

    [NSLayoutConstraint activateConstraints:@[
        [formRow.topAnchor constraintEqualToAnchor:form.topAnchor constant:14],
        [formRow.leadingAnchor constraintEqualToAnchor:form.leadingAnchor constant:14],
        [formRow.trailingAnchor constraintEqualToAnchor:form.trailingAnchor constant:-14],
        [_policyLabel.topAnchor constraintEqualToAnchor:formRow.bottomAnchor constant:8],
        [_policyLabel.leadingAnchor constraintEqualToAnchor:form.leadingAnchor constant:14],
        [_policyLabel.trailingAnchor constraintEqualToAnchor:form.trailingAnchor constant:-14],
        [_policyLabel.bottomAnchor constraintEqualToAnchor:form.bottomAnchor constant:-12],
    ]];

    _table = [[TableView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    _table.translatesAutoresizingMaskIntoConstraints = NO;
    [_table setColumns:@[@"编号", @"车牌", @"车位", @"创建时间", @"到场时间",
                         @"宽限截止", @"定金(元)", @"状态"]];
    [root addSubview:_table];

    _statusLabel = [NSTextField wrappingLabelWithString:@""];
    _statusLabel.textColor = [NSColor labelColor];
    _statusLabel.font = [NSFont systemFontOfSize:12];
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_statusLabel];

    [NSLayoutConstraint activateConstraints:@[
        [title.topAnchor constraintEqualToAnchor:root.topAnchor constant:24],
        [title.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [form.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:14],
        [form.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [form.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [_table.topAnchor constraintEqualToAnchor:form.bottomAnchor constant:14],
        [_table.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [_table.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [_statusLabel.topAnchor constraintEqualToAnchor:_table.bottomAnchor constant:10],
        [_statusLabel.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [_statusLabel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [_statusLabel.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-16],
    ]];

    self.view = root;
}

- (void)viewDidLoad{
    [super viewDidLoad];
    [self refresh];
}

- (NSDatePicker *)datePicker{
    NSDatePicker *picker = [[NSDatePicker alloc] init];
    picker.datePickerStyle = NSDatePickerStyleTextFieldAndStepper;
    picker.datePickerElements = NSDatePickerElementFlagYearMonthDay | NSDatePickerElementFlagHourMinute;
    picker.datePickerMode = NSDatePickerModeSingle;
    [picker.widthAnchor constraintEqualToConstant:170].active = YES;
    return picker;
}

- (NSString *)plate{
    return [_plateField.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
}

- (void)book:(id)sender{
    NSString *plate = [self plate];
    if (plate.length == 0){
        _statusLabel.stringValue = @"预约前请输入车辆车牌。";
        return;
    }
    if ([_arrivalPicker.dateValue timeIntervalSinceNow] <= 0){
        _statusLabel.stringValue = @"预约到场时间必须在当前时间之后。";
        return;
    }
    int typeIndex = (int)_typePopup.indexOfSelectedItem;
    auto arrival = smartpark::ParkingRecord::TimePoint(
        std::chrono::seconds((long)[_arrivalPicker.dateValue timeIntervalSince1970]));
    auto result = self.bridge->bookVehicle(plate.UTF8String,
        smartpark_ui::vehicleTypeFromIndex(typeIndex), arrival);
    _statusLabel.stringValue = result
        ? [NSString stringWithFormat:@"预约成功：%@ → 车位 %@", plate,
            [NSString stringWithUTF8String:result->booking.spotId().c_str()]]
        : [NSString stringWithFormat:@"预约失败：车牌 %@ 无法预约。", plate];
    // 预约会占用/释放预留车位并产生定金，广播给所有页面统一刷新
    // （通知总线会回调本页 refresh，无需再单独调用）。
    [[NSNotificationCenter defaultCenter] postNotificationName:@"SmartParkDataChanged"
                                                        object:nil];
}

- (void)checkIn:(id)sender{
    NSString *plate = [self plate];
    if (plate.length == 0){
        _statusLabel.stringValue = @"到场确认前请输入预约车牌。";
        return;
    }
    auto result = self.bridge->confirmBooking(plate.UTF8String);
    _statusLabel.stringValue = result
        ? [NSString stringWithFormat:@"到场确认成功：%@ → 车位 %@", plate,
            [NSString stringWithUTF8String:result->spotId.c_str()]]
        : [NSString stringWithFormat:@"车牌 %@ 没有可确认的预约。", plate];
    // 到场确认会把预留车位转为占用并新建停车记录，必须让仪表盘/地图同步。
    [[NSNotificationCenter defaultCenter] postNotificationName:@"SmartParkDataChanged"
                                                        object:nil];
}

- (void)cancel:(id)sender{
    NSString *plate = [self plate];
    if (plate.length == 0){
        _statusLabel.stringValue = @"取消预约前请输入预约车牌。";
        return;
    }
    bool ok = self.bridge->cancelBooking(plate.UTF8String);
    _statusLabel.stringValue = ok
        ? [NSString stringWithFormat:@"取消成功：%@ 预约已取消。", plate]
        : [NSString stringWithFormat:@"车牌 %@ 没有可取消的预约。", plate];
    // 取消会释放预留车位，同样需要广播。
    [[NSNotificationCenter defaultCenter] postNotificationName:@"SmartParkDataChanged"
                                                        object:nil];
}

- (void)refresh{
    if (self.bridge == nullptr || !self.bridge->ready()){
        return;
    }
    auto policy = self.bridge->bookingPolicy();
    _policyLabel.stringValue = [NSString stringWithFormat:
        @"预约规则：定金 %.2f 元 | 最多提前 %d 天 | 到场宽限期 %lld 分钟。",
        policy.deposit, policy.advanceDays, (long long)policy.gracePeriod.count()];

    NSMutableArray<NSArray<NSString *> *> *rows = [NSMutableArray array];
    for (const smartpark::Booking &booking : self.bridge->bookings()){
        [rows addObject:@[
            [NSString stringWithUTF8String:booking.id().c_str()],
            [NSString stringWithUTF8String:booking.plateNumber().c_str()],
            [NSString stringWithUTF8String:booking.spotId().c_str()],
            [NSString stringWithUTF8String:smartpark_ui::formatTime(booking.createdAt()).c_str()],
            [NSString stringWithUTF8String:smartpark_ui::formatTime(booking.arrivalTime()).c_str()],
            [NSString stringWithUTF8String:smartpark_ui::formatTime(booking.arrivalDeadline()).c_str()],
            [NSString stringWithFormat:@"%.2f", booking.deposit()],
            [NSString stringWithUTF8String:smartpark_ui::bookingStatusText(booking.status())],
        ]];
    }
    [_table setRows:rows];
}

@end
