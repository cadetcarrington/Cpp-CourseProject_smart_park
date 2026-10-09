#import "BookingViewController.h"

#import "TableView.h"
#import "TextUtil.h"
#import "bridge/ParkingBridge.h"

#include <chrono>
#include <cmath>
#include <cstddef>

// 预约管理页。表单写的是「时段预约」（Reservation，0.7 模型）：与网页 H5、
// 用户端 CLI、协议 reservation.* 同一张表、同一套规则，因此本地模式与远程
// 服务端模式下的行为完全一致——远程模式由服务端完成校验、选位、收定金与
// 路线规划。第一版预约（Booking）只剩历史数据，列在下方并标注来源。
@implementation BookingViewController{
    NSTextField *_plateField;
    NSPopUpButton *_typePopup;
    NSDatePicker *_startPicker;
    NSPopUpButton *_durationPopup;
    NSButton *_accessibleCheck;
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
    _startPicker = [self datePicker];
    _startPicker.dateValue = [self defaultStartDate];
    _durationPopup = [[NSPopUpButton alloc] init];
    // tag 就是分钟数：提交时直接取，避免下标与时长两套映射。
    for (NSArray *item in @[@[@"30 分钟", @30], @[@"1 小时", @60], @[@"2 小时", @120],
                            @[@"3 小时", @180], @[@"4 小时", @240]]){
        [_durationPopup addItemWithTitle:item[0]];
        _durationPopup.lastItem.tag = [item[1] integerValue];
    }
    [_durationPopup selectItemWithTag:120];
    _accessibleCheck = [NSButton checkboxWithTitle:@"无障碍车位（免定金）"
                                            target:nil
                                            action:nil];

    NSButton *createButton = [NSButton buttonWithTitle:@"创建预约"
                                                target:self
                                                action:@selector(createReservation:)];
    NSButton *checkInButton = [NSButton buttonWithTitle:@"到场确认"
                                                 target:self
                                                 action:@selector(checkIn:)];
    NSButton *cancelButton = [NSButton buttonWithTitle:@"取消预约"
                                                target:self
                                                action:@selector(cancel:)];
    createButton.keyEquivalent = @"\r";

    NSStackView *formRow = [[NSStackView alloc] init];
    formRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    formRow.spacing = 10.0;
    formRow.translatesAutoresizingMaskIntoConstraints = NO;
    [formRow addArrangedSubview:[NSTextField labelWithString:@"车牌"]];
    [formRow addArrangedSubview:_plateField];
    [formRow addArrangedSubview:[NSTextField labelWithString:@"车型"]];
    [formRow addArrangedSubview:_typePopup];
    [formRow addArrangedSubview:[NSTextField labelWithString:@"到场时间"]];
    [formRow addArrangedSubview:_startPicker];
    [formRow addArrangedSubview:[NSTextField labelWithString:@"时长"]];
    [formRow addArrangedSubview:_durationPopup];
    [formRow addArrangedSubview:_accessibleCheck];
    [_plateField.widthAnchor constraintEqualToConstant:130].active = YES;

    NSStackView *actionRow = [[NSStackView alloc] init];
    actionRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    actionRow.spacing = 10.0;
    actionRow.translatesAutoresizingMaskIntoConstraints = NO;
    [actionRow addArrangedSubview:createButton];
    [actionRow addArrangedSubview:checkInButton];
    [actionRow addArrangedSubview:cancelButton];
    [form addSubview:formRow];
    [form addSubview:actionRow];

    _policyLabel = [NSTextField wrappingLabelWithString:@""];
    _policyLabel.textColor = [NSColor secondaryLabelColor];
    _policyLabel.font = [NSFont systemFontOfSize:12];
    _policyLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [form addSubview:_policyLabel];

    [NSLayoutConstraint activateConstraints:@[
        [formRow.topAnchor constraintEqualToAnchor:form.topAnchor constant:14],
        [formRow.leadingAnchor constraintEqualToAnchor:form.leadingAnchor constant:14],
        [formRow.trailingAnchor constraintLessThanOrEqualToAnchor:form.trailingAnchor
                                                         constant:-14],
        [actionRow.topAnchor constraintEqualToAnchor:formRow.bottomAnchor constant:10],
        [actionRow.leadingAnchor constraintEqualToAnchor:form.leadingAnchor constant:14],
        [actionRow.trailingAnchor constraintLessThanOrEqualToAnchor:form.trailingAnchor
                                                          constant:-14],
        [_policyLabel.topAnchor constraintEqualToAnchor:actionRow.bottomAnchor constant:10],
        [_policyLabel.leadingAnchor constraintEqualToAnchor:form.leadingAnchor constant:14],
        [_policyLabel.trailingAnchor constraintEqualToAnchor:form.trailingAnchor constant:-14],
        [_policyLabel.bottomAnchor constraintEqualToAnchor:form.bottomAnchor constant:-12],
    ]];

    _table = [[TableView alloc] initWithFrame:NSMakeRect(0, 0, 800, 300)];
    _table.translatesAutoresizingMaskIntoConstraints = NO;
    // 末列区分两代预约：本页表单创建的是 Reservation（「时段预约」），
    // 第一版的 Booking 只剩历史数据（本地库里的旧记录）。
    [_table setColumns:@[@"编号", @"车牌", @"车位", @"创建时间", @"预约时间",
                         @"宽限截止", @"定金(元)", @"状态", @"来源"]];
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

// 与网页 H5 同一口径：(当前 + 45 分钟) 向上取整到 15 分钟格，
// 保证默认值一定满足「至少提前」规则，不用用户自己算。
- (NSDate *)defaultStartDate{
    const NSTimeInterval snap = 15 * 60.0;
    const NSTimeInterval target = [NSDate timeIntervalSinceReferenceDate] + 45 * 60.0;
    return [NSDate dateWithTimeIntervalSinceReferenceDate:std::ceil(target / snap) * snap];
}

- (NSString *)plate{
    return [_plateField.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
}

- (smartpark::ParkingRecord::TimePoint)startTime{
    return smartpark::ParkingRecord::TimePoint(
        std::chrono::seconds((long)_startPicker.dateValue.timeIntervalSince1970));
}

- (void)createReservation:(id)sender{
    NSString *plate = [self plate];
    if (plate.length == 0){
        _statusLabel.stringValue = @"创建预约前请输入车牌。";
        return;
    }
    if (self.bridge == nullptr || !self.bridge->ready()){
        _statusLabel.stringValue = @"数据未就绪，无法创建预约。";
        return;
    }
    const auto rule = self.bridge->reservationRule();
    const int durationMin = (int)_durationPopup.selectedTag;
    const NSTimeInterval lead = _startPicker.dateValue.timeIntervalSinceNow;
    // 先在本地按规则拦一道，省一次注定被拒的往返；措辞与核心/服务端一致。
    if (rule.minLeadTimeMin > 0 && lead < rule.minLeadTimeMin * 60.0){
        _statusLabel.stringValue = [NSString stringWithFormat:
            @"预约需至少提前 %d 分钟：请把到场时间调到 %@ 之后。",
            rule.minLeadTimeMin,
            smartpark_ui::toNSString(smartpark_ui::formatTime(
                smartpark::ParkingRecord::Clock::now() +
                std::chrono::minutes(rule.minLeadTimeMin)))];
        return;
    }
    if (rule.minDurationMin > 0 && durationMin < rule.minDurationMin){
        _statusLabel.stringValue = [NSString stringWithFormat:
            @"预约时长不足：最短 %d 分钟。", rule.minDurationMin];
        return;
    }
    if (rule.maxAdvanceDays > 0 && lead > rule.maxAdvanceDays * 86400.0){
        _statusLabel.stringValue = [NSString stringWithFormat:
            @"只能预约未来 %d 天内的时间段。", rule.maxAdvanceDays];
        return;
    }

    const int typeIndex = (int)_typePopup.indexOfSelectedItem;
    const auto arrival = [self startTime];
    auto result = self.bridge->createReservation(
        plate.UTF8String, smartpark_ui::vehicleTypeFromIndex(typeIndex), arrival,
        std::chrono::minutes(durationMin),
        _accessibleCheck.state == NSControlStateValueOn);
    if (!result){
        const std::string reason = self.bridge->lastError();
        _statusLabel.stringValue = reason.empty()
            ? [NSString stringWithFormat:@"预约失败：车牌 %@ 无法预约（可能已在场、已有未结束预约或没有可用车位）。", plate]
            : smartpark_ui::toNSString(reason);
    } else{
        // 到场窗口 = [开始 - 锁位提前量, 宽限截止]，与核心/服务端的判定一致；
        // 窗口没到就点「到场确认」会被拒绝，所以这里先把时间说清楚。
        const auto start = result->reservation.startTime();
        const auto windowFrom = start -
            std::chrono::minutes(rule.lockLeadTimeMin);
        const auto deadline = result->reservation.graceDeadline();
        _statusLabel.stringValue = [NSString stringWithFormat:
            @"预约成功：%@ → 车位 %@；定金 %.2f 元已收。\n"
             "预期路线：入口 %zu 行驶 %.1f 米 / %d 个转弯；离场 %.1f 米 / %d 个转弯。\n"
             "可到场时间 %@ 至 %@：到点在下方点「到场确认」，或由入口闸机刷牌自动核销。",
            plate, smartpark_ui::toNSString(result->reservation.spotId()),
            result->reservation.deposit(),
            result->entranceIndex + 1, result->entryRoute.distance,
            result->entryRoute.turnCount, result->exitRoute.distance,
            result->exitRoute.turnCount,
            smartpark_ui::toNSString(smartpark_ui::formatTime(
                smartpark::ParkingRecord::TimePoint(windowFrom.time_since_epoch()))),
            smartpark_ui::toNSString(smartpark_ui::formatTime(
                smartpark::ParkingRecord::TimePoint(deadline.time_since_epoch())))];
        // 下单时规划好的路线交给车位图：预约之后地图上就能看到进场/出场路线。
        self.bridge->setPlannedRoute(result->entryRoute, result->exitRoute);
    }
    // 预约会锁位并产生定金，广播给所有页面统一刷新
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
    if (self.bridge == nullptr || !self.bridge->ready()){
        _statusLabel.stringValue = @"数据未就绪，无法确认到场。";
        return;
    }
    auto arrived = self.bridge->checkInReservation(plate.UTF8String);
    if (arrived){
        _statusLabel.stringValue = [NSString stringWithFormat:
            @"到场确认成功：%@ 已入场，车位 %@；定金将在离场时抵扣停车费。",
            plate, smartpark_ui::toNSString(arrived->spotId())];
    } else{
        // 失败原因优先用数据源的原话（服务端会说明窗口/状态），
        // 拿不到时才回查本地预约记录自己解释。
        NSString *reason = smartpark_ui::toNSString(self.bridge->lastError());
        _statusLabel.stringValue = reason.length > 0
            ? reason
            : [self checkInFailureReasonForPlate:plate];
    }
    // 到场确认会把车位转为占用并新建停车记录，必须让仪表盘/地图/记录同步。
    [[NSNotificationCenter defaultCenter] postNotificationName:@"SmartParkDataChanged"
                                                        object:nil];
}

// 兜底解释：数据源没给原因时，回查预约记录说明为什么确认不了
// （未预约 / 已取消 / 已爽约 / 还没到窗口 / 已过宽限）。
- (NSString *)checkInFailureReasonForPlate:(NSString *)plate{
    const std::string target(plate.UTF8String);
    const smartpark::Reservation *latest = nullptr;
    for (const smartpark::Reservation &reservation : self.bridge->reservations()){
        if (reservation.plateNumber() != target){
            continue;
        }
        if (latest == nullptr || reservation.startTime() > latest->startTime()){
            latest = &reservation;
        }
    }
    if (latest == nullptr){
        return [NSString stringWithFormat:@"车牌 %@ 没有时段预约，请先创建预约。", plate];
    }
    const auto rule = self.bridge->reservationRule();
    auto clockTime = [](smartpark::Reservation::TimePoint t){
        return smartpark::ParkingRecord::TimePoint(t.time_since_epoch());
    };
    NSString *start = smartpark_ui::toNSString(smartpark_ui::formatTime(clockTime(latest->startTime())));
    NSString *deadline = smartpark_ui::toNSString(smartpark_ui::formatTime(clockTime(latest->graceDeadline())));
    switch (latest->status()){
    case smartpark::ReservationStatus::Cancelled:
        return [NSString stringWithFormat:@"车牌 %@ 的预约已被取消。", plate];
    case smartpark::ReservationStatus::NoShow:
        return [NSString stringWithFormat:
            @"车牌 %@ 的预约已超过宽限截止 %@ 作废（爽约，定金已没收），请重新预约。",
            plate, deadline];
    case smartpark::ReservationStatus::CheckedIn:
    case smartpark::ReservationStatus::Completed:
        return [NSString stringWithFormat:
            @"车牌 %@ 已到场，车辆正在场内；如需离场请到「车辆作业」办理出库。", plate];
    case smartpark::ReservationStatus::Expired:
        return [NSString stringWithFormat:@"车牌 %@ 的预约支付超时已关闭，请重新预约。", plate];
    case smartpark::ReservationStatus::Confirmed:
    case smartpark::ReservationStatus::PendingPayment:
    default:
        break;
    }
    NSString *opens = smartpark_ui::toNSString(smartpark_ui::formatTime(
        smartpark::ParkingRecord::TimePoint(
            (latest->startTime() - std::chrono::minutes(rule.lockLeadTimeMin))
                .time_since_epoch())));
    return [NSString stringWithFormat:
        @"还没到可确认时间：%@ 的预约从 %@ 开始，可确认时段为 %@ 至 %@"
         "（开始前 %d 分钟起）；到点后再点「到场确认」。",
        plate, start, opens, deadline, rule.lockLeadTimeMin];
}

- (void)cancel:(id)sender{
    NSString *plate = [self plate];
    if (plate.length == 0){
        _statusLabel.stringValue = @"取消预约前请输入预约车牌。";
        return;
    }
    if (self.bridge == nullptr || !self.bridge->ready()){
        _statusLabel.stringValue = @"数据未就绪，无法取消预约。";
        return;
    }
    const bool ok = self.bridge->cancelReservation(plate.UTF8String);
    if (ok){
        _statusLabel.stringValue = [NSString stringWithFormat:
            @"取消成功：%@ 的预约已取消，定金全额退回。", plate];
        // 预约没了，预期路线也就失效了。
        self.bridge->clearPlannedRoute();
    } else{
        NSString *reason = smartpark_ui::toNSString(self.bridge->lastError());
        _statusLabel.stringValue = reason.length > 0
            ? reason
            : [NSString stringWithFormat:@"车牌 %@ 没有可取消的预约。", plate];
    }
    // 取消会释放车位并退回定金，同样需要广播。
    [[NSNotificationCenter defaultCenter] postNotificationName:@"SmartParkDataChanged"
                                                        object:nil];
}

- (void)refresh{
    if (self.bridge == nullptr || !self.bridge->ready()){
        return;
    }
    const auto rule = self.bridge->reservationRule();
    _policyLabel.stringValue = [NSString stringWithFormat:
        @"预约规则：定金 %.2f 元（无障碍车位免定金） | 需至少提前 %d 分钟 | "
         "最短时长 %d 分钟 | 最多提前 %d 天 | 到场窗口：开始前 %d 分钟 至 开始后 %d 分钟。",
        rule.deposit, rule.minLeadTimeMin, rule.minDurationMin, rule.maxAdvanceDays,
        rule.lockLeadTimeMin, rule.gracePeriodMin];

    // 时段预约是当前模型，排在前面；Booking 是历史数据，排在后面。
    NSMutableArray<NSArray<NSString *> *> *rows = [NSMutableArray array];
    for (const smartpark::Reservation &reservation : self.bridge->reservations()){
        auto time = [](smartpark::Reservation::TimePoint t){
            return smartpark_ui::formatTime(
                smartpark::ParkingRecord::TimePoint(t.time_since_epoch()));
        };
        [rows addObject:@[
            [NSString stringWithUTF8String:reservation.id().c_str()],
            [NSString stringWithUTF8String:reservation.plateNumber().c_str()],
            [NSString stringWithUTF8String:reservation.spotId().c_str()],
            [NSString stringWithUTF8String:time(reservation.createdAt()).c_str()],
            [NSString stringWithUTF8String:time(reservation.startTime()).c_str()],
            [NSString stringWithUTF8String:time(reservation.graceDeadline()).c_str()],
            [NSString stringWithFormat:@"%.2f", reservation.deposit()],
            [NSString stringWithUTF8String:smartpark_ui::reservationStatusText(reservation.status())],
            @"时段预约",
        ]];
    }
    // 第一版预约（Booking）：本页表单不再创建它，只显示本地库里的历史记录，
    // 免得旧数据像凭空消失。
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
            @"预约(历史)",
        ]];
    }
    [_table setRows:rows];
}

@end
