#import "OperationsViewController.h"

#import "TextUtil.h"
#import "LocalRecognitionService.h"
#import "PlatePhotoArchive.h"
#import "RecognitionConfidence.h"
#import "bridge/ParkingBridge.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cstdlib>

// 识别之后的自动动作：入库识别自动入场、出库识别自动出场（不再要求再点一次按钮）。
typedef NS_ENUM(NSInteger, SmartParkRecognitionIntent){
    SmartParkRecognitionIntentNone = 0,
    SmartParkRecognitionIntentEnter,
    SmartParkRecognitionIntentExit,
};

@implementation OperationsViewController{
    NSTextField *_plateField;
    NSPopUpButton *_typePopup;
    NSPopUpButton *_strategyPopup;
    NSTextField *_statusLabel;
    NSImageView *_imageView;
    NSTask *_recognitionTask;
    NSTask *_localRecognitionService;
    BOOL _usesSharedRecognitionService;
    NSString *_localRecognitionSocket;
    NSString *_localRecognitionError;
    BOOL _recognitionTimedOut;
    BOOL _recognitionCancelled;
    NSString *_selectedPlate;
    // 本次识别要办的事 + 原图路径（识别结果里没有裁剪图时拿它兜底归档）。
    SmartParkRecognitionIntent _recognitionIntent;
    NSString *_recognitionImagePath;
    // 本次入场刚归档的照片：只交给紧接着的那次 allocate:，手工入场不会沿用旧照片。
    NSString *_pendingEntryPhotoPath;
    // 右下角结果面板：出库后显示车辆信息 / 停车时间 / 应付款。
    NSBox *_resultCard;
    NSTextField *_resultTitle;
    NSArray<NSTextField *> *_resultValueLabels;
    NSTextField *_resultPhotoLabel;
    NSButton *_revealPhotoButton;
    NSString *_lastEntryPhotoPath;
}

- (instancetype)initWithNibName:(NSNibName)nibNameOrNil bundle:(NSBundle *)nibBundleOrNil{
    if ((self = [super initWithNibName:nibNameOrNil bundle:nibBundleOrNil])){
        [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(vehicleSelected:) name:@"SmartParkVehicleSelected" object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(stopLocalRecognitionService:)
            name:NSApplicationWillTerminateNotification object:nil];
    }
    return self;
}

- (void)loadView{
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];


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
    // 识别即办理：识别成功直接入场/出场，不用再点一次按钮。
    NSButton *recognizeEnterButton = [NSButton buttonWithTitle:@"识别并入场"
                                                        target:self
                                                        action:@selector(recognizeAndEnter:)];
    NSButton *recognizeExitButton = [NSButton buttonWithTitle:@"识别并出场"
                                                       target:self
                                                       action:@selector(recognizeAndExit:)];
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
        recognizeEnterButton, recognizeExitButton, cancelRecognitionButton, emergencyButton
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
    _statusLabel.font = [NSFont systemFontOfSize:13];
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_statusLabel];

    // 车牌预览
    NSVisualEffectView *imageCard = [[NSVisualEffectView alloc] init];
    imageCard.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    imageCard.material = NSVisualEffectMaterialUnderWindowBackground;
    imageCard.state = NSVisualEffectStateActive;
    imageCard.wantsLayer = YES;
    imageCard.layer.cornerRadius = 15.0;
    imageCard.layer.masksToBounds = YES;
    imageCard.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:imageCard];

    _imageView = [[NSImageView alloc] init];
    _imageView.imageScaling = NSImageScaleProportionallyUpOrDown;
    _imageView.translatesAutoresizingMaskIntoConstraints = NO;
    [imageCard addSubview:_imageView];

    [NSLayoutConstraint activateConstraints:@[
        [_imageView.topAnchor constraintEqualToAnchor:imageCard.topAnchor constant:8],
        [_imageView.bottomAnchor constraintEqualToAnchor:imageCard.bottomAnchor constant:-8],
        [_imageView.leadingAnchor constraintEqualToAnchor:imageCard.leadingAnchor constant:8],
        [_imageView.trailingAnchor constraintEqualToAnchor:imageCard.trailingAnchor constant:-8],
    ]];

    // 右下角：上一次入场/出库的结果（出库时给出车辆信息、停车时间与应付款）。
    _resultCard = [[NSBox alloc] init];
    _resultCard.boxType = NSBoxCustom;
    _resultCard.cornerRadius = 12.0;
    _resultCard.fillColor = [NSColor colorWithSRGBRed:1 green:1 blue:1 alpha:0.05];
    _resultCard.titlePosition = NSNoTitle;
    _resultCard.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:_resultCard];

    _resultTitle = [NSTextField labelWithString:@"办理结果"];
    _resultTitle.font = [NSFont systemFontOfSize:16 weight:NSFontWeightSemibold];
    _resultTitle.translatesAutoresizingMaskIntoConstraints = NO;
    [_resultCard addSubview:_resultTitle];

    NSArray<NSString *> *resultKeys = @[@"车牌", @"车型", @"车位",
                                        @"入场时间", @"出场时间", @"停车时长", @"应付款"];
    NSMutableArray<NSArray<NSView *> *> *resultRows = [NSMutableArray array];
    NSMutableArray<NSTextField *> *resultValues = [NSMutableArray array];
    for (NSString *key in resultKeys){
        NSTextField *name = [NSTextField labelWithString:key];
        name.textColor = [NSColor secondaryLabelColor];
        // 「正常字体」：正文用 14pt 常规字重，不用标题那种 semibold。
        name.font = [NSFont systemFontOfSize:15 weight:NSFontWeightRegular];
        NSTextField *value = [NSTextField labelWithString:@"—"];
        value.font = [NSFont systemFontOfSize:15 weight:NSFontWeightRegular];
        value.selectable = YES;   // 金额/时间可以直接选中复制
        value.lineBreakMode = NSLineBreakByTruncatingMiddle;
        [resultValues addObject:value];
        [resultRows addObject:@[name, value]];
    }
    _resultValueLabels = resultValues;
    NSGridView *resultGrid = [NSGridView gridViewWithViews:resultRows];
    resultGrid.rowSpacing = 6.0;
    resultGrid.columnSpacing = 14.0;
    [resultGrid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    resultGrid.translatesAutoresizingMaskIntoConstraints = NO;
    [_resultCard addSubview:resultGrid];

    _resultPhotoLabel = [NSTextField wrappingLabelWithString:@"入场车牌照片：—"];
    _resultPhotoLabel.textColor = [NSColor secondaryLabelColor];
    _resultPhotoLabel.font = [NSFont systemFontOfSize:13];
    _resultPhotoLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [_resultCard addSubview:_resultPhotoLabel];

    _revealPhotoButton = [NSButton buttonWithTitle:@"在访达中显示"
                                            target:self
                                            action:@selector(revealArchivedPhoto:)];
    _revealPhotoButton.enabled = NO;
    _revealPhotoButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_resultCard addSubview:_revealPhotoButton];

    [NSLayoutConstraint activateConstraints:@[
        [_resultTitle.topAnchor constraintEqualToAnchor:_resultCard.topAnchor constant:14],
        [_resultTitle.leadingAnchor constraintEqualToAnchor:_resultCard.leadingAnchor constant:16],
        [resultGrid.topAnchor constraintEqualToAnchor:_resultTitle.bottomAnchor constant:10],
        [resultGrid.leadingAnchor constraintEqualToAnchor:_resultCard.leadingAnchor constant:16],
        [resultGrid.trailingAnchor constraintLessThanOrEqualToAnchor:_resultCard.trailingAnchor constant:-16],
        [_resultPhotoLabel.topAnchor constraintEqualToAnchor:resultGrid.bottomAnchor constant:12],
        [_resultPhotoLabel.leadingAnchor constraintEqualToAnchor:_resultCard.leadingAnchor constant:16],
        [_resultPhotoLabel.trailingAnchor constraintEqualToAnchor:_resultCard.trailingAnchor constant:-16],
        [_revealPhotoButton.topAnchor constraintEqualToAnchor:_resultPhotoLabel.bottomAnchor constant:8],
        [_revealPhotoButton.leadingAnchor constraintEqualToAnchor:_resultCard.leadingAnchor constant:16],
        [_revealPhotoButton.bottomAnchor constraintLessThanOrEqualToAnchor:_resultCard.bottomAnchor constant:-14],
    ]];

    [NSLayoutConstraint activateConstraints:@[
        // 标题已交给窗口工具栏（原生）显示，内容从安全区（栏下方）开始。
        [form.topAnchor constraintEqualToAnchor:root.safeAreaLayoutGuide.topAnchor constant:18],
        [form.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [form.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [imageCard.topAnchor constraintEqualToAnchor:form.bottomAnchor constant:16],
        [imageCard.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [imageCard.widthAnchor constraintEqualToAnchor:root.widthAnchor multiplier:0.5 constant:-24],
        [imageCard.heightAnchor constraintEqualToConstant:300],
        // 结果面板贴在同一行右侧：图像在左、结果在右，出库结算一眼能看到。
        [_resultCard.topAnchor constraintEqualToAnchor:imageCard.topAnchor],
        [_resultCard.leadingAnchor constraintEqualToAnchor:imageCard.trailingAnchor constant:16],
        [_resultCard.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [_resultCard.bottomAnchor constraintEqualToAnchor:imageCard.bottomAnchor],
        [_statusLabel.topAnchor constraintEqualToAnchor:imageCard.bottomAnchor constant:12],
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
    // Keep application-owned models alive across logout/window replacement.
    if (!_usesSharedRecognitionService){ [self stopLocalRecognitionService:nil]; }
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
    // 这次分配的路线交给车位图：地图上直接画出进场/出场路线。
    self.bridge->setPlannedRoute(result->entryRoute, result->exitRoute);
    // 右下角结果面板：车辆信息 + 入场时间 + 留档照片（出库时按车牌取回这张）。
    // 照片只认本次识别刚归档的那张；手工输入车牌入场时没有照片，如实显示「—」。
    NSString *entryPhoto = _pendingEntryPhotoPath;
    _pendingEntryPhotoPath = nil;
    [self showResult:@"入场办理"
              values:@[plate,
                       smartpark_ui::toNSString(
                           smartpark_ui::vehicleTypeText([self selectedType])),
                       smartpark_ui::toNSString(result->spotId),
                       smartpark_ui::toNSString(smartpark_ui::formatTime(
                           smartpark::ParkingRecord::Clock::now())),
                       @"—", @"—", @"—"]
               photo:entryPhoto];
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
        // 结算成功但时间待确认、或服务端拒绝，都要说清原因，不能都推给「不在场」。
        NSString *reason = smartpark_ui::toNSString(self.bridge->lastError());
        _statusLabel.stringValue = reason.length > 0
            ? [NSString stringWithFormat:@"出库未完成：%@", reason]
            : [NSString stringWithFormat:@"车牌 %@ 不存在可离场的在场记录。", plate];
        return;
    }
    // 车已离场，之前那条进场路线不再代表任何东西，别留在地图上。
    self.bridge->clearPlannedRoute();
    auto duration = std::chrono::duration_cast<std::chrono::minutes>(record->duration());
    _statusLabel.stringValue = [NSString stringWithFormat:
        @"离场完成：%@ | 车位 %@ | 时长 %lld 分钟 | 费用 %.2f 元",
        smartpark_ui::toNSString(record->plateNumber()),
        smartpark_ui::toNSString(record->spotId()),
        (long long)duration.count(), record->fee()];

    // 入库时留档的车牌照片：出库时取回同车牌最近一张，放在预览区做人工核对。
    NSString *entryPhoto = [PlatePhotoArchive latestPhotoForPlate:
        smartpark_ui::toNSString(record->plateNumber())];
    if (entryPhoto.length > 0){
        NSImage *image = [[NSImage alloc] initWithContentsOfFile:entryPhoto];
        if (image != nil){
            _imageView.image = image;
        }
    }
    [self showResult:@"出库结算"
              values:@[smartpark_ui::toNSString(record->plateNumber()),
                       smartpark_ui::toNSString(
                           smartpark_ui::vehicleTypeText(record->vehicleType())),
                       smartpark_ui::toNSString(record->spotId()),
                       smartpark_ui::toNSString(smartpark_ui::formatTime(record->entryTime())),
                       record->exitTime()
                           ? smartpark_ui::toNSString(
                                 smartpark_ui::formatTime(*record->exitTime()))
                           : @"—",
                       [self durationText:duration],
                       [NSString stringWithFormat:@"%.2f 元", record->fee()]]
               photo:entryPhoto];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"SmartParkDataChanged" object:nil];
}

// 停车时长按「X 小时 Y 分钟」写；不足一小时只说分钟，避免出现「0 小时」。
- (NSString *)durationText:(std::chrono::minutes)duration{
    const long long minutes = duration.count();
    if (minutes < 60){
        return [NSString stringWithFormat:@"%lld 分钟", minutes];
    }
    return [NSString stringWithFormat:@"%lld 小时 %lld 分钟",
            minutes / 60, minutes % 60];
}

// 右下角结果面板：title + 七行固定字段（车牌/车型/车位/入场/出场/时长/应付款）
// + 留档照片。photo 为空表示这张单子没有留档照片（例如手工输入车牌入场）。
- (void)showResult:(NSString *)title
            values:(NSArray<NSString *> *)values
             photo:(NSString *)photoPath{
    _resultTitle.stringValue = title.length > 0 ? title : @"办理结果";
    for (NSUInteger index = 0; index < _resultValueLabels.count; ++index){
        _resultValueLabels[index].stringValue = index < values.count ? values[index] : @"—";
    }
    _lastEntryPhotoPath = photoPath.length > 0 ? [photoPath copy] : nil;
    NSString *directory = [PlatePhotoArchive directoryWithError:nil] ?: @"—";
    _resultPhotoLabel.stringValue = photoPath.length > 0
        ? [NSString stringWithFormat:@"入场车牌照片：%@\n照片目录：%@",
            photoPath.lastPathComponent, directory]
        : [NSString stringWithFormat:@"入场车牌照片：—（手工入场无照片）\n照片目录：%@",
            directory];
    _revealPhotoButton.enabled = photoPath.length > 0;
}

- (void)revealArchivedPhoto:(id)sender{
    if (_lastEntryPhotoPath.length == 0){
        return;
    }
    [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:
        @[[NSURL fileURLWithPath:_lastEntryPhotoPath]]];
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

// 识别即办理：选图 → 识别 → 直接入场/出场，不再要求再点一次按钮。
- (void)recognizeAndEnter:(id)sender{
    [self beginRecognitionWithIntent:SmartParkRecognitionIntentEnter];
}

- (void)recognizeAndExit:(id)sender{
    [self beginRecognitionWithIntent:SmartParkRecognitionIntentExit];
}

- (void)beginRecognitionWithIntent:(SmartParkRecognitionIntent)intent{
    if (_recognitionTask != nil){
        _statusLabel.stringValue = @"识别仍在进行，请等待或先取消。";
        return;
    }
    _recognitionIntent = intent;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.allowedContentTypes = @[UTTypeJPEG, UTTypePNG, UTTypeBMP];
    panel.allowsMultipleSelection = NO;
    panel.message = intent == SmartParkRecognitionIntentExit
        ? @"选择出库车辆的车牌照片：识别成功后自动办理出库"
        : @"选择入场车辆的车牌照片：识别成功后自动入库并把照片留档";
    [panel beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse result){
        if (result != NSModalResponseOK || panel.URL == nil){
            _recognitionIntent = SmartParkRecognitionIntentNone;
            return;
        }
        _recognitionImagePath = panel.URL.path;
        _imageView.image = nil;  // 预览留给第一个模型（检测器）裁出的车牌图
        [self runRecognitionForImage:panel.URL.path];
    }];
}

// 识别成功后按意图直接办理；照片先归档再入场，出库时才能按车牌取回核对。
- (void)performRecognitionIntentWithPlate:(NSString *)plate
                                   result:(NSDictionary *)result{
    const SmartParkRecognitionIntent intent = _recognitionIntent;
    _recognitionIntent = SmartParkRecognitionIntentNone;
    _plateField.stringValue = plate;

    if (intent == SmartParkRecognitionIntentExit){
        [self release:nil];
        return;
    }

    // 优先归档检测器裁出的车牌图（就是喂给识别模型的那张），缺了退回原图。
    NSString *source = result[@"crop_image"];
    if (![source isKindOfClass:[NSString class]] || source.length == 0){
        source = _recognitionImagePath;
    }
    NSError *archiveError = nil;
    NSString *archived = [PlatePhotoArchive archiveImageAtPath:source
                                                          plate:plate
                                                          error:&archiveError];
    if (archived.length == 0){
        // 归档失败不影响入场，但要如实说出来（照片是出库核对的依据）。
        _statusLabel.stringValue = [NSString stringWithFormat:
            @"识别到 %@，但车牌照片留档失败：%@", plate,
            archiveError.localizedDescription ?: @"未知原因"];
    }
    // 交给紧接着的 allocate: 显示在结果面板里；手工入场那条路不会拿到它。
    _pendingEntryPhotoPath = archived.length > 0 ? archived : nil;
    [self allocate:nil];
    if (archived.length > 0){
        _statusLabel.stringValue = [NSString stringWithFormat:
            @"%@ | 识别车牌 %@，照片已留档：%@",
            _statusLabel.stringValue, plate, archived.lastPathComponent];
    }
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
    if (_recognitionTask != nil){
        _recognitionCancelled = YES;
        if (_recognitionTask.running){
            [_recognitionTask terminate];
        }
        [self stopLocalRecognitionService:nil];
        _statusLabel.stringValue = @"已取消识别。";
    }
}

- (void)runRecognitionForImage:(NSString *)imagePath{
    if (_recognitionTask != nil){
        _statusLabel.stringValue = @"识别仍在进行，请等待或先取消。";
        return;
    }
    // 图片始终留在本机识别；连接远程时，仅停车业务继续交给 bridge。
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

    _statusLabel.stringValue = @"正在本机识别车牌，请稍候…";
    _recognitionTimedOut = NO;
    _recognitionCancelled = NO;

    NSTask *task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:detectorPython];
    task.arguments = @[script, imagePath, @"--ocr-python", ocrPython];

    NSMutableDictionary<NSString *, NSString *> *environment =
        [[[NSProcessInfo processInfo] environment] mutableCopy];
    environment[@"PYTHONDONTWRITEBYTECODE"] = @"1";
    environment[@"PYTHONIOENCODING"] = @"utf-8";
    environment[@"YOLO_OFFLINE"] = @"true";
    // 让识别脚本把第一个模型（检测器）裁出的车牌图落盘，供下方预览显示。
    NSString *previewDirectory = [NSTemporaryDirectory()
        stringByAppendingPathComponent:@"smartpark-lpr-preview"];
    [[NSFileManager defaultManager] removeItemAtPath:previewDirectory error:nil];
    [[NSFileManager defaultManager] createDirectoryAtPath:previewDirectory
        withIntermediateDirectories:YES attributes:nil error:nil];
    environment[@"SMARTPARK_LPR_CROP_DIR"] = previewDirectory;
    task.environment = environment;
    // Custom scripts retain their single-image CLI contract. The bundled
    // pipeline uses an application-owned resident service in either mode.
    if (override == nullptr || override[0] == '\0'){
        NSString *socket = [self localRecognitionSocketForScript:script
            detectorPython:detectorPython ocrPython:ocrPython environment:environment];
        if (socket == nil){
            return;
        }
        NSString *client = [[script stringByDeletingLastPathComponent]
            stringByAppendingPathComponent:@"lpr_client.py"];
        NSMutableArray *arguments = [@[@"-B", client, imagePath, @"--socket", socket,
                                       @"--wait-ready", @"90"] mutableCopy];
        if (_localRecognitionService != nil){
            [arguments addObjectsFromArray:@[@"--service-pid",
                [NSString stringWithFormat:@"%d", _localRecognitionService.processIdentifier]]];
        }
        task.arguments = arguments;
    }

    NSPipe *outPipe = [NSPipe pipe];
    NSPipe *errPipe = [NSPipe pipe];
    task.standardOutput = outPipe;
    task.standardError = errPipe;

    NSError *error = nil;
    if (![task launchAndReturnError:&error]){
        [self stopLocalRecognitionService:nil];
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
                NSString *serviceError = controller->_usesSharedRecognitionService
                    ? [LocalRecognitionService sharedService].diagnostics : controller->_localRecognitionError;
                NSString *diagnostics = [details stringByAppendingString:serviceError ?: @""];
                if (task.terminationStatus != 0){
                    [controller stopLocalRecognitionService:nil];
                }
                [controller handleRecognitionOutput:output error:diagnostics exitCode:task.terminationStatus];
            }
        });
    });
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 120 * NSEC_PER_SEC),
                   dispatch_get_main_queue(), ^{
        OperationsViewController *controller = weakSelf;
        if (controller != nil && controller->_recognitionTask == task && task.running){
            controller->_recognitionTimedOut = YES;
            [task terminate];
            [controller stopLocalRecognitionService:nil];
        }
    });
}

// A controller owns its model process and a private, short socket name.
- (void)stopLocalRecognitionService:(NSNotification *)notification{
    if (notification != nil){
        _recognitionCancelled = YES;
        if (_recognitionTask.running){
            [_recognitionTask terminate];
        }
        // Completion checks this identity before touching the view.
        _recognitionTask = nil;
    }
    if (_usesSharedRecognitionService){
        [[LocalRecognitionService sharedService] stop];
    } else if (_localRecognitionService.running){
        [_localRecognitionService terminate];
    }
    _localRecognitionService = nil;
    _localRecognitionSocket = nil;
}

- (NSString *)localRecognitionSocketForScript:(NSString *)script
                              detectorPython:(NSString *)detectorPython
                                   ocrPython:(NSString *)ocrPython
                                 environment:(NSDictionary<NSString *, NSString *> *)environment{
    if ([script isEqualToString:@SMARTPARK_LPR_SCRIPT_PATH]){
        _usesSharedRecognitionService = YES;
        LocalRecognitionService *shared = [LocalRecognitionService sharedService];
        NSString *socket = [shared startWithScript:script detectorPython:detectorPython
            ocrPython:ocrPython environment:environment];
        _localRecognitionService = shared.task;
        _localRecognitionSocket = socket;
        return socket;
    }
    _usesSharedRecognitionService = NO;
    if (_localRecognitionService.running){
        return _localRecognitionSocket;
    }
    NSString *directory = [script stringByDeletingLastPathComponent];
    NSString *service = [directory stringByAppendingPathComponent:@"lpr_service.py"];
    NSString *client = [directory stringByAppendingPathComponent:@"lpr_client.py"];
    if (![[NSFileManager defaultManager] fileExistsAtPath:service]
        || ![[NSFileManager defaultManager] fileExistsAtPath:client]){
        _statusLabel.stringValue = @"本机常驻识别脚本缺失，请检查安装目录。";
        return nil;
    }
    NSString *nonce = [[NSUUID UUID].UUIDString substringToIndex:16];
    NSString *socket = [NSTemporaryDirectory() stringByAppendingPathComponent:
        [NSString stringWithFormat:@"sp-lpr-%@.sock", nonce]];
    NSTask *serviceTask = [[NSTask alloc] init];
    serviceTask.executableURL = [NSURL fileURLWithPath:detectorPython];
    serviceTask.arguments = @[@"-B", service, @"--socket", socket, @"--ocr-python", ocrPython];
    serviceTask.environment = environment;
    serviceTask.standardOutput = [NSFileHandle fileHandleWithNullDevice];
    NSPipe *errors = [NSPipe pipe];
    serviceTask.standardError = errors;
    NSError *error = nil;
    if (![serviceTask launchAndReturnError:&error]){
        _statusLabel.stringValue = [NSString stringWithFormat:@"无法启动本机推理：%@", error.localizedDescription];
        return nil;
    }
    _localRecognitionError = nil;
    _localRecognitionService = serviceTask;
    _localRecognitionSocket = socket;
    __weak OperationsViewController *weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        NSData *data = [errors.fileHandleForReading readDataToEndOfFile];
        [serviceTask waitUntilExit];
        NSString *details = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] ?: @"";
        dispatch_async(dispatch_get_main_queue(), ^{
            OperationsViewController *controller = weakSelf;
            if (controller != nil && controller->_localRecognitionService == serviceTask){
                controller->_localRecognitionError = details;
            }
        });
    });
    return socket;
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
    // 第一个模型（检测器）裁出的车牌图：无论识别是否成功都先显示出来。
    NSString *cropImage = result[@"crop_image"];
    if ([cropImage isKindOfClass:[NSString class]] && cropImage.length > 0){
        NSImage *image = [[NSImage alloc] initWithContentsOfFile:cropImage];
        if (image != nil){
            _imageView.image = image;
        }
    }
    NSString *plate = result[@"plate"];
    if (![plate isKindOfClass:[NSString class]] || plate.length == 0){
        _statusLabel.stringValue = @"未识别到车牌。";
        return;
    }
    NSNumber *det = result[@"detection_confidence"];
    NSNumber *rec = result[@"recognition_confidence"];
    double detV = [det isKindOfClass:[NSNumber class]] ? det.doubleValue : 0.0;
    double recV = [rec isKindOfClass:[NSNumber class]] ? rec.doubleValue : 0.0;
    // 自定义脚本可能不给 valid：缺省当作有效，避免把「字段缺失」误判成低置信度。
    NSNumber *validFlag = result[@"valid"];
    const BOOL valid = [validFlag isKindOfClass:[NSNumber class]] ? validFlag.boolValue : YES;

    // 识别即办理：按点下的按钮（入场/出场）直接完成，不再弹「是否采用该车牌」；
    // 但识别置信度不够高时先弹一次确认（阈值见 recognitionConfirmationThreshold）。
    if (_recognitionIntent == SmartParkRecognitionIntentEnter
        || _recognitionIntent == SmartParkRecognitionIntentExit){
        const BOOL exiting = _recognitionIntent == SmartParkRecognitionIntentExit;
        NSString *intentText = exiting ? @"出库" : @"入场";
        _statusLabel.stringValue = [NSString stringWithFormat:
            @"识别到 %@（检测 %.1f%% · 识别 %.1f%%），按识别结果办理中…",
            plate, detV * 100.0, recV * 100.0];

        // 判据只取「识别置信度」（车牌读对没读对），不把检测置信度算进来：
        // 样张上检测常常只有 0.8 出头而识别是 0.99，用 min() 会把好读的也拦下来。
        const double threshold = [self recognitionConfirmationThreshold];
        if (!smartpark_lpr::needsConfirmation(recV, valid, threshold)){
            [self performRecognitionIntentWithPlate:plate result:result];
            return;
        }

        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = [NSString stringWithFormat:
            @"识别置信度偏低（低于 %.0f%%）", threshold * 100.0];
        alert.informativeText = [NSString stringWithFormat:
            @"识别到车牌：%@\n检测置信度 %.1f%% · 识别置信度 %.1f%%%@\n"
             "确认后按该车牌办理%@。",
            plate, detV * 100.0, recV * 100.0,
            valid ? @"" : @"（识别结果被标记为不可靠）", intentText];
        [alert addButtonWithTitle:[NSString stringWithFormat:@"按 %@ 办理%@", plate, intentText]];
        [alert addButtonWithTitle:@"取消"];
        [alert beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse response){
            if (response == NSAlertFirstButtonReturn){
                [self performRecognitionIntentWithPlate:plate result:result];
                return;
            }
            // 取消：不归档照片、不办理，车牌也不写进输入框。
            _recognitionIntent = SmartParkRecognitionIntentNone;
            _statusLabel.stringValue = [NSString stringWithFormat:
                @"已取消：未按 %@ 办理%@。", plate, intentText];
        }];
        return;
    }

    // 没有意图（理论上不会走到）：保留原来的候选确认，避免误操作。
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

// 识别置信度确认阈值（0~1）：低于它才弹一次人工确认。默认 0.85；
// SMARTPARK_LPR_CONFIRM_THRESHOLD 可覆盖（填 0.01 即「永远自动办理」）。
- (double)recognitionConfirmationThreshold{
    const char *value = getenv("SMARTPARK_LPR_CONFIRM_THRESHOLD");
    if (value != nullptr && value[0] != '\0'){
        const double parsed = atof(value);
        if (parsed > 0.0 && parsed <= 1.0){
            return parsed;
        }
    }
    return 0.85;
}

@end
