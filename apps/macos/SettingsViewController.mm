#import "SettingsViewController.h"

#import "TextUtil.h"
#import "bridge/ParkingBridge.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "core/model/ParkingLayout.h"

#include <cstddef>
#include <string>

// 设施配置：停车场布局编辑器（原生 AppKit）+ 计费规则展示。
//
// 布局编辑器走完整闭环：编辑 → 实时语法校验 → 原生确认 sheet → 应用 →
// 数据不兼容时询问是否重置数据库。文本用 NSTextView（撤销/重做由
// NSUndoManager 提供），文件读写用 NSOpenPanel / NSSavePanel，
// 提示与确认一律用 NSAlert sheet。
@interface SettingsViewController () <NSTextViewDelegate>
@end

@implementation SettingsViewController{
    NSTextView *_layoutView;
    NSTextField *_layoutStatus;
    NSButton *_applyButton;
    NSTextField *_billingLabel;
    // 仅在首次进入时从 bridge 载入文本，避免切页刷新冲掉未应用的编辑。
    BOOL _layoutLoaded;
}

#pragma mark - 视图构建

- (void)loadView{
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];

    NSTextField *title = [NSTextField labelWithString:@"设施配置"];
    title.font = [NSFont systemFontOfSize:26 weight:NSFontWeightSemibold];
    title.textColor = [NSColor labelColor];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:title];

    // ---- 停车场布局卡片 ----
    NSBox *layoutCard = [[NSBox alloc] init];
    layoutCard.boxType = NSBoxCustom;
    layoutCard.cornerRadius = 12.0;
    layoutCard.fillColor = [NSColor colorWithSRGBRed:1 green:1 blue:1 alpha:0.05];
    layoutCard.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:layoutCard];

    NSTextField *layoutTitle = [NSTextField labelWithString:@"停车场布局"];
    layoutTitle.font = [NSFont systemFontOfSize:14 weight:NSFontWeightSemibold];
    layoutTitle.textColor = [NSColor labelColor];
    layoutTitle.translatesAutoresizingMaskIntoConstraints = NO;
    [layoutCard addSubview:layoutTitle];

    NSTextField *layoutHint = [NSTextField wrappingLabelWithString:
        @"指令：site 宽 高｜entrance x y｜exit x y｜"
        @"region 名称 x y 行 列 车位宽 车位长 通道宽 left|right|up|down [类型]｜"
        @"obstacle x y 宽 高 [名称]。应用后按新布局重建车位数据。"];
    layoutHint.textColor = [NSColor secondaryLabelColor];
    layoutHint.font = [NSFont systemFontOfSize:12];
    layoutHint.translatesAutoresizingMaskIntoConstraints = NO;
    [layoutCard addSubview:layoutHint];

    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = NO;
    scroll.borderType = NSBezelBorder;
    scroll.translatesAutoresizingMaskIntoConstraints = NO;

    _layoutView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 720, 200)];
    _layoutView.delegate = self;
    _layoutView.editable = YES;
    _layoutView.selectable = YES;
    _layoutView.richText = NO;
    _layoutView.allowsUndo = YES;   // 原生撤销/重做：⌘Z / ⇧⌘Z
    _layoutView.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    _layoutView.textColor = [NSColor labelColor];
    _layoutView.backgroundColor = [NSColor textBackgroundColor];
    _layoutView.textContainerInset = NSMakeSize(8.0, 8.0);
    _layoutView.verticallyResizable = YES;
    _layoutView.horizontallyResizable = NO;
    _layoutView.autoresizingMask = NSViewWidthSizable;
    _layoutView.minSize = NSMakeSize(0.0, 0.0);
    _layoutView.maxSize = NSMakeSize(FLT_MAX, FLT_MAX);
    _layoutView.textContainer.widthTracksTextView = YES;
    _layoutView.textContainer.containerSize = NSMakeSize(720.0, FLT_MAX);
    // 布局是指令式精确语法：关闭智能替换，避免引号/破折号/拼写纠正改写内容。
    _layoutView.automaticQuoteSubstitutionEnabled = NO;
    _layoutView.automaticDashSubstitutionEnabled = NO;
    _layoutView.automaticTextReplacementEnabled = NO;
    _layoutView.automaticSpellingCorrectionEnabled = NO;
    _layoutView.continuousSpellCheckingEnabled = NO;
    _layoutView.grammarCheckingEnabled = NO;
    _layoutView.smartInsertDeleteEnabled = NO;
    scroll.documentView = _layoutView;
    [layoutCard addSubview:scroll];

    _applyButton = [NSButton buttonWithTitle:@"应用布局"
                                      target:self
                                      action:@selector(applyLayout:)];
    _applyButton.bezelStyle = NSBezelStyleRounded;
    // ⌘↩ 而非回车：回车必须留给文本编辑器换行。
    _applyButton.keyEquivalent = @"\r";
    _applyButton.keyEquivalentModifierMask = NSEventModifierFlagCommand;

    NSButton *builtinButton = [NSButton buttonWithTitle:@"载入内置布局"
                                                 target:self
                                                 action:@selector(loadBuiltinLayout:)];
    NSButton *revertButton = [NSButton buttonWithTitle:@"还原修改"
                                                target:self
                                                action:@selector(revertLayout:)];
    NSButton *importButton = [NSButton buttonWithTitle:@"导入…"
                                                target:self
                                                action:@selector(importLayout:)];
    NSButton *exportButton = [NSButton buttonWithTitle:@"导出…"
                                                target:self
                                                action:@selector(exportLayout:)];

    NSStackView *buttons = [NSStackView stackViewWithViews:@[
        _applyButton, builtinButton, revertButton, importButton, exportButton
    ]];
    buttons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    buttons.spacing = 8.0;
    buttons.translatesAutoresizingMaskIntoConstraints = NO;
    [layoutCard addSubview:buttons];

    _layoutStatus = [NSTextField wrappingLabelWithString:@""];
    _layoutStatus.font = [NSFont systemFontOfSize:12];
    _layoutStatus.textColor = [NSColor secondaryLabelColor];
    _layoutStatus.translatesAutoresizingMaskIntoConstraints = NO;
    [layoutCard addSubview:_layoutStatus];

    [NSLayoutConstraint activateConstraints:@[
        [layoutTitle.topAnchor constraintEqualToAnchor:layoutCard.topAnchor constant:14],
        [layoutTitle.leadingAnchor constraintEqualToAnchor:layoutCard.leadingAnchor constant:14],
        [layoutHint.topAnchor constraintEqualToAnchor:layoutTitle.bottomAnchor constant:6],
        [layoutHint.leadingAnchor constraintEqualToAnchor:layoutCard.leadingAnchor constant:14],
        [layoutHint.trailingAnchor constraintEqualToAnchor:layoutCard.trailingAnchor constant:-14],
        [scroll.topAnchor constraintEqualToAnchor:layoutHint.bottomAnchor constant:10],
        [scroll.leadingAnchor constraintEqualToAnchor:layoutCard.leadingAnchor constant:14],
        [scroll.trailingAnchor constraintEqualToAnchor:layoutCard.trailingAnchor constant:-14],
        [scroll.heightAnchor constraintEqualToConstant:200],
        [buttons.topAnchor constraintEqualToAnchor:scroll.bottomAnchor constant:10],
        [buttons.leadingAnchor constraintEqualToAnchor:layoutCard.leadingAnchor constant:14],
        [buttons.trailingAnchor constraintLessThanOrEqualToAnchor:layoutCard.trailingAnchor
                                                       constant:-14],
        [_layoutStatus.topAnchor constraintEqualToAnchor:buttons.bottomAnchor constant:8],
        [_layoutStatus.leadingAnchor constraintEqualToAnchor:layoutCard.leadingAnchor constant:14],
        [_layoutStatus.trailingAnchor constraintEqualToAnchor:layoutCard.trailingAnchor
                                                     constant:-14],
        [_layoutStatus.bottomAnchor constraintEqualToAnchor:layoutCard.bottomAnchor constant:-14],
    ]];

    // ---- 计费卡片 ----
    NSBox *billingCard = [[NSBox alloc] init];
    billingCard.boxType = NSBoxCustom;
    billingCard.cornerRadius = 12.0;
    billingCard.fillColor = [NSColor colorWithSRGBRed:1 green:1 blue:1 alpha:0.05];
    billingCard.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:billingCard];

    NSTextField *billingTitle = [NSTextField labelWithString:@"计费规则"];
    billingTitle.font = [NSFont systemFontOfSize:14 weight:NSFontWeightSemibold];
    billingTitle.textColor = [NSColor labelColor];
    billingTitle.translatesAutoresizingMaskIntoConstraints = NO;
    [billingCard addSubview:billingTitle];

    _billingLabel = [NSTextField wrappingLabelWithString:@""];
    _billingLabel.textColor = [NSColor secondaryLabelColor];
    _billingLabel.font = [NSFont systemFontOfSize:12];
    _billingLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [billingCard addSubview:_billingLabel];

    [NSLayoutConstraint activateConstraints:@[
        [billingTitle.topAnchor constraintEqualToAnchor:billingCard.topAnchor constant:14],
        [billingTitle.leadingAnchor constraintEqualToAnchor:billingCard.leadingAnchor constant:14],
        [_billingLabel.topAnchor constraintEqualToAnchor:billingTitle.bottomAnchor constant:8],
        [_billingLabel.leadingAnchor constraintEqualToAnchor:billingCard.leadingAnchor constant:14],
        [_billingLabel.trailingAnchor constraintEqualToAnchor:billingCard.trailingAnchor
                                                      constant:-14],
        [_billingLabel.bottomAnchor constraintEqualToAnchor:billingCard.bottomAnchor constant:-14],
    ]];

    [NSLayoutConstraint activateConstraints:@[
        [title.topAnchor constraintEqualToAnchor:root.topAnchor constant:24],
        [title.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [layoutCard.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:14],
        [layoutCard.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [layoutCard.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [billingCard.topAnchor constraintEqualToAnchor:layoutCard.bottomAnchor constant:14],
        [billingCard.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
        [billingCard.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
        [billingCard.bottomAnchor constraintLessThanOrEqualToAnchor:root.bottomAnchor
                                                          constant:-24],
    ]];

    self.view = root;
}

- (void)viewDidLoad{
    [super viewDidLoad];
    [self refresh];
}

#pragma mark - 刷新

- (void)refresh{
    // 只载入一次：切页回来时保留用户尚未应用的编辑内容。
    if (!_layoutLoaded && self.bridge != nullptr){
        _layoutView.string = [NSString stringWithUTF8String:
            self.bridge->layoutDescription().c_str()];
        _layoutLoaded = YES;
        [self validatePendingLayout];
    }

    if (self.bridge != nullptr && self.bridge->ready()){
        auto rule = self.bridge->billingRule();
        _billingLabel.stringValue = [NSString stringWithFormat:
            @"免费 %lld 分钟，之后每 %lld 分钟计费一次；首单元 %.2f 元，后续每单元 %.2f 元，单次封顶 %.2f 元。",
            (long long)rule.freeDuration.count(), (long long)rule.billingUnit.count(),
            rule.minimumFee, rule.unitFee, rule.dailyCap];
    } else{
        _billingLabel.stringValue = @"数据库未就绪，计费规则暂不可用。";
    }
}

#pragma mark - 语法校验

- (void)textDidChange:(NSNotification *)notification{
    [self validatePendingLayout];
}

// 即时解析当前文本：语法有效则显示将生成的车位数，并启用「应用布局」。
- (void)validatePendingLayout{
    if (self.bridge == nullptr){
        _applyButton.enabled = NO;
        _layoutStatus.stringValue = @"数据桥未就绪，无法应用布局。";
        _layoutStatus.textColor = [NSColor systemRedColor];
        return;
    }

    NSString *text = _layoutView.string;
    if (text.length == 0){
        _applyButton.enabled = NO;
        _layoutStatus.stringValue = @"布局为空：至少需要 site、entrance、exit 与一个 region。";
        _layoutStatus.textColor = [NSColor systemRedColor];
        return;
    }

    try{
        const auto layout = smartpark::ParkingLayout::fromDescription(
            std::string(text.UTF8String));
        _applyButton.enabled = YES;
        _layoutStatus.stringValue = [NSString stringWithFormat:
            @"语法有效 · 将生成 %zu 个车位（尚未应用）", layout.spots().size()];
        _layoutStatus.textColor = [NSColor secondaryLabelColor];
    } catch (const std::exception &failure){
        _applyButton.enabled = NO;
        _layoutStatus.stringValue = [NSString stringWithFormat:
            @"语法错误：%@", smartpark_ui::toNSString(failure.what())];
        _layoutStatus.textColor = [NSColor systemRedColor];
    }
}

#pragma mark - 应用布局

- (void)applyLayout:(id)sender{
    if (self.bridge == nullptr){
        return;
    }
    NSString *text = _layoutView.string;

    NSAlert *confirm = [[NSAlert alloc] init];
    confirm.messageText = @"应用自定义停车场布局？";
    confirm.informativeText =
        @"应用后按新布局重建车位数据。车库中保存的布局快照与新布局不一致，"
        @"若车库中保存的布局快照与新布局不一致，需要重置数据库（清空历史停车"
        @"记录与预约）——继续后会再次确认。建议先用「导出…」备份布局。";
    [confirm addButtonWithTitle:@"应用"];
    [confirm addButtonWithTitle:@"取消"];
    [self presentAlert:confirm completion:^(NSModalResponse response){
        if (response != NSAlertFirstButtonReturn){
            return;
        }
        [self applyLayoutText:text];
    }];
}

- (void)applyLayoutText:(NSString *)text{
    std::string error;
    bool needsDatabaseReset = false;
    const bool applied = self.bridge->applyLayoutDescription(
        std::string(text.UTF8String), &error, &needsDatabaseReset);

    if (applied){
        [self reportAppliedLayout];
        return;
    }

    [self validatePendingLayout];
    if (!needsDatabaseReset){
        [self presentMessage:@"布局未应用"
                      detail:smartpark_ui::toNSString(error)
                       style:NSAlertStyleWarning];
        return;
    }

    NSAlert *reset = [[NSAlert alloc] init];
    // rebuildService 对任何异常都返回失败，原因不一定是布局快照冲突
    // （也可能是读取/恢复失败）。因此标题保持中性，只如实转述核心的原因。
    reset.messageText = @"无法在当前数据库上应用新布局";
    reset.informativeText = [NSString stringWithFormat:
        @"失败原因：\n%@\n\n"
        @"继续会重置数据库：清空历史停车记录与预约，然后应用当前布局。是否继续？",
        smartpark_ui::toNSString(error)];
    [reset addButtonWithTitle:@"重置并应用"];
    [reset addButtonWithTitle:@"取消"];
    reset.alertStyle = NSAlertStyleCritical;
    [self presentAlert:reset completion:^(NSModalResponse response){
        if (response != NSAlertFirstButtonReturn){
            return;
        }
        [self resetDatabaseAndApplyLayoutText:text];
    }];
}

- (void)resetDatabaseAndApplyLayoutText:(NSString *)text{
    std::string error;
    if (self.bridge->resetDatabaseAndApplyLayout(std::string(text.UTF8String), &error)){
        [self reportAppliedLayout];
        [self presentMessage:@"已重置数据库并应用新布局"
                      detail:@"历史停车记录已清空，新布局已生效。"
                       style:NSAlertStyleInformational];
        return;
    }

    // 布局已在内存模式生效，但未能落库：如实告知，不谎报成功。
    _layoutStatus.stringValue = smartpark_ui::toNSString(error);
    _layoutStatus.textColor = [NSColor systemOrangeColor];
    [self broadcastDataChanged];
}

- (void)reportAppliedLayout{
    const std::size_t count =
        (self.bridge != nullptr) ? self.bridge->spots().size() : 0;
    // 内存模式下布局确实生效了，但没有落库：如实说明，不谎报已保存。
    if (self.bridge != nullptr && self.bridge->memoryOnly()){
        _layoutStatus.stringValue = [NSString stringWithFormat:
            @"已应用自定义布局：%zu 个车位，但数据库不可用，仅保存在内存（重启后不会保留）。",
            count];
        _layoutStatus.textColor = [NSColor systemOrangeColor];
    } else{
        _layoutStatus.stringValue = [NSString stringWithFormat:
            @"已应用自定义布局：%zu 个车位。", count];
        _layoutStatus.textColor = [NSColor systemGreenColor];
    }
    [self broadcastDataChanged];
}

#pragma mark - 其它布局操作

- (void)loadBuiltinLayout:(id)sender{
    _layoutView.string = [NSString stringWithUTF8String:
        smartpark::ParkingLayout::garageDescription()];
    [self validatePendingLayout];
    _layoutStatus.stringValue = @"已载入内置车库布局，点击「应用布局」后生效。";
    _layoutStatus.textColor = [NSColor secondaryLabelColor];
}

- (void)revertLayout:(id)sender{
    if (self.bridge == nullptr){
        return;
    }
    _layoutView.string = [NSString stringWithUTF8String:
        self.bridge->layoutDescription().c_str()];
    [self validatePendingLayout];
    _layoutStatus.stringValue = @"已还原为当前生效的布局。";
    _layoutStatus.textColor = [NSColor secondaryLabelColor];
}

- (void)importLayout:(id)sender{
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.allowsMultipleSelection = NO;
    panel.canChooseDirectories = NO;
    panel.canChooseFiles = YES;
    panel.allowedContentTypes = @[UTTypePlainText, UTTypeUTF8PlainText];

    [panel beginSheetModalForWindow:self.view.window
                 completionHandler:^(NSModalResponse result){
        if (result != NSModalResponseOK || panel.URL == nil){
            return;
        }
        NSError *readError = nil;
        NSString *text = [NSString stringWithContentsOfURL:panel.URL
                                                  encoding:NSUTF8StringEncoding
                                                     error:&readError];
        if (text == nil){
            [self presentMessage:@"无法读取布局文件"
                          detail:readError.localizedDescription
                           style:NSAlertStyleWarning];
            return;
        }
        _layoutView.string = text;
        [self validatePendingLayout];
        _layoutStatus.stringValue = [NSString stringWithFormat:
            @"已导入 %@（尚未应用）", panel.URL.lastPathComponent];
    }];
}

- (void)exportLayout:(id)sender{
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.allowedContentTypes = @[UTTypePlainText];
    panel.nameFieldStringValue = @"smartpark-layout.txt";

    [panel beginSheetModalForWindow:self.view.window
                 completionHandler:^(NSModalResponse result){
        if (result != NSModalResponseOK || panel.URL == nil){
            return;
        }
        NSError *writeError = nil;
        const BOOL written = [_layoutView.string writeToURL:panel.URL
                                                 atomically:YES
                                                   encoding:NSUTF8StringEncoding
                                                      error:&writeError];
        if (!written){
            [self presentMessage:@"导出失败"
                          detail:writeError.localizedDescription
                           style:NSAlertStyleWarning];
            return;
        }
        _layoutStatus.stringValue = [NSString stringWithFormat:
            @"已导出到 %@", panel.URL.path];
        _layoutStatus.textColor = [NSColor secondaryLabelColor];
    }];
}

#pragma mark - 原生提示

- (void)presentAlert:(NSAlert *)alert
          completion:(void (^)(NSModalResponse))handler{
    NSWindow *window = self.view.window;
    if (window != nil){
        [alert beginSheetModalForWindow:window completionHandler:handler];
        return;
    }
    const NSModalResponse response = [alert runModal];
    if (handler != nil){
        handler(response);
    }
}

- (void)presentMessage:(NSString *)title
                detail:(NSString *)detail
                 style:(NSAlertStyle)style{
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = detail;
    alert.alertStyle = style;
    [alert addButtonWithTitle:@"好"];
    [self presentAlert:alert completion:nil];
}

- (void)broadcastDataChanged{
    [[NSNotificationCenter defaultCenter] postNotificationName:@"SmartParkDataChanged"
                                                        object:nil];
}

@end
