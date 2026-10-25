#pragma once

#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

// macOS 26「液态玻璃」（Liquid Glass）叠加辅助。
//
// 双重门槛：编译期要求 SDK ≥ macOS 26（macos-15 CI 上整段实现被预处理
// 剔除，符号不存在也不参与链接）；运行期要求系统 ≥ macOS 26。任何一项
// 不满足时 SmartParkGlassWrap 返回 nil，调用方走既有的
// NSVisualEffectView 外观——旧系统观感零变化。
//
// 设计取舍：只把「背景容器」（登录卡、注册卡）换成 NSGlassEffectView；
// 按钮保持系统标准样式。用 macOS 26 SDK 编译后，标准控件（含工具栏）在
// macOS 26 上会自动获得新材质，无需逐个改造；也不在玻璃卡内再嵌套玻璃
// 按钮（嵌套时 z 序/折射行为不受保证，Apple 只承诺 contentView 一层）。
BOOL SmartParkGlassEnabled(void);

// 把 content 包进液态玻璃卡片（cornerRadius 为四角曲率半径）。
// 系统或 SDK 不满足时返回 nil，content 不被改动、不加约束。
NSView * _Nullable SmartParkGlassWrap(NSView *content, CGFloat cornerRadius);

NS_ASSUME_NONNULL_END
