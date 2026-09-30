#import "OperationsViewController.h"

#import "TextUtil.h"
#import "bridge/ParkingBridge.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cstdlib>

@implementation OperationsViewController{
    NSTextField *_plateField;
    NSPopUpButton *_typePopup;
    NSPopUpButton *_strategyPopup;
    NSTextField *_statusLabel;
}

- (void)loadView{
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];

    NSTextField *title = [NSTextField labelWithString:@"车辆作业"];
    title.font = [NSFont systemFontOfSize:26 weight:NSFontWeightSemibold];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:title];

    NSBox *form = [[NSBox alloc] init];
    form.boxType = NSBoxCustom;
    form.cornerRadius = 12.0;
    form.fillColor = [NSColor colorWithSRGBRed:1 green:1 blue:1 alpha:0.05];
    form.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:form];

    _plateField = [[NSTextField alloc] init];
    _plateField.placeholderString = @"例如：晋A12345";
    [_plateField.widthAnchor constraintEqualToConstant:180].active = YES;

    _typePopup = [[NSPopUpButton alloc] init];
    [_typePopup addItemsWithTitles:@[@"轿车", @"摩托车", @"卡车", @"电动车"]];

    _strategyPopup = [[NSPopUpButton alloc] init];
    [_strategyPopup addItemsWithTitles:@[@"加权代价（推荐）", @"最近车位"]];

    NSButton *allocateButton = [NSButton buttonWithTitle:@"自动分配车位" target:self action:@selector(allocate:)];
    NSButton *recognizeButton = [NSButton buttonWithTitle:@"识别图片" target:self action:@selector(recognizeImage:)];
    NSButton *updateTypeButton = [NSButton buttonWithTitle:@"修改车辆类型" target:self action:@selector(updateType:)];
    NSButton *releaseButton = [NSButton buttonWithTitle:@"车辆出库" target:self action:@selector(release:)];
    NSButton *emergencyButton = [NSButton buttonWithTitle:@"应急生命通道入场" target:self action:@selector(emergency:)];

    NSGridView *grid = [NSGridView gridViewWithViews:@[
        @[[NSTextField labelWithString:@"车牌"], _plateField],
        @[[NSTextField labelWithString:@"车辆类型"], _typePopup],
        @[[NSTextField labelWithString:@"分配策略"], _strategyPopup],
    ]];
    grid.rowSpacing = 10.0;
    grid.columnSpacing = 12.0;
    grid.translatesAutoresizingMaskIntoConstraints = NO;

    NSStackView *buttons = [[NSStackView alloc] init];
    buttons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    buttons.spacing = 10.0;
    buttons.translatesAutoresizingMaskIntoConstraints = NO;
    [buttons addArrangedSubview:allocateButton];
    [buttons addArrangedSubview:recognizeButton];
    [buttons addArrangedSubview:updateTypeButton];
    [buttons addArrangedSubview:releaseButton];
    [buttons addArrangedSubview:emergencyButton];

    [form addSubview:grid];
    [form addSubview:buttons];
    [NSLayoutConstraint activateConstraints:@[
        [grid.topAnchor constraintEqualToAnchor:form.topAnchor constant:16],
        [grid.leadingAnchor constraintEqualToAnchor:form.leadingAnchor constant:16],
        [grid.trailingAnchor constraintLessThanOrEqualToAnchor:form.trailingAnchor constant:-16],
        [buttons.topAnchor constraintEqualToAnchor:grid.bottomAnchor constant:16],
        [buttons.leadingAnchor constraintEqualToAnchor:form.leadingAnchor constant:16],
        [buttons.trailingAnchor constraintLessThanOrEqualToAnchor:form.trailingAnchor constant:-16],
        [buttons.bottomAnchor constraintEqualToAnchor:form.bottomAnchor constant:-16],
    ]];

    _statusLabel = [NSTextField wrappingLabelWithString:@"输入车牌与车型后自动分配；出库可输入在场车牌。"];
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
        [_statusLabel.topAnchor constraintEqualToAnchor:form.bottomAnchor constant:16],
        [_statusLabel.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [_statusLabel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
    ]];

    self.view = root;
}

- (void)viewDidLoad{
    [super viewDidLoad];
}

- (NSString *)plate{
    return [_plateField.stringValue stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
}

- (smartpark::VehicleType)selectedType{
    return smartpark_ui::vehicleTypeFromIndex((int)_typePopup.indexOfSelectedItem);
}

- (void)allocate:(id)sender{
    NSString *plate = [self plate];
    if (plate.length == 0){
        _statusLabel.stringValue = @"自动分配前请输入车辆车牌。";
        return;
    }
    self.bridge->setStrategy(_strategyPopup.indexOfSelectedItem == 1
        ? smartpark::AllocationStrategy::Nearest
        : smartpark::AllocationStrategy::WeightedCost);
    auto result = self.bridge->enterVehicle(plate.UTF8String, [self selectedType]);
    if (!result){
        _statusLabel.stringValue = @"无可用车位：当前停车场已满或没有可达车位。";
        return;
    }
    _statusLabel.stringValue = [NSString stringWithFormat:
        @"已分配车位：%s | 入口 %.1f m | 出口 %.1f m | 拥堵 %d | 分区压力 %.1f | 转向 %d | 类型成本 %.1f | 综合评分 %.1f",
        result->spotId.c_str(), result->entryRoute.distance, result->exitRoute.distance,
        result->nearbyOccupiedSpots, result->breakdown.zonePressureCost,
        result->entryRoute.turnCount + result->exitRoute.turnCount,
        result->breakdown.typePenalty, result->score];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"SmartParkDataChanged" object:nil];
}

- (void)updateType:(id)sender{
    NSString *plate = [self plate];
    if (plate.length == 0){
        _statusLabel.stringValue = @"修改车辆类型前请输入在场车辆车牌。";
        return;
    }
    auto newType = [self selectedType];
    bool ok = self.bridge->updateVehicleType(plate.UTF8String, newType);
    _statusLabel.stringValue = ok
        ? [NSString stringWithFormat:@"车辆类型已修改：%s → %s", plate.UTF8String,
            smartpark_ui::vehicleTypeText(newType)]
        : [NSString stringWithFormat:@"无法修改车牌 %s 的车辆类型：车辆不在场。", plate.UTF8String];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"SmartParkDataChanged" object:nil];
}

- (void)release:(id)sender{
    NSString *plate = [self plate];
    if (plate.length == 0){
        _statusLabel.stringValue = @"请先在“当前车位”选中占用车辆，或输入在场车牌。";
        return;
    }
    auto record = self.bridge->leaveVehicle(plate.UTF8String);
    if (!record){
        _statusLabel.stringValue = [NSString stringWithFormat:@"车牌 %s 不存在可离场的在场记录。", plate.UTF8String];
        return;
    }
    auto duration = std::chrono::duration_cast<std::chrono::minutes>(record->duration());
    _statusLabel.stringValue = [NSString stringWithFormat:
        @"离场完成：%s | 车位 %s | 时长 %lld 分钟 | 费用 %.2f 元",
        record->plateNumber().c_str(), record->spotId().c_str(),
        (long long)duration.count(), record->fee()];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"SmartParkDataChanged" object:nil];
}

- (void)emergency:(id)sender{
    NSString *plate = [self plate];
    if (plate.length == 0){
        _statusLabel.stringValue = @"应急入场前请输入车辆车牌。";
        return;
    }
    auto result = self.bridge->emergencyEnter(plate.UTF8String, [self selectedType]);
    _statusLabel.stringValue = result
        ? [NSString stringWithFormat:@"应急入场完成：分配车位 %s。", result->spotId.c_str()]
        : @"应急入场失败：无可用车位。";
    [[NSNotificationCenter defaultCenter] postNotificationName:@"SmartParkDataChanged" object:nil];
}

- (void)refresh{
    // 车辆作业页无只读列表，切换时无需额外刷新。
}

- (void)recognizeImage:(id)sender{
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.allowedContentTypes = @[UTTypeJPEG, UTTypePNG, UTTypeBMP];
    panel.allowsMultipleSelection = NO;
    [panel beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse result){
        if (result != NSModalResponseOK || panel.URL == nil){
            return;
        }
        [self runRecognitionForImage:panel.URL.path];
    }];
}

- (NSString *)resolvePython:(const char *)envVar fallbackDir:(const char *)subdir{
    const char *value = getenv(envVar);
    if (value != nullptr && value[0] != '\0'){
        NSString *path = [NSString stringWithUTF8String:value];
        if ([[NSFileManager defaultManager] fileExistsAtPath:path]){
            return path;
        }
    }
    NSString *fallback = [NSHomeDirectory() stringByAppendingPathComponent:
        [NSString stringWithFormat:@".smartpark/%s/bin/python", subdir]];
    if ([[NSFileManager defaultManager] fileExistsAtPath:fallback]){
        return fallback;
    }
    return nil;
}

- (void)runRecognitionForImage:(NSString *)imagePath{
    NSString *detectorPython = [self resolvePython:"SMARTPARK_LPR_PY" fallbackDir:"lpr"];
    NSString *ocrPython = [self resolvePython:"SMARTPARK_OCR_PY" fallbackDir:"ocr"];
    NSString *script = @SMARTPARK_LPR_SCRIPT_PATH;

    if (detectorPython == nil || ocrPython == nil
        || ![[NSFileManager defaultManager] fileExistsAtPath:script]){
        _statusLabel.stringValue = @"请配置 SMARTPARK_LPR_PY、SMARTPARK_OCR_PY 与识别脚本路径。";
        return;
    }

    _statusLabel.stringValue = @"正在识别车牌，请稍候…";

    NSTask *task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:detectorPython];
    task.arguments = @[script, imagePath, @"--ocr-python", ocrPython];

    NSMutableDictionary<NSString *, NSString *> *environment =
        [[[NSProcessInfo processInfo] environment] mutableCopy];
    environment[@"PYTHONDONTWRITEBYTECODE"] = @"1";
    environment[@"PYTHONIOENCODING"] = @"utf-8";
    task.environment = environment;

    NSPipe *outPipe = [NSPipe pipe];
    NSPipe *errPipe = [NSPipe pipe];
    task.standardOutput = outPipe;
    task.standardError = errPipe;

    __weak OperationsViewController *weakSelf = self;
    task.terminationHandler = ^(NSTask *t){
        NSData *outData = outPipe.fileHandleForReading.readDataToEndOfFile;
        NSData *errData = errPipe.fileHandleForReading.readDataToEndOfFile;
        NSString *outStr = [[NSString alloc] initWithData:outData encoding:NSUTF8StringEncoding];
        NSString *errStr = [[NSString alloc] initWithData:errData encoding:NSUTF8StringEncoding];
        int exitCode = t.terminationStatus;
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakSelf handleRecognitionOutput:outStr error:errStr exitCode:exitCode];
        });
    };

    NSError *error = nil;
    [task launchAndReturnError:&error];
    if (error){
        _statusLabel.stringValue = [NSString stringWithFormat:@"无法启动识别程序：%@", error.localizedDescription];
    }
}

- (void)handleRecognitionOutput:(NSString *)output error:(NSString *)error exitCode:(int)exitCode{
    if (exitCode != 0){
        _statusLabel.stringValue = error.length > 0 ? error : @"识别程序异常退出。";
        return;
    }
    NSData *data = [output dataUsingEncoding:NSUTF8StringEncoding];
    NSError *jsonError = nil;
    id obj = [NSJSONSerialization JSONObjectWithData:data options:0 error:&jsonError];
    if (jsonError || ![obj isKindOfClass:[NSDictionary class]]){
        _statusLabel.stringValue = @"识别返回的数据格式无效。";
        return;
    }
    NSDictionary *result = obj;
    NSString *plate = result[@"plate"];
    if (![plate isKindOfClass:[NSString class]] || plate.length == 0){
        _statusLabel.stringValue = @"未识别到车牌。";
        return;
    }
    NSNumber *det = result[@"detection_confidence"];
    NSNumber *rec = result[@"recognition_confidence"];
    double detV = [det isKindOfClass:[NSNumber class]] ? det.doubleValue : 0.0;
    double recV = [rec isKindOfClass:[NSNumber class]] ? rec.doubleValue : 0.0;

    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"识别到候选车牌";
    alert.informativeText = [NSString stringWithFormat:
        @"车牌：%@\n检测置信度 %.1f%% · 识别置信度 %.1f%%\n是否采用该车牌？",
        plate, detV * 100.0, recV * 100.0];
    [alert addButtonWithTitle:@"采用"];
    [alert addButtonWithTitle:@"取消"];
    [alert beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse response){
        if (response == NSAlertFirstButtonReturn){
            _plateField.stringValue = plate;
            _statusLabel.stringValue = [NSString stringWithFormat:
                @"已采用候选车牌 %@，请核对后再操作入场或出场。", plate];
        }
    }];
}

@end
