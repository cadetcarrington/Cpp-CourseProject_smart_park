#ifndef SMARTPARK_APPS_MACOS_MAINMENU_H
#define SMARTPARK_APPS_MACOS_MAINMENU_H

// 安装标准的 AppKit 主菜单（应用 / 操作 / 编辑 / 显示 / 窗口 / 帮助）。
//
// 为什么必须显式安装：本应用完全用代码搭界面，没有 MainMenu.xib。macOS 的
// ⌘C / ⌘V / ⌘X / ⌘A / ⌘Z 并非 NSTextField 自己监听，而是「编辑」菜单里那些
// keyEquivalent 为 c/v/x/a/z 的菜单项——按下快捷键时 AppKit 把菜单项的 action
// 沿响应者链发给当前第一响应者（文本框的 field editor）。没有主菜单，这些
// 快捷键在任何输入框里都完全不生效，同时 ⌘Q / ⌘W / ⌘M 等系统快捷键也缺失。
//
// 「显示」菜单里的页面切换 / 刷新由 AppDelegate 接收后转给主窗口控制器：
// 菜单项 target 留空，AppKit 沿响应者链送到 App 委托，没有主窗口时自动变灰。
void SmartParkInstallMainMenu(void);

#endif
