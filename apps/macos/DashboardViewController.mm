#import "DashboardViewController.h"

#import "BarChartView.h"
#import "DonutChartView.h"
#import "LineChartView.h"
#import "bridge/ParkingBridge.h"

#include "core/model/ParkingRecord.h"
#include "core/model/ParkingSpot.h"
#include "core/service/ParkingInsightEngine.h"

#include <ctime>

namespace{
NSColor *rgba(double r, double g, double b, double a){
    return [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0
                                blue:b / 255.0 alpha:a / 255.0];
}

NSString *dayLabel(int offsetFromToday){
    NSCalendar *cal = [NSCalendar currentCalendar];
    NSDate *date = [cal dateByAddingUnit:NSCalendarUnitDay value:offsetFromToday
                                  toDate:[NSDate date] options:0];
    NSDateFormatter *fmt = [[NSDateFormatter alloc] init];
    fmt.dateFormat = @"MM-dd";
    return [fmt stringFromDate:date];
}

// 记录时间点相对今天的天数差（今天=0，昨天=1 …），超出 0..6 返回 -1。
int dayIndexFromNow(const smartpark::ParkingRecord::TimePoint &tp){
    std::time_t raw = smartpark::ParkingRecord::Clock::to_time_t(tp);
    NSDate *date = [NSDate dateWithTimeIntervalSince1970:(NSTimeInterval)raw];
    NSCalendar *cal = [NSCalendar currentCalendar];
    NSDateComponents *diff = [cal components:NSCalendarUnitDay
                                    fromDate:[cal startOfDayForDate:date]
                                      toDate:[cal startOfDayForDate:[NSDate date]]
                                     options:0];
    NSInteger delta = diff.day;
    if (delta < 0 || delta > 6){
        return -1;
    }
    return (int)(6 - delta);
}
} // namespace

@implementation DashboardViewController{
    NSScrollView *_scroll;
    NSStackView *_stack;

    NSTextField *_kpiTotal;
    NSTextField *_kpiAvailable;
    NSTextField *_kpiOccupied;
    NSTextField *_kpiReserved;
    NSTextField *_summaryLabel;

    DonutChartView *_compositionChart;
    DonutChartView *_typeChart;
    LineChartView *_forecastChart;
    LineChartView *_sevenDayRevenueChart;
    LineChartView *_sevenDayFlowChart;
    BarChartView *_flowChart;
    BarChartView *_zonePressureChart;

    NSTextField *_insightLabel;
    NSTextField *_zoneInsightLabel;
    NSTextField *_bookingImpactLabel;
    NSTextField *_recordsTrendLabel;
}

- (void)loadView{
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 900, 700)];

    _scroll = [[NSScrollView alloc] initWithFrame:root.bounds];
    _scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _scroll.hasVerticalScroller = YES;
    _scroll.drawsBackground = NO;
    [root addSubview:_scroll];

    _stack = [[NSStackView alloc] init];
    _stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    _stack.alignment = NSLayoutAttributeLeading;
    _stack.spacing = 14.0;
    _stack.translatesAutoresizingMaskIntoConstraints = NO;

    NSView *content = [[NSView alloc] init];
    [content addSubview:_stack];
    [NSLayoutConstraint activateConstraints:@[
        [_stack.topAnchor constraintEqualToAnchor:content.topAnchor constant:20],
        [_stack.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:24],
        [_stack.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-24],
        [_stack.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-20],
    ]];
    _scroll.documentView = content;
    content.translatesAutoresizingMaskIntoConstraints = NO;
    [content.widthAnchor constraintEqualToAnchor:_scroll.contentView.widthAnchor].active = YES;

    // 标题
    NSTextField *title = [NSTextField labelWithString:@"仪表盘"];
    title.font = [NSFont systemFontOfSize:26 weight:NSFontWeightSemibold];
    [_stack addArrangedSubview:title];

    // KPI 行
    NSStackView *kpiRow = [[NSStackView alloc] init];
    kpiRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    kpiRow.distribution = NSStackViewDistributionFillEqually;
    kpiRow.spacing = 12.0;
    _kpiTotal = [self kpiCard:@"总车位" into:kpiRow];
    _kpiAvailable = [self kpiCard:@"空闲" into:kpiRow];
    _kpiOccupied = [self kpiCard:@"占用" into:kpiRow];
    _kpiReserved = [self kpiCard:@"预留" into:kpiRow];
    [_stack addArrangedSubview:kpiRow];
    [kpiRow.widthAnchor constraintEqualToAnchor:_stack.widthAnchor].active = YES;

    // 摘要
    _summaryLabel = [NSTextField wrappingLabelWithString:@""];
    _summaryLabel.textColor = [NSColor secondaryLabelColor];
    _summaryLabel.font = [NSFont systemFontOfSize:12];
    [_stack addArrangedSubview:_summaryLabel];
    [_summaryLabel.widthAnchor constraintEqualToAnchor:_stack.widthAnchor].active = YES;

    // 图表：第一行（组合 Donut + 车型 Donut + 预测 Line）
    _compositionChart = [DonutChartView new];
    _typeChart = [DonutChartView new];
    _forecastChart = [LineChartView new];
    [_forecastChart setUnit:@"%"];
    NSStackView *chartRow1 = [self rowWithViews:@[
        [self cardWithTitle:@"车位组合" content:_compositionChart],
        [self cardWithTitle:@"车位类型" content:_typeChart],
        [self cardWithTitle:@"占用率预测" content:_forecastChart],
    ]];
    [_stack addArrangedSubview:chartRow1];
    [chartRow1.widthAnchor constraintEqualToAnchor:_stack.widthAnchor].active = YES;

    // 图表：第二行（7 天收入 + 7 天流量）
    _sevenDayRevenueChart = [LineChartView new];
    [_sevenDayRevenueChart setUnit:@" 元"];
    [_sevenDayRevenueChart setValueDecimals:2];
    _sevenDayFlowChart = [LineChartView new];
    [_sevenDayFlowChart setUnit:@" 辆"];
    NSStackView *chartRow2 = [self rowWithViews:@[
        [self cardWithTitle:@"近 7 天收入" content:_sevenDayRevenueChart],
        [self cardWithTitle:@"近 7 天流量" content:_sevenDayFlowChart],
    ]];
    [_stack addArrangedSubview:chartRow2];
    [chartRow2.widthAnchor constraintEqualToAnchor:_stack.widthAnchor].active = YES;

    // 图表：第三行（流量 Bar + 分区压力 Bar）
    _flowChart = [BarChartView new];
    _zonePressureChart = [BarChartView new];
    NSStackView *chartRow3 = [self rowWithViews:@[
        [self cardWithTitle:@"车流统计" content:_flowChart],
        [self cardWithTitle:@"分区压力" content:_zonePressureChart],
    ]];
    [_stack addArrangedSubview:chartRow3];
    [chartRow3.widthAnchor constraintEqualToAnchor:_stack.widthAnchor].active = YES;

    // 洞察标签
    _insightLabel = [self insightLabel];
    _zoneInsightLabel = [self insightLabel];
    _bookingImpactLabel = [self insightLabel];
    _recordsTrendLabel = [self insightLabel];
    [_stack addArrangedSubview:_insightLabel];
    [_stack addArrangedSubview:_zoneInsightLabel];
    [_stack addArrangedSubview:_bookingImpactLabel];
    [_stack addArrangedSubview:_recordsTrendLabel];
    for (NSTextField *label in @[_insightLabel, _zoneInsightLabel,
                                 _bookingImpactLabel, _recordsTrendLabel]){
        [label.widthAnchor constraintEqualToAnchor:_stack.widthAnchor].active = YES;
    }

    self.view = root;
}

- (void)viewDidLoad{
    [super viewDidLoad];
    [self refresh];
}

- (void)refresh{
    [self refreshMetrics];
}

- (NSVisualEffectView *)glassCard{
    NSVisualEffectView *card = [[NSVisualEffectView alloc] init];
    card.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    card.material = NSVisualEffectMaterialUnderWindowBackground;
    card.state = NSVisualEffectStateActive;
    card.wantsLayer = YES;
    card.layer.cornerRadius = 15.0;
    card.layer.masksToBounds = YES;
    return card;
}

- (NSTextField *)kpiCard:(NSString *)name into:(NSStackView *)row{
    NSVisualEffectView *box = [self glassCard];
    box.translatesAutoresizingMaskIntoConstraints = NO;

    NSTextField *value = [NSTextField labelWithString:@"0"];
    value.font = [NSFont monospacedDigitSystemFontOfSize:22 weight:NSFontWeightSemibold];
    NSTextField *nameLabel = [NSTextField labelWithString:name];
    nameLabel.textColor = [NSColor labelColor];
    nameLabel.font = [NSFont systemFontOfSize:12 weight:NSFontWeightMedium];

    NSStackView *v = [[NSStackView alloc] init];
    v.orientation = NSUserInterfaceLayoutOrientationVertical;
    v.alignment = NSLayoutAttributeCenterX;
    v.spacing = 4.0;
    v.translatesAutoresizingMaskIntoConstraints = NO;
    [v addArrangedSubview:value];
    [v addArrangedSubview:nameLabel];

    [box addSubview:v];
    [NSLayoutConstraint activateConstraints:@[
        [v.topAnchor constraintEqualToAnchor:box.topAnchor constant:12],
        [v.bottomAnchor constraintEqualToAnchor:box.bottomAnchor constant:-12],
        [v.centerXAnchor constraintEqualToAnchor:box.centerXAnchor],
    ]];
    [box.heightAnchor constraintGreaterThanOrEqualToConstant:70].active = YES;
    [row addArrangedSubview:box];
    return value;
}

- (NSView *)cardWithTitle:(NSString *)title content:(NSView *)content{
    NSVisualEffectView *box = [self glassCard];

    NSTextField *titleLabel = [NSTextField labelWithString:title];
    titleLabel.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];

    content.translatesAutoresizingMaskIntoConstraints = NO;

    [box addSubview:titleLabel];
    [box addSubview:content];
    titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [titleLabel.topAnchor constraintEqualToAnchor:box.topAnchor constant:12],
        [titleLabel.leadingAnchor constraintEqualToAnchor:box.leadingAnchor constant:14],
        [content.topAnchor constraintEqualToAnchor:titleLabel.bottomAnchor constant:8],
        [content.leadingAnchor constraintEqualToAnchor:box.leadingAnchor constant:8],
        [content.trailingAnchor constraintEqualToAnchor:box.trailingAnchor constant:-8],
        [content.bottomAnchor constraintEqualToAnchor:box.bottomAnchor constant:-8],
        [content.heightAnchor constraintGreaterThanOrEqualToConstant:240],
    ]];
    return box;
}

- (NSStackView *)rowWithViews:(NSArray<NSView *> *)views{
    NSStackView *row = [[NSStackView alloc] init];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.distribution = NSStackViewDistributionFillEqually;
    row.spacing = 12.0;
    for (NSView *view in views){
        [row addArrangedSubview:view];
    }
    return row;
}

- (NSTextField *)insightLabel{
    NSTextField *label = [NSTextField wrappingLabelWithString:@""];
    label.textColor = [NSColor labelColor];
    label.font = [NSFont systemFontOfSize:12];
    return label;
}

- (void)refreshMetrics{
    if (self.bridge == nullptr || !self.bridge->ready()){
        return;
    }
    ParkingBridge *b = self.bridge;

    const int total = b->totalSpots();
    const int available = b->remainingSpots();
    const int occupied = b->occupiedSpots();
    const int reserved = b->reservedSpots();
    _kpiTotal.stringValue = [NSString stringWithFormat:@"%d", total];
    _kpiAvailable.stringValue = [NSString stringWithFormat:@"%d", available];
    _kpiOccupied.stringValue = [NSString stringWithFormat:@"%d", occupied];
    _kpiReserved.stringValue = [NSString stringWithFormat:@"%d", reserved];

    const double occupancyRate = total == 0 ? 0.0 : occupied * 100.0 / total;
    _summaryLabel.stringValue = [NSString stringWithFormat:
        @"当前占用率 %.1f%%（%d / %d）。累计停车记录 %d 条，累计已结算费用 %.2f 元。\n"
         "待结算预约定金 %.2f 元；爽约没收定金 %.2f 元。",
        occupancyRate, occupied, total, b->recordCount(), b->totalRevenue(),
        b->pendingDeposits(), b->forfeitedDeposits()];

    const smartpark::ParkingInsights insights = b->insights();

    // 组合占比 Donut
    const int occupiedN = MAX(0, insights.currentOccupied);
    const int reservedN = MAX(0, insights.currentReserved);
    const int disabledN = MAX(0, insights.currentDisabled);
    const int availableN = MAX(0, total - occupiedN - reservedN - disabledN);
    [_compositionChart setCenterTitle:@"总车位"];
    [_compositionChart setCenterValue:[NSString stringWithFormat:@"%d", total]];
    [_compositionChart setSlices:@[
        [self donutSlice:@"占用" value:occupiedN color:rgba(180, 35, 24, 140)],
        [self donutSlice:@"预留" value:reservedN color:rgba(181, 71, 8, 140)],
        [self donutSlice:@"空闲" value:availableN color:rgba(15, 118, 110, 140)],
        [self donutSlice:@"停用" value:disabledN color:rgba(102, 112, 133, 140)],
    ]];

    // 车型占比 Donut
    int normal = 0, accessible = 0, charging = 0, vip = 0;
    for (const smartpark::ParkingSpot &spot : b->spots()){
        switch (spot.type()){
        case smartpark::SpotType::Accessible: ++accessible; break;
        case smartpark::SpotType::Charging: ++charging; break;
        case smartpark::SpotType::Vip: ++vip; break;
        case smartpark::SpotType::Normal: default: ++normal; break;
        }
    }
    [_typeChart setCenterTitle:@"车位类型"];
    [_typeChart setCenterValue:[NSString stringWithFormat:@"%d", normal + accessible + charging + vip]];
    [_typeChart setSlices:@[
        [self donutSlice:@"普通" value:normal color:rgba(15, 118, 110, 140)],
        [self donutSlice:@"无障碍" value:accessible color:rgba(79, 70, 229, 140)],
        [self donutSlice:@"充电" value:charging color:rgba(2, 106, 162, 140)],
        [self donutSlice:@"VIP" value:vip color:rgba(124, 58, 237, 140)],
    ]];

    // 预测 Line
    const smartpark::OccupancyForecast *f30 = nullptr, *f60 = nullptr, *f120 = nullptr;
    for (const smartpark::OccupancyForecast &f : insights.forecasts){
        if (f.minutes == 30) f30 = &f;
        else if (f.minutes == 60) f60 = &f;
        else if (f.minutes == 120) f120 = &f;
    }
    LineSeriesData *forecast = [LineSeriesData new];
    forecast.name = @"预测占用率";
    forecast.color = rgba(46, 109, 180, 140);
    NSMutableArray<LinePointData *> *forecastPoints = [NSMutableArray array];
    [forecastPoints addObject:[self linePoint:@"当前" value:insights.currentRate]];
    if (f30) [forecastPoints addObject:[self linePoint:@"30分" value:f30->predictedRate]];
    if (f60) [forecastPoints addObject:[self linePoint:@"60分" value:f60->predictedRate]];
    if (f120) [forecastPoints addObject:[self linePoint:@"120分" value:f120->predictedRate]];
    forecast.points = forecastPoints;
    [_forecastChart setSeries:@[forecast]];

    // 7 天收入 + 流量
    NSMutableArray<NSString *> *dayLabels = [NSMutableArray array];
    for (int i = 0; i < 7; ++i){
        [dayLabels addObject:dayLabel(i - 6)];
    }
    double revenue[7] = {0};
    double arrivals[7] = {0};
    double departures[7] = {0};
    for (const smartpark::ParkingRecord &record : b->records()){
        int ei = dayIndexFromNow(record.entryTime());
        if (ei >= 0) arrivals[ei] += 1.0;
        if (record.exitTime()){
            int xi = dayIndexFromNow(*record.exitTime());
            if (xi >= 0){
                departures[xi] += 1.0;
                if (record.isClosed()) revenue[xi] += record.fee();
            }
        }
    }
    LineSeriesData *revenueSeries = [LineSeriesData new];
    revenueSeries.name = @"收入";
    revenueSeries.color = rgba(15, 118, 110, 140);
    NSMutableArray<LinePointData *> *revenuePoints = [NSMutableArray array];
    for (int i = 0; i < 7; ++i){
        [revenuePoints addObject:[self linePoint:dayLabels[i] value:revenue[i]]];
    }
    revenueSeries.points = revenuePoints;
    [_sevenDayRevenueChart setSeries:@[revenueSeries]];

    LineSeriesData *arrivalSeries = [LineSeriesData new];
    arrivalSeries.name = @"入场";
    arrivalSeries.color = rgba(2, 106, 162, 140);
    LineSeriesData *departureSeries = [LineSeriesData new];
    departureSeries.name = @"离场";
    departureSeries.color = rgba(180, 35, 24, 140);
    NSMutableArray<LinePointData *> *arrivalPoints = [NSMutableArray array];
    NSMutableArray<LinePointData *> *departurePoints = [NSMutableArray array];
    for (int i = 0; i < 7; ++i){
        [arrivalPoints addObject:[self linePoint:dayLabels[i] value:arrivals[i]]];
        [departurePoints addObject:[self linePoint:dayLabels[i] value:departures[i]]];
    }
    arrivalSeries.points = arrivalPoints;
    departureSeries.points = departurePoints;
    [_sevenDayFlowChart setSeries:@[arrivalSeries, departureSeries]];

    // 车流 Bar
    [_flowChart setBars:@[
        [self barSlice:@"1 小时入场" value:insights.arrivals60
                  color:rgba(15, 118, 110, 140)
                 text:[NSString stringWithFormat:@"%d 辆", insights.arrivals60]],
        [self barSlice:@"60 分钟离场" value:insights.departures60
                  color:rgba(180, 35, 24, 140)
                 text:[NSString stringWithFormat:@"%d 辆", insights.departures60]],
        [self barSlice:@"3 小时入场" value:insights.arrivals180
                  color:rgba(2, 106, 162, 140)
                 text:[NSString stringWithFormat:@"%d 辆", insights.arrivals180]],
        [self barSlice:@"3 小时离场" value:insights.departures180
                  color:rgba(124, 58, 237, 140)
                 text:[NSString stringWithFormat:@"%d 辆", insights.departures180]],
    ]];

    // 分区压力 Bar
    NSMutableArray<BarSliceData *> *zoneBars = [NSMutableArray array];
    for (const smartpark::ZoneInsight &zone : insights.zones){
        double percent = zone.pressure * 100.0;
        NSColor *color = rgba(15, 118, 110, 140);
        if (percent >= 90.0) color = rgba(180, 35, 24, 140);
        else if (percent >= 80.0) color = rgba(181, 71, 8, 140);
        [zoneBars addObject:[self barSlice:[NSString stringWithUTF8String:zone.zone.c_str()]
                                    value:percent color:color
                                     text:[NSString stringWithFormat:@"%.0f%%", percent]]];
    }
    [_zonePressureChart setBars:zoneBars];

    // 洞察标签
    NSString *f60Text = f60 ? [NSString stringWithFormat:@"%.0f%%", f60->predictedRate] : @"-";
    NSString *confidenceText = f60 ? [NSString stringWithFormat:@"%d", (int)(f60->confidence * 100.0)] : @"0";
    NSString *highestZone = @"-";
    double highestPressure = 0.0;
    int warningCount = 0, criticalCount = 0;
    NSMutableArray<NSString *> *riskTitles = [NSMutableArray array];
    for (const smartpark::ZoneInsight &zone : insights.zones){
        if (zone.pressure > highestPressure){
            highestPressure = zone.pressure;
            highestZone = [NSString stringWithUTF8String:zone.zone.c_str()];
        }
    }
    for (const smartpark::RiskAlert &alert : insights.alerts){
        if (alert.severity == smartpark::RiskAlert::Severity::Critical){
            ++criticalCount;
            [riskTitles addObject:[NSString stringWithUTF8String:alert.title.c_str()]];
        } else if (alert.severity == smartpark::RiskAlert::Severity::Warning){
            ++warningCount;
            [riskTitles addObject:[NSString stringWithUTF8String:alert.title.c_str()]];
        }
    }

    NSMutableString *insight = [NSMutableString stringWithFormat:
        @"预测：60 分钟后占用率 %@（置信度 %@%%）", f60Text, confidenceText];
    [insight appendFormat:@"\n最高压力分区 %@ · 风险告警 %d 条（严重 %d 条）",
        highestZone, warningCount + criticalCount, criticalCount];
    if (riskTitles.count > 0){
        [insight appendFormat:@"\n重点：%@", [riskTitles componentsJoinedByString:@"；"]];
    }
    [insight appendFormat:@"\n趋势推演基于最近 3 小时入场 %d 辆、离场 %d 辆。",
        insights.arrivals180, insights.departures180];
    _insightLabel.stringValue = insight;

    NSMutableArray<NSString *> *topZones = [NSMutableArray array];
    NSInteger zoneLimit = MIN(3, (NSInteger)insights.zones.size());
    for (NSInteger i = 0; i < zoneLimit; ++i){
        const smartpark::ZoneInsight &zone = insights.zones[i];
        [topZones addObject:[NSString stringWithFormat:@"%s %.0f%%",
                             zone.zone.c_str(), zone.pressure * 100.0]];
    }
    _zoneInsightLabel.stringValue = [NSString stringWithFormat:
        @"分区压力：%@ ｜ 压力最高：%@ ｜ 建议优先把新入场车辆引导至压力较低的分区。",
        [topZones componentsJoinedByString:@" · "], highestZone];

    NSString *rate30Text = f30 ? [NSString stringWithFormat:@"%.0f%%", f30->predictedRate] : @"-";
    int upcoming30 = f30 ? f30->upcomingBookings : 0;
    int upcoming60 = f60 ? f60->upcomingBookings : 0;
    _bookingImpactLabel.stringValue = [NSString stringWithFormat:
        @"预约影响：未来 30 分钟新增 %d 个到场，预计占用率升至 %@；未来 60 分钟新增 %d 个到场。",
        upcoming30, rate30Text, upcoming60];

    NSString *f120Text = f120 ? [NSString stringWithFormat:@"%.0f%%", f120->predictedRate] : @"-";
    _recordsTrendLabel.stringValue = [NSString stringWithFormat:
        @"趋势摘要：最近 60 分钟入场 %d / 离场 %d；最近 180 分钟入场 %d / 离场 %d。"
         "预测置信度 %@%% · 120 分钟预测占用率 %@。",
        insights.arrivals60, insights.departures60,
        insights.arrivals180, insights.departures180, confidenceText, f120Text];
}

- (DonutSliceData *)donutSlice:(NSString *)label value:(double)value color:(NSColor *)color{
    DonutSliceData *slice = [DonutSliceData new];
    slice.label = label;
    slice.value = value;
    slice.color = color;
    return slice;
}

- (BarSliceData *)barSlice:(NSString *)label value:(double)value color:(NSColor *)color text:(NSString *)text{
    BarSliceData *slice = [BarSliceData new];
    slice.label = label;
    slice.value = value;
    slice.color = color;
    slice.valueText = text;
    return slice;
}

- (LinePointData *)linePoint:(NSString *)label value:(double)value{
    LinePointData *point = [LinePointData new];
    point.label = label;
    point.value = value;
    return point;
}

@end
