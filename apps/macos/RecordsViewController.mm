#import "RecordsViewController.h"

#import "TableView.h"
#import "TextUtil.h"
#import "bridge/ParkingBridge.h"

#include <chrono>

namespace{
// 与 Qt 版 MainWindow.cpp 的 recordOverlapsRange 保持一致：在停记录以「当前
// 时间」当作离场时间，因此跨越时间窗口的在停车辆也会被计入结果，而不是要求
// 整段停放都发生在窗口内。
bool recordOverlapsRange(const smartpark::ParkingRecord &record,
                         const smartpark::ParkingRecord::TimePoint &from,
                         const smartpark::ParkingRecord::TimePoint &to){
    const auto exitTime = record.exitTime().value_or(
        smartpark::ParkingRecord::Clock::now());
    return record.entryTime() <= to && exitTime >= from;
}
} // namespace

@implementation RecordsViewController{
    NSDatePicker *_fromPicker;
    NSDatePicker *_toPicker;
    TableView *_table;
    NSTextField *_summaryLabel;
    BOOL _filterActive;
}

- (void)loadView{
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];

    NSTextField *title = [NSTextField labelWithString:@"停车记录"];
    title.font = [NSFont systemFontOfSize:26 weight:NSFontWeightSemibold];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:title];

    _fromPicker = [self datePicker];
    _toPicker = [self datePicker];
    _fromPicker.dateValue = [NSDate dateWithTimeIntervalSinceNow:-7 * 86400];
    _toPicker.dateValue = [NSDate dateWithTimeIntervalSinceNow:86400];

    NSButton *queryButton = [NSButton buttonWithTitle:@"查询" target:self action:@selector(query:)];
    NSButton *resetButton = [NSButton buttonWithTitle:@"显示全部" target:self action:@selector(reset:)];
    NSTextField *rangeLabel = [NSTextField labelWithString:@"时间范围"];
    NSTextField *toLabel = [NSTextField labelWithString:@"至"];

    NSStackView *filterRow = [[NSStackView alloc] init];
    filterRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    filterRow.spacing = 8.0;
    filterRow.translatesAutoresizingMaskIntoConstraints = NO;
    [filterRow addArrangedSubview:rangeLabel];
    [filterRow addArrangedSubview:_fromPicker];
    [filterRow addArrangedSubview:toLabel];
    [filterRow addArrangedSubview:_toPicker];
    [filterRow addArrangedSubview:queryButton];
    [filterRow addArrangedSubview:resetButton];
    [root addSubview:filterRow];

    _table = [[TableView alloc] initWithFrame:NSMakeRect(0, 0, 800, 400)];
    _table.translatesAutoresizingMaskIntoConstraints = NO;
    [_table setColumns:@[@"车牌", @"车辆类型", @"车位", @"入场时间",
                         @"离场时间", @"时长(分钟)", @"费用(元)", @"状态"]];
    [root addSubview:_table];

    _summaryLabel = [NSTextField wrappingLabelWithString:@""];
    _summaryLabel.textColor = [NSColor secondaryLabelColor];
    _summaryLabel.font = [NSFont systemFontOfSize:12];
    _summaryLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_summaryLabel];

    [NSLayoutConstraint activateConstraints:@[
        [title.topAnchor constraintEqualToAnchor:root.topAnchor constant:24],
        [title.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [filterRow.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:14],
        [filterRow.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [_table.topAnchor constraintEqualToAnchor:filterRow.bottomAnchor constant:12],
        [_table.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [_table.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [_summaryLabel.topAnchor constraintEqualToAnchor:_table.bottomAnchor constant:10],
        [_summaryLabel.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [_summaryLabel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [_summaryLabel.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-16],
    ]];

    self.view = root;
}

- (void)viewDidLoad{
    [super viewDidLoad];
    _filterActive = NO;
    [self refresh];
}

- (NSDatePicker *)datePicker{
    NSDatePicker *picker = [[NSDatePicker alloc] init];
    picker.datePickerStyle = NSDatePickerStyleTextFieldAndStepper;
    picker.datePickerElements = NSDatePickerElementFlagYearMonthDay | NSDatePickerElementFlagHourMinute;
    picker.datePickerMode = NSDatePickerModeSingle;
    [picker.widthAnchor constraintEqualToConstant:180].active = YES;
    return picker;
}

- (void)query:(id)sender{
    // 与 Qt 版一致：先拒绝非法范围，而不是静默显示空表。
    if ([_fromPicker.dateValue compare:_toPicker.dateValue] == NSOrderedDescending){
        [self presentWarning:@"时间范围无效" detail:@"起始时间不能晚于结束时间。"];
        return;
    }
    _filterActive = YES;
    [self refresh];
}

- (void)presentWarning:(NSString *)title detail:(NSString *)detail{
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = detail;
    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:@"好"];
    NSWindow *window = self.view.window;
    if (window != nil){
        [alert beginSheetModalForWindow:window completionHandler:nil];
        return;
    }
    [alert runModal];
}

- (NSString *)rangeText{
    static NSDateFormatter *formatter = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        formatter = [[NSDateFormatter alloc] init];
        formatter.dateFormat = @"yyyy-MM-dd HH:mm";
    });
    return [NSString stringWithFormat:@"%@ 至 %@",
            [formatter stringFromDate:_fromPicker.dateValue],
            [formatter stringFromDate:_toPicker.dateValue]];
}

- (void)reset:(id)sender{
    _filterActive = NO;
    _fromPicker.dateValue = [NSDate dateWithTimeIntervalSinceNow:-7 * 86400];
    _toPicker.dateValue = [NSDate dateWithTimeIntervalSinceNow:86400];
    [self refresh];
}

- (void)refresh{
    if (self.bridge == nullptr || !self.bridge->ready()){
        return;
    }
    const auto &records = self.bridge->records();
    auto from = smartpark::ParkingRecord::TimePoint(
        std::chrono::seconds((long)[_fromPicker.dateValue timeIntervalSince1970]));
    auto to = smartpark::ParkingRecord::TimePoint(
        std::chrono::seconds((long)[_toPicker.dateValue timeIntervalSince1970]));

    NSMutableArray<NSArray<NSString *> *> *rows = [NSMutableArray array];
    int parkedCount = 0;
    double feeSum = 0.0;
    for (const smartpark::ParkingRecord &record : records){
        // 先筛后算：汇总的三个数字都必须来自可见行，否则表格与小结互相矛盾。
        if (_filterActive && !recordOverlapsRange(record, from, to)){
            continue;
        }
        const bool closed = record.isClosed();
        if (!closed){
            ++parkedCount;
        }
        feeSum += record.fee();
        auto duration = std::chrono::duration_cast<std::chrono::minutes>(record.duration());
        NSString *entry = [NSString stringWithUTF8String:smartpark_ui::formatTime(record.entryTime()).c_str()];
        NSString *exit = closed
            ? [NSString stringWithUTF8String:smartpark_ui::formatTime(*record.exitTime()).c_str()]
            : @"在停";
        [rows addObject:@[
            [NSString stringWithUTF8String:record.plateNumber().c_str()],
            [NSString stringWithUTF8String:smartpark_ui::vehicleTypeText(record.vehicleType())],
            [NSString stringWithUTF8String:record.spotId().c_str()],
            entry,
            exit,
            [NSString stringWithFormat:@"%lld", (long long)duration.count()],
            [NSString stringWithFormat:@"%.2f", record.fee()],
            closed ? @"已离场" : @"在停",
        ]];
    }
    [_table setRows:rows];

    if (_filterActive){
        _summaryLabel.stringValue = [NSString stringWithFormat:
            @"查询结果：%lu 条（全部 %lu 条） ｜ 在停 %d 辆 ｜ 费用合计 %.2f 元 ｜ 时间 %@",
            (unsigned long)rows.count, (unsigned long)records.size(),
            parkedCount, feeSum, [self rangeText]];
    } else{
        _summaryLabel.stringValue = [NSString stringWithFormat:
            @"全部停车记录：%lu 条 ｜ 在停 %d 辆 ｜ 费用合计 %.2f 元",
            (unsigned long)rows.count, parkedCount, feeSum];
    }
}

@end
