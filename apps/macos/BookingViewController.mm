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
        ? [NSString stringWithFormat:
            @"预约成功：%@ → 车位 %@，到场时间 %@；可在 %@ 至 %@ 之间点「到场确认」。",
            plate, smartpark_ui::toNSString(result->booking.spotId()),
            smartpark_ui::toNSString(smartpark_ui::formatTime(result->booking.arrivalTime())),
            smartpark_ui::toNSString(smartpark_ui::formatTime(
                result->booking.arrivalTime() - self.bridge->bookingPolicy().gracePeriod)),
            smartpark_ui::toNSString(smartpark_ui::formatTime(result->booking.arrivalDeadline()))]
        : [NSString stringWithFormat:
            @"预约失败：车牌 %@ 无法预约（可能已在场、已有预约、时间超出 %d 天或没有可用车位）。",
            plate, self.bridge->bookingPolicy().advanceDays];
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
    if (result){
        _statusLabel.stringValue = [NSString stringWithFormat:
            @"到场确认成功：%@ → 车位 %@，定金退回（离场时按计费规则结算）。",
            plate, smartpark_ui::toNSString(result->spotId)];
    } else{
        _statusLabel.stringValue = [self checkInFailureReasonForPlate:plate];
    }
    // 到场确认会把预留车位转为占用并新建停车记录，必须让仪表盘/地图同步。
    [[NSNotificationCenter defaultCenter] postNotificationName:@"SmartParkDataChanged"
                                                        object:nil];
}

// confirmBooking 只有成功/失败两种结果，失败原因要自己回查预约记录：
// 未预约 / 已取消 / 已爽约 / 还没到到场时间 / 已过宽限。
// 最常见的是「还没到到场时间」——预约表单默认把到场时间设在 1 小时后，
// 刚预约完就点「到场确认」必然落在这个分支，不能笼统说成「没有预约」。
- (NSString *)checkInFailureReasonForPlate:(NSString *)plate{
    if (self.bridge == nullptr || !self.bridge->ready()){
        return @"数据未就绪，无法确认到场。";
    }
    const std::string target(plate.UTF8String);
    const smartpark::Booking *latest = nullptr;
    for (const smartpark::Booking &booking : self.bridge->bookings()){
        if (booking.plateNumber() != target){
            continue;
        }
        if (latest == nullptr || booking.arrivalTime() > latest->arrivalTime()){
            latest = &booking;
        }
    }
    if (latest == nullptr){
        return [NSString stringWithFormat:@"车牌 %@ 没有预约记录，请先预约。", plate];
    }

    NSString *arrival = smartpark_ui::toNSString(
        smartpark_ui::formatTime(latest->arrivalTime()));
    NSString *deadline = smartpark_ui::toNSString(
        smartpark_ui::formatTime(latest->arrivalDeadline()));
    switch (latest->status()){
    case smartpark::BookingStatus::Cancelled:
        return [NSString stringWithFormat:@"车牌 %@ 的预约已被取消。", plate];
    case smartpark::BookingStatus::NoShow:
        return [NSString stringWithFormat:
            @"车牌 %@ 的预约已超过宽限截止 %@ 作废（爽约，定金已没收），请重新预约。",
            plate, deadline];
    case smartpark::BookingStatus::CheckedIn:
        return [NSString stringWithFormat:
            @"车牌 %@ 已到场，车辆正在场内；如需离场请到「车辆作业」办理出库。", plate];
    case smartpark::BookingStatus::Booked:
    default:
        break;
    }

    // 可确认窗口 = 到场时间前后各一个宽限期，与核心 confirmBooking 保持一致。
    const auto grace = self.bridge->bookingPolicy().gracePeriod;
    const auto now = smartpark::Booking::Clock::now();
    if (now < latest->arrivalTime() - grace){
        NSString *opens = smartpark_ui::toNSString(smartpark_ui::formatTime(
            latest->arrivalTime() - grace));
        return [NSString stringWithFormat:
            @"还没到可确认时间：%@ 的预约到场时间是 %@，可确认时段为 %@ 至 %@"
             "（到场前后各 %lld 分钟）；到点后再点「到场确认」。",
            plate, arrival, opens, deadline, (long long)grace.count()];
    }
    return [NSString stringWithFormat:
        @"%@ 的预约已超过宽限截止 %@，请重新预约。", plate, deadline];
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
