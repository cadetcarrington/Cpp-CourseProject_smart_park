#import "MainMenu.h"

#import <Cocoa/Cocoa.h>

namespace {

// target 一律留空（nil）：AppKit 会沿响应者链找到真正处理这个 action 的对象，
// 例如 copy: 落到文本框的 field editor（NSTextView）上。
NSMenuItem *makeItem(NSString *title, SEL action, NSString *key, NSEventModifierFlags modifiers){
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title
                                                  action:action
                                           keyEquivalent:(key != nil ? key : @"")];
    if (modifiers != 0){
        item.keyEquivalentModifierMask = modifiers;
    }
    return item;
}

// 带 tag 的菜单项：页面切换用它记录目标页索引。
NSMenuItem *makeTaggedItem(NSString *title, SEL action, NSString *key,
                           NSEventModifierFlags modifiers, NSInteger tag){
    NSMenuItem *item = makeItem(title, action, key, modifiers);
    item.tag = tag;
    return item;
}

NSMenu *makeSubmenu(NSString *title){
    NSMenu *menu = [[NSMenu alloc] initWithTitle:title];
    // 交给 AppKit 按响应者链校验：没选中文字时「拷贝」自动变灰，不可粘贴时
    // 「粘贴」自动变灰，和系统 App 表现一致。
    menu.autoenablesItems = YES;
    return menu;
}

// 顶层菜单项：菜单栏上显示的是 submenu 的标题（应用菜单会被 App 名称替换）。
NSMenuItem *addTopLevel(NSMenu *mainMenu, NSString *title, NSMenu *submenu){
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
    item.submenu = submenu;
    [mainMenu addItem:item];
    return item;
}

// 页面切换的快捷键与 MainWindowController 的页面顺序一致（0 仪表盘 … 6 设施配置）。
NSArray<NSString *> *pageTitles(void){
    return @[ @"仪表盘", @"车位图", @"车辆出入库", @"当前车位",
              @"预约管理", @"停车记录", @"设施配置" ];
}

}  // namespace

void SmartParkInstallMainMenu(void){
    NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@"主菜单"];
    mainMenu.autoenablesItems = YES;

    // ① 应用菜单：标题显示时会被系统替换成 App 名称。
    NSMenu *appMenu = makeSubmenu(@"SmartPark");
    [appMenu addItem:makeItem(@"关于 SmartPark 管理端", @selector(orderFrontStandardAboutPanel:), @"", 0)];

    // 服务子菜单（系统提供的「用其他 App 打开选中内容」等），空的时候 AppKit 会自动隐藏。
    NSMenu *servicesMenu = makeSubmenu(@"服务");
    NSMenuItem *servicesItem = [[NSMenuItem alloc] initWithTitle:@"服务" action:nil keyEquivalent:@""];
    servicesItem.submenu = servicesMenu;
    [appMenu addItem:servicesItem];
    NSApp.servicesMenu = servicesMenu;

    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItem:makeItem(@"隐藏 SmartPark", @selector(hide:), @"h", NSEventModifierFlagCommand)];
    [appMenu addItem:makeItem(@"隐藏其他", @selector(hideOtherApplications:), @"h",
                              NSEventModifierFlagCommand | NSEventModifierFlagOption)];
    [appMenu addItem:makeItem(@"显示全部", @selector(unhideAllApplications:), @"", 0)];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItem:makeItem(@"退出登录", @selector(smartparkLogout:), @"w",
                              NSEventModifierFlagCommand | NSEventModifierFlagShift)];
    [appMenu addItem:makeItem(@"退出 SmartPark", @selector(terminate:), @"q", NSEventModifierFlagCommand)];
    addTopLevel(mainMenu, @"SmartPark", appMenu);

    // ② 操作菜单：四个高频动作，直接进对应页（出库/寻车会先弹「选择或输入在场车牌」）。
    NSMenu *actionMenu = makeSubmenu(@"操作");
    [actionMenu addItem:makeItem(@"出库", @selector(smartparkCheckOut:), @"o", NSEventModifierFlagCommand)];
    [actionMenu addItem:makeItem(@"入库", @selector(smartparkCheckIn:), @"i", NSEventModifierFlagCommand)];
    [actionMenu addItem:makeItem(@"寻车", @selector(smartparkFindCar:), @"f", NSEventModifierFlagCommand)];
    [actionMenu addItem:makeItem(@"预约", @selector(smartparkNewReservation:), @"n", NSEventModifierFlagCommand)];
    addTopLevel(mainMenu, @"操作", actionMenu);

    // ③ 编辑菜单：⌘C / ⌘V / ⌘X / ⌘A / ⌘Z 全部来自这里。
    NSMenu *editMenu = makeSubmenu(@"编辑");
    [editMenu addItem:makeItem(@"撤销", @selector(undo:), @"z", NSEventModifierFlagCommand)];
    [editMenu addItem:makeItem(@"重做", @selector(redo:), @"z",
                               NSEventModifierFlagCommand | NSEventModifierFlagShift)];
    [editMenu addItem:[NSMenuItem separatorItem]];
    [editMenu addItem:makeItem(@"剪切", @selector(cut:), @"x", NSEventModifierFlagCommand)];
    [editMenu addItem:makeItem(@"拷贝", @selector(copy:), @"c", NSEventModifierFlagCommand)];
    [editMenu addItem:makeItem(@"粘贴", @selector(paste:), @"v", NSEventModifierFlagCommand)];
    [editMenu addItem:makeItem(@"粘贴并匹配样式", @selector(pasteAsPlainText:), @"v",
                               NSEventModifierFlagCommand | NSEventModifierFlagOption | NSEventModifierFlagShift)];
    [editMenu addItem:makeItem(@"删除", @selector(delete:), @"", 0)];
    [editMenu addItem:[NSMenuItem separatorItem]];
    [editMenu addItem:makeItem(@"全选", @selector(selectAll:), @"a", NSEventModifierFlagCommand)];
    addTopLevel(mainMenu, @"编辑", editMenu);

    // ④ 显示菜单：页面切换 / 刷新 / 侧边栏 / 全屏。切页与刷新由 AppDelegate 接收，
    // 再转发给主窗口控制器（菜单在没有主窗口时自动变灰）。
    NSMenu *viewMenu = makeSubmenu(@"显示");

    NSMenu *pageMenu = makeSubmenu(@"页面");
    NSArray<NSString *> *titles = pageTitles();
    for (NSInteger index = 0; index < (NSInteger)titles.count; ++index){
        NSString *key = [NSString stringWithFormat:@"%ld", (long)(index + 1)];
        [pageMenu addItem:makeTaggedItem(titles[(NSUInteger)index], @selector(smartparkSelectPage:),
                                         key, NSEventModifierFlagCommand, index)];
    }
    NSMenuItem *pageItem = [[NSMenuItem alloc] initWithTitle:@"页面" action:nil keyEquivalent:@""];
    pageItem.submenu = pageMenu;
    [viewMenu addItem:pageItem];

    [viewMenu addItem:[NSMenuItem separatorItem]];
    [viewMenu addItem:makeItem(@"刷新", @selector(smartparkRefreshPage:), @"r", NSEventModifierFlagCommand)];
    // toggleSidebar: 由 NSSplitViewController 实现，靠 sidebarItem.canCollapse 生效。
    [viewMenu addItem:makeItem(@"显示/隐藏侧边栏", @selector(toggleSidebar:), @"s",
                               NSEventModifierFlagCommand | NSEventModifierFlagControl)];
    [viewMenu addItem:[NSMenuItem separatorItem]];
    [viewMenu addItem:makeItem(@"进入全屏", @selector(toggleFullScreen:), @"f",
                               NSEventModifierFlagCommand | NSEventModifierFlagControl)];
    addTopLevel(mainMenu, @"显示", viewMenu);

    // ⑤ 窗口菜单：最小化 / 缩放 / 全部前置（窗口列表由 AppKit 自动追加）。
    NSMenu *windowMenu = makeSubmenu(@"窗口");
    [windowMenu addItem:makeItem(@"最小化", @selector(performMiniaturize:), @"m", NSEventModifierFlagCommand)];
    [windowMenu addItem:makeItem(@"缩放", @selector(performZoom:), @"", 0)];
    [windowMenu addItem:[NSMenuItem separatorItem]];
    [windowMenu addItem:makeItem(@"全部前置", @selector(arrangeInFront:), @"", 0)];
    [windowMenu addItem:[NSMenuItem separatorItem]];
    // 没有「文件」菜单：这个 App 只有一个主窗口，关闭窗口即退出。
    [windowMenu addItem:makeItem(@"关闭窗口", @selector(performClose:), @"w", NSEventModifierFlagCommand)];
    addTopLevel(mainMenu, @"窗口", windowMenu);

    // ⑥ 帮助菜单：放一张快捷键总表，答辩演示时比翻文档快。
    NSMenu *helpMenu = makeSubmenu(@"帮助");
    [helpMenu addItem:makeItem(@"SmartPark 快捷键", @selector(smartparkShowShortcuts:), @"", 0)];
    addTopLevel(mainMenu, @"帮助", helpMenu);

    NSApp.mainMenu = mainMenu;
    // 让「窗口」菜单自动列出所有窗口、「帮助」菜单带上搜索框，和系统 App 一致。
    NSApp.windowsMenu = windowMenu;
    NSApp.helpMenu = helpMenu;
}
