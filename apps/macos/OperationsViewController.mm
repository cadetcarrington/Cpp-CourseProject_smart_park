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
    NSTask *_recognitionTask;
    BOOL _recognitionTimedOut;
    BOOL _recognitionCancelled;
    NSString *_selectedPlate;
}

- (instancetype)initWithNibName:(NSNibName)nibNameOrNil bundle:(NSBundle *)nibBundleOrNil{
    if ((self = [super initWithNibName:nibNameOrNil bundle:nibBundleOrNil])){
        [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(vehicleSelected:) name:@"SmartParkVehicleSelected" object:nil];
    }
    return self;
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
    NSButton *cancelRecognitionButton = [NSButton buttonWithTitle:@"取消识别" target:self action:@selector(cancelRecognition:)];
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

    NSStackView *primaryButtons = [NSStackView stackViewWithViews:@[
        allocateButton, updateTypeButton, releaseButton
    ]];
    primaryButtons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    primaryButtons.spacing = 10.0;
    NSStackView *secondaryButtons = [NSStackView stackViewWithViews:@[
        recognizeButton, cancelRecognitionButton, emergencyButton
    ]];
    secondaryButtons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    secondaryButtons.spacing = 10.0;
    NSStackView *buttons = [NSStackView stackViewWithViews:@[
        primaryButtons, secondaryButtons
    ]];
    buttons.orientation = NSUserInterfaceLayoutOrientationVertical;
    buttons.alignment = NSLayoutAttributeLeading;
    buttons.spacing = 8.0;
    buttons.translatesAutoresizingMaskIntoConstraints = NO;

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
    if (_selectedPlate.length > 0){
        _plateField.stringValue = _selectedPlate;
    }
}

- (void)dealloc{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    if (_recognitionTask.running){
        [_recognitionTask terminate];
    }
}

- (void)vehicleSelected:(NSNotification *)notification{
    NSString *plate = notification.userInfo[@"plate"];
    if ([plate isKindOfClass:[NSString class]]){
        _selectedPlate = [plate copy];
        if (self.isViewLoaded){
            _plateField.stringValue = plate;
        }
    }
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
        @"已分配车位：%@ | 入口 %.1f m | 出口 %.1f m | 拥堵 %d | 分区压力 %.1f | 转向 %d | 类型成本 %.1f | 综合评分 %.1f",
        smartpark_ui::toNSString(result->spotId), result->entryRoute.distance, result->exitRoute.distance,
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
    // 远程模式没有对应 action：直接说清楚，否则下面会误报成「车辆不在场」。
    if (!self.bridge->capabilities().vehicleTypeEdit){
        _statusLabel.stringValue =
            @"远程模式暂不支持车型更正：请由服务端或 Gate 修正。";
        return;
    }
    auto newType = [self selectedType];
    bool ok = self.bridge->updateVehicleType(plate.UTF8String, newType);
    _statusLabel.stringValue = ok
        ? [NSString stringWithFormat:@"车辆类型已修改：%@ → %@", plate,
            smartpark_ui::toNSString(smartpark_ui::vehicleTypeText(newType))]
        : [NSString stringWithFormat:@"无法修改车牌 %@ 的车辆类型：车辆不在场。", plate];
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
        _statusLabel.stringValue = [NSString stringWithFormat:@"车牌 %@ 不存在可离场的在场记录。", plate];
        return;
    }
    auto duration = std::chrono::duration_cast<std::chrono::minutes>(record->duration());
    _statusLabel.stringValue = [NSString stringWithFormat:
        @"离场完成：%@ | 车位 %@ | 时长 %lld 分钟 | 费用 %.2f 元",
        smartpark_ui::toNSString(record->plateNumber()),
        smartpark_ui::toNSString(record->spotId()),
        (long long)duration.count(), record->fee()];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"SmartParkDataChanged" object:nil];
}

- (void)emergency:(id)sender{
    NSString *plate = [self plate];
    if (plate.length == 0){
        _statusLabel.stringValue = @"应急入场前请输入车辆车牌。";
        return;
    }
    // 应急生命通道在协议里没有 action，远程模式只能如实说明不可用，
    // 不能让它落到「无可用车位」——那是另一回事。
    if (!self.bridge->capabilities().emergencyEnter){
        _statusLabel.stringValue =
            @"远程模式不提供应急生命通道入场（协议暂无对应接口），请在服务端操作。";
        return;
    }
    auto result = self.bridge->emergencyEnter(plate.UTF8String, [self selectedType]);
    _statusLabel.stringValue = result
        ? [NSString stringWithFormat:@"应急入场完成：分配车位 %@。",
            smartpark_ui::toNSString(result->spotId)]
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

- (void)cancelRecognition:(id)sender{
    if (_recognitionTask.running){
        _recognitionCancelled = YES;
        [_recognitionTask terminate];
        _statusLabel.stringValue = @"已取消识别。";
    }
}

- (void)runRecognitionForImage:(NSString *)imagePath{
    if (_recognitionTask.running){
        _statusLabel.stringValue = @"识别仍在进行，请等待或先取消。";
        return;
    }
    // 远程模式：识别在服务端跑。本机不再拉模型、也不需要 Python 识别环境——
    // 服务端有 --lpr-command 配好的检测+OCR，客户端只负责把照片发过去。
    if (self.bridge != nullptr && self.bridge->supportsRemoteRecognition()){
        [self runRemoteRecognitionForImage:imagePath];
        return;
    }
    NSString *detectorPython = [self resolvePython:"SMARTPARK_LPR_PY" fallbackDir:"lpr"];
    NSString *ocrPython = [self resolvePython:"SMARTPARK_OCR_PY" fallbackDir:"ocr"];
    const char *override = getenv("SMARTPARK_LPR_SCRIPT");
    NSString *script = override != nullptr && override[0] != '\0'
        ? [NSString stringWithUTF8String:override] : @SMARTPARK_LPR_SCRIPT_PATH;

    if (detectorPython == nil || ocrPython == nil
        || ![[NSFileManager defaultManager] fileExistsAtPath:script]){
        _statusLabel.stringValue = @"请配置 SMARTPARK_LPR_PY、SMARTPARK_OCR_PY 与识别脚本路径。";
        return;
    }

    _statusLabel.stringValue = @"正在识别车牌，请稍候…";
    _recognitionTimedOut = NO;
    _recognitionCancelled = NO;

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

    NSError *error = nil;
    if (![task launchAndReturnError:&error]){
        _statusLabel.stringValue = [NSString stringWithFormat:@"无法启动识别程序：%@", error.localizedDescription];
        return;
    }
    _recognitionTask = task;
    __weak OperationsViewController *weakSelf = self;
    dispatch_group_t readers = dispatch_group_create();
    __block NSData *outData;
    __block NSData *errData;
    dispatch_group_async(readers, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        outData = [outPipe.fileHandleForReading readDataToEndOfFile];
    });
    dispatch_group_async(readers, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        errData = [errPipe.fileHandleForReading readDataToEndOfFile];
    });
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        [task waitUntilExit];
        dispatch_group_wait(readers, DISPATCH_TIME_FOREVER);
        NSString *output = [[NSString alloc] initWithData:outData encoding:NSUTF8StringEncoding] ?: @"";
        NSString *details = [[NSString alloc] initWithData:errData encoding:NSUTF8StringEncoding] ?: @"";
        dispatch_async(dispatch_get_main_queue(), ^{
            OperationsViewController *controller = weakSelf;
            if (controller == nil || controller->_recognitionTask != task){
                return;
            }
            controller->_recognitionTask = nil;
            if (controller->_recognitionTimedOut){
                controller->_statusLabel.stringValue = @"识别超时，请重试。";
            } else if (!controller->_recognitionCancelled){
                [controller handleRecognitionOutput:output error:details exitCode:task.terminationStatus];
            }
        });
    });
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 120 * NSEC_PER_SEC),
                   dispatch_get_main_queue(), ^{
        OperationsViewController *controller = weakSelf;
        if (controller != nil && controller->_recognitionTask == task && task.running){
            controller->_recognitionTimedOut = YES;
            [task terminate];
        }
    });
}

// 把照片发给服务端识别（异步，不阻塞界面）。
- (void)runRemoteRecognitionForImage:(NSString *)imagePath{
    NSData *data = [NSData dataWithContentsOfFile:imagePath];
    if (data.length == 0){
        _statusLabel.stringValue = @"无法读取所选图片。";
        return;
    }
    if (data.length > 6 * 1024 * 1024){
        _statusLabel.stringValue = @"图片超过 6MB，请换一张小一点的。";
        return;
    }
    _statusLabel.stringValue = @"正在由服务端识别车牌，请稍候…";
    QByteArray bytes(reinterpret_cast<const char *>(data.bytes),
                     static_cast<int>(data.length));
    ParkingBridge *bridge = self.bridge;
    __weak OperationsViewController *weakSelf = self;
    bridge->recognizePlateRemotely(bytes, [weakSelf](smartpark::ParkingDataSource::PlateRecognition outcome){
        dispatch_async(dispatch_get_main_queue(), ^{
            OperationsViewController *controller = weakSelf;
            if (controller == nil){
                return;
            }
            [controller presentRemoteRecognition:outcome];
        });
    });
}

- (void)presentRemoteRecognition:(smartpark::ParkingDataSource::PlateRecognition)outcome{
    if (!outcome.ok){
        _statusLabel.stringValue = [NSString stringWithFormat:@"服务端识别失败：%s",
            outcome.error.c_str()];
        return;
    }
    NSString *plate = [NSString stringWithUTF8String:outcome.plate.c_str()];
    if (plate.length == 0){
        _statusLabel.stringValue = @"服务端未识别到车牌。";
        return;
    }
    // mock 后端是按图片哈希编的车牌，必须说清楚，别让人当成真识别结果。
    const bool mock = outcome.backend == "mock";
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = mock ? @"服务端返回候选车牌（模拟识别）" : @"服务端识别到候选车牌";
    alert.informativeText = [NSString stringWithFormat:
        @"车牌：%@\n识别置信度 %.1f%% · 后端 %s%@\n是否采用该车牌？",
        plate, outcome.confidence * 100.0, outcome.backend.c_str(),
        mock ? @"\n（服务端未配置 --lpr-command，这是演示用结果）" : @""];
    [alert addButtonWithTitle:@"采用"];
    [alert addButtonWithTitle:@"取消"];
    [alert beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse response){
        if (response == NSAlertFirstButtonReturn){
            _plateField.stringValue = plate;
            _statusLabel.stringValue = [NSString stringWithFormat:
                @"已采用服务端识别的车牌 %@，请核对后再操作入场或出场。", plate];
        }
    }];
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
