#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include "NativeEffects.h"
#include "platform/MacNotifications.h"
#include <QWidget>
#include <QAbstractScrollArea>
#include <QComboBox>
#include <QEvent>

#include <string>
#include <vector>

// 原生 NSPopUpButton 的 action target：把用户选择回写到 QComboBox。
@interface SmartParkPopupTarget : NSObject
@property (nonatomic, assign) QComboBox *combo;
- (void)popupChanged:(NSPopUpButton *)sender;
@end

@implementation SmartParkPopupTarget
- (void)popupChanged:(NSPopUpButton *)sender{
    if (self.combo == nullptr){
        return;
    }
    const int idx = (int)sender.indexOfSelectedItem;
    if (idx >= 0 && idx < self.combo->count() && self.combo->currentIndex() != idx){
        self.combo->setCurrentIndex(idx);
    }
}
@end

// 原生停车记录表格的数据源与委托：view-based NSTableView，单元格文本可选中复制。
@interface SmartParkRecordTableSource : NSObject <NSTableViewDataSource, NSTableViewDelegate>
@property (nonatomic, assign) NSTableView *tableView;
- (instancetype)initWithColumns:(NSArray<NSString *> *)columns;
- (void)setRows:(NSArray<NSArray<NSString *> *> *)rows;
@end

@implementation SmartParkRecordTableSource{
    NSMutableArray<NSString *> *_columns;
    NSMutableArray<NSArray<NSString *> *> *_rows;
}
- (instancetype)initWithColumns:(NSArray<NSString *> *)columns{
    if ((self = [super init])){
        _columns = [[NSMutableArray alloc] initWithArray:columns];
        _rows = [[NSMutableArray alloc] init];
    }
    return self;
}
- (void)dealloc{
    [_columns release];
    [_rows release];
    [super dealloc];
}
- (void)setRows:(NSArray<NSArray<NSString *> *> *)rows{
    [_rows setArray:rows];
    [self.tableView reloadData];
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView{
    return (NSInteger)_rows.count;
}
- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row{
    if (row < 0 || row >= (NSInteger)_rows.count){
        return nil;
    }
    NSInteger columnIndex = [tableView.tableColumns indexOfObject:tableColumn];
    if (columnIndex == NSNotFound || columnIndex >= (NSInteger)_columns.count){
        return nil;
    }
    NSTextField *cell = [tableView makeViewWithIdentifier:@"SmartParkRecordCell" owner:self];
    if (cell == nil){
        cell = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, MAX(tableColumn.width, 40), 0)];
        cell.identifier = @"SmartParkRecordCell";
        cell.bordered = NO;
        cell.editable = NO;
        cell.selectable = YES;
        cell.drawsBackground = NO;
        cell.lineBreakMode = NSLineBreakByTruncatingTail;
        cell.textColor = [NSColor labelColor];
        [cell setFont:[NSFont systemFontOfSize:NSFont.smallSystemFontSize]];
        [cell autorelease];
    }
    NSArray<NSString *> *rowData = _rows[(NSUInteger)row];
    cell.stringValue = (NSUInteger)columnIndex < rowData.count ? rowData[(NSUInteger)columnIndex] : @"";
    return cell;
}
@end

// 监听 QComboBox 的 enabled 状态变化，同步到原生控件。
class PopupEnabledSync : public QObject{
public:
    PopupEnabledSync(QComboBox *combo, NSPopUpButton *popup, QObject *parent = nullptr)
        : QObject(parent), combo_(combo), popup_(popup){}
    bool eventFilter(QObject *watched, QEvent *event) override{
        if (watched == combo_){
            if (event->type() == QEvent::EnabledChange){
                [popup_ setEnabled:combo_->isEnabled()];
            } else if (event->type() == QEvent::Hide){
                combo_->setStyleSheet(QString());
            }
        }
        return QObject::eventFilter(watched, event);
    }
private:
    QComboBox *combo_;
    NSPopUpButton *popup_;
};

class BackgroundEffectSync : public QObject{
public:
    BackgroundEffectSync(QWidget *widget, NSView *view, NSVisualEffectView *effect)
        : QObject(widget), view_(view), effect_([effect retain]){}

    ~BackgroundEffectSync() override{
        [effect_ removeFromSuperview];
        [effect_ release];
    }

    bool eventFilter(QObject *watched, QEvent *event) override{
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
            event->type() == QEvent::Show || event->type() == QEvent::Hide){
            effect_.frame = view_.frame;
            effect_.hidden = !static_cast<QWidget *>(watched)->isVisible();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    NSView *view_;
    NSVisualEffectView *effect_;
};

namespace{
static char kPopupButtonKey;
static char kPopupTargetKey;
static char kSidebarEffectKey;
static char kHeaderEffectKey;
static char kContentEffectKey;
static char kRecordTableKey;
}

namespace smartpark_ui{
bool applyNativeVibrancy(QWidget *window, bool darkAppearance){
    if (window == nullptr || getenv("SMARTPARK_NO_VIBRANCY") != nullptr){
        return false;
    }
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    if (view == nil || view.window == nil){
        return false;
    }
    NSWindow *nsWindow = view.window;
    // 让窗口自身透明，Qt 层透明区域才能透出后面的毛玻璃。
    nsWindow.opaque = NO;
    nsWindow.backgroundColor = [NSColor clearColor];

    nsWindow.styleMask |= NSWindowStyleMaskFullSizeContentView;
    // nsWindow.styleMask &= ~NSWindowStyleMaskTitled;

    nsWindow.titlebarAppearsTransparent = YES;
    nsWindow.titleVisibility = NSWindowTitleHidden;

    NSView *hostView = view.superview;

    if (hostView == nil) {
        return false;
    }

    NSVisualEffectView * blur = [[NSVisualEffectView alloc] initWithFrame:view.bounds];

    blur.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    blur.blendingMode = NSVisualEffectBlendingModeBehindWindow;

    blur.material = NSVisualEffectMaterialHUDWindow;
    //blur.material = NSVisualEffectMaterialSidebar;
    //blur.material = NSVisualEffectMaterialUnderWindowBackground;

    blur.state = NSVisualEffectStateActive;

    //blur.alphaValue = 0.85;

    [hostView addSubview:blur  positioned:NSWindowBelow  relativeTo:view];
    // NSVisualEffectView *vibrancy = [[NSVisualEffectView alloc]
    //     initWithFrame:view.bounds];
    // vibrancy.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    // vibrancy.material = darkAppearance
    //     ? NSVisualEffectMaterialHUDWindow
    //     : NSVisualEffectMaterialUnderWindowBackground;
    // vibrancy.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    // vibrancy.state = NSVisualEffectStateActive;
    // [view addSubview:vibrancy positioned:NSWindowBelow relativeTo:nil];
    // [vibrancy release];
    return true;
}

bool applyNativeSidebarVibrancy(QWidget *sidebar){
    if (sidebar == nullptr || getenv("SMARTPARK_NO_VIBRANCY") != nullptr){
        return false;
    }
    sidebar->setAttribute(Qt::WA_NativeWindow, true);
    NSView *view = reinterpret_cast<NSView *>(sidebar->winId());
    NSView *hostView = view.superview;
    if (view == nil || view.window == nil || hostView == nil){
        return false;
    }
    if (objc_getAssociatedObject(view, &kSidebarEffectKey) != nil){
        return true;
    }

    NSVisualEffectView *effect = [[NSVisualEffectView alloc] initWithFrame:view.frame];
    effect.autoresizingMask = NSViewNotSizable;
    effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    effect.material = NSVisualEffectMaterialSidebar;
    effect.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    effect.state = NSVisualEffectStateActive;
    [hostView addSubview:effect positioned:NSWindowBelow relativeTo:view];
    auto *sync = new BackgroundEffectSync(sidebar, view, effect);
    sidebar->installEventFilter(sync);
    objc_setAssociatedObject(view, &kSidebarEffectKey, effect,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
#if !__has_feature(objc_arc)
    [effect release];
#endif
    return true;
}

bool applyNativeHeaderVibrancy(QWidget *header){
    if (header == nullptr || getenv("SMARTPARK_NO_VIBRANCY") != nullptr){
        return false;
    }
    header->setAttribute(Qt::WA_NativeWindow, true);
    NSView *view = reinterpret_cast<NSView *>(header->winId());
    if (view == nil || view.window == nil || view.superview == nil){
        return false;
    }
    if (objc_getAssociatedObject(view, &kHeaderEffectKey) != nil){
        return true;
    }

    NSVisualEffectView *effect = [[NSVisualEffectView alloc] initWithFrame:view.frame];
    effect.autoresizingMask = NSViewNotSizable;
    effect.wantsLayer = YES;
    effect.layer.cornerRadius = 12.0;
    effect.layer.masksToBounds = YES;
    effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    effect.material = NSVisualEffectMaterialUnderWindowBackground;
    effect.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    effect.state = NSVisualEffectStateActive;
    effect.hidden = !header->isVisible();
    [view.superview addSubview:effect positioned:NSWindowBelow relativeTo:view];
    auto *sync = new BackgroundEffectSync(header, view, effect);
    header->installEventFilter(sync);
    objc_setAssociatedObject(view, &kHeaderEffectKey, effect,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
#if !__has_feature(objc_arc)
    [effect release];
#endif
    return true;
}

bool applyNativeContentVibrancy(QWidget *content){
    if (content == nullptr || getenv("SMARTPARK_NO_VIBRANCY") != nullptr){
        return false;
    }
    content->setAttribute(Qt::WA_NativeWindow, true);
    NSView *view = reinterpret_cast<NSView *>(content->winId());
    if (view == nil || view.window == nil || view.superview == nil){
        return false;
    }
    for (QWidget *ancestor = content->parentWidget(); ancestor != nullptr;
         ancestor = ancestor->parentWidget()){
        auto *scroll = qobject_cast<QAbstractScrollArea *>(ancestor);
        if (scroll == nullptr){
            continue;
        }
        NSView *viewport = reinterpret_cast<NSView *>(scroll->viewport()->winId());
        if (viewport == nil || ![view isDescendantOf:viewport]){
            return false;
        }
        viewport.wantsLayer = YES;
        viewport.layer.masksToBounds = YES;
    }
    if (objc_getAssociatedObject(view, &kContentEffectKey) != nil){
        return true;
    }

    NSVisualEffectView *effect = [[NSVisualEffectView alloc] initWithFrame:view.frame];
    effect.autoresizingMask = NSViewNotSizable;
    effect.wantsLayer = YES;
    effect.layer.cornerRadius = 12.0;
    effect.layer.masksToBounds = YES;
    effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    effect.material = NSVisualEffectMaterialUnderWindowBackground;
    effect.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    effect.state = NSVisualEffectStateActive;
    effect.hidden = !content->isVisible();
    [view.superview addSubview:effect positioned:NSWindowBelow relativeTo:view];
    auto *sync = new BackgroundEffectSync(content, view, effect);
    content->installEventFilter(sync);
    objc_setAssociatedObject(view, &kContentEffectKey, effect,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
#if !__has_feature(objc_arc)
    [effect release];
#endif
    return true;
}

bool applyNativePopupButton(QComboBox *combo){
    if (combo == nullptr || getenv("SMARTPARK_NO_NATIVE_POPUP") != nullptr){
        return false;
    }
    combo->setAttribute(Qt::WA_NativeWindow, true);
    NSView *qtView = reinterpret_cast<NSView *>(combo->winId());
    if (qtView == nil || qtView.window == nil){
        return false;
    }

    NSPopUpButton *popup = (NSPopUpButton *)objc_getAssociatedObject(qtView, &kPopupButtonKey);
    if (popup == nil){
        popup = [[NSPopUpButton alloc] initWithFrame:qtView.bounds pullsDown:NO];
        popup.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        popup.wantsLayer = YES;
        popup.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];

        SmartParkPopupTarget *target = [[SmartParkPopupTarget alloc] init];
        target.combo = combo;
        [popup setTarget:target];
        [popup setAction:@selector(popupChanged:)];

        [qtView addSubview:popup];

        auto *sync = new PopupEnabledSync(combo, popup, combo);
        combo->installEventFilter(sync);

        objc_setAssociatedObject(qtView, &kPopupButtonKey, popup, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(qtView, &kPopupTargetKey, target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        // 程序性 setCurrentIndex 时同步原生控件选中项（值相同则跳过，避免循环）。
        QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                         [popup](int idx){
                             if ((int)popup.indexOfSelectedItem != idx){
                                 [popup selectItemAtIndex:idx];
                             }
                         });

#if !__has_feature(objc_arc)
        [popup release];
        [target release];
#endif
    }

    // 同步 items 与当前选中项，并隐藏 Qt 自绘内容。
    [popup removeAllItems];
    for (int i = 0; i < combo->count(); ++i){
        [popup addItemWithTitle:combo->itemText(i).toNSString()];
    }
    const int current = combo->currentIndex();
    if (current >= 0 && current < combo->count()){
        [popup selectItemAtIndex:current];
    }
    [popup setEnabled:combo->isEnabled()];
    [popup setFrame:qtView.bounds];
    [popup setHidden:NO];
    [qtView addSubview:popup positioned:NSWindowAbove relativeTo:nil];
    combo->setStyleSheet(QStringLiteral(
        "QComboBox { background: transparent; border: none; color: transparent; }"
        "QComboBox::drop-down { border: none; width: 0; }"
        "QComboBox::down-arrow { image: none; width: 0; height: 0; }"));

    return true;
}

void *applyNativeRecordTable(QWidget *host, const QStringList &columns){
    if (host == nullptr || getenv("SMARTPARK_NO_NATIVE_TABLE") != nullptr){
        return nullptr;
    }
    host->setAttribute(Qt::WA_NativeWindow, true);
    NSView *qtView = reinterpret_cast<NSView *>(host->winId());
    if (qtView == nil || qtView.window == nil){
        return nullptr;
    }

    SmartParkRecordTableSource *source =
        (SmartParkRecordTableSource *)objc_getAssociatedObject(qtView, &kRecordTableKey);
    if (source != nil){
        return source;
    }

    // 浅色毛玻璃背景：表格区域透出系统浅色材质，带圆角。
    NSVisualEffectView *glass = [[NSVisualEffectView alloc] initWithFrame:qtView.bounds];
    glass.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    glass.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    glass.material = NSVisualEffectMaterialUnderWindowBackground;
    glass.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    glass.state = NSVisualEffectStateActive;
    glass.wantsLayer = YES;
    glass.layer.cornerRadius = 15.0;
    glass.layer.masksToBounds = YES;
    [qtView addSubview:glass positioned:NSWindowBelow relativeTo:nil];
    [glass release];

    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:qtView.bounds];
    scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    scroll.borderType = NSNoBorder;
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = YES;
    scroll.drawsBackground = NO;
    scroll.backgroundColor = [NSColor clearColor];
    scroll.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    scroll.wantsLayer = YES;
    scroll.layer.cornerRadius = 15.0;
    scroll.layer.masksToBounds = YES;

    NSTableView *table = [[NSTableView alloc] initWithFrame:scroll.contentView.bounds];
    table.usesAlternatingRowBackgroundColors = NO;
    table.backgroundColor = [NSColor clearColor];
    table.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    table.rowHeight = 24.0;
    table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
    table.allowsMultipleSelection = NO;
    table.allowsEmptySelection = YES;

    NSTableHeaderView *header = [[NSTableHeaderView alloc] init];
    table.headerView = header;
    [header release];

    NSMutableArray<NSString *> *titles = [NSMutableArray arrayWithCapacity:(NSUInteger)columns.size()];
    for (int i = 0; i < columns.size(); ++i){
        NSString *title = columns.at(i).toNSString();
        [titles addObject:title];
        NSTableColumn *col = [[NSTableColumn alloc]
            initWithIdentifier:[NSString stringWithFormat:@"col_%d", i]];
        col.title = title;
        col.width = 110.0;
        col.minWidth = 56.0;
        [table addTableColumn:col];
        [col release];
    }

    source = [[SmartParkRecordTableSource alloc] initWithColumns:titles];
    source.tableView = table;
    table.dataSource = source;
    table.delegate = source;

    scroll.documentView = table;
    [qtView addSubview:scroll];

    objc_setAssociatedObject(qtView, &kRecordTableKey, source, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    [table release];
    [scroll release];
    [source release];

    return source;
}

void setNativeRecordTableRows(void *handle, const QVector<QStringList> &rows){
    if (handle == nullptr){
        return;
    }
    SmartParkRecordTableSource *source = (SmartParkRecordTableSource *)handle;
    NSMutableArray<NSArray<NSString *> *> *objRows =
        [NSMutableArray arrayWithCapacity:(NSUInteger)rows.size()];
    for (const QStringList &row : rows){
        NSMutableArray<NSString *> *objRow =
            [NSMutableArray arrayWithCapacity:(NSUInteger)row.size()];
        for (const QString &cell : row){
            [objRow addObject:cell.toNSString()];
        }
        [objRows addObject:objRow];
    }
    [source setRows:objRows];
}

void showExitNotification(QWidget *window, const QString &title,
                          const QStringList &lines){
    NSWindow *parent = nil;
    if (window != nullptr){
        if (NSView *view = reinterpret_cast<NSView *>(window->winId())){
            parent = view.window;
        }
    }
    std::vector<std::string> utf8Lines;
    utf8Lines.reserve(static_cast<size_t>(lines.size()));
    for (const QString &line : lines){
        utf8Lines.emplace_back(line.toUtf8().constData());
    }
    const std::string utf8Title = title.toUtf8().constData();
    macnotify::showExitBanner((const void *)parent, utf8Title.c_str(), utf8Lines);
}
} // namespace smartpark_ui
