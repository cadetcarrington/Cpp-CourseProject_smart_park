#ifndef SMARTPARK_APPS_MACOS_PLATEPROMPT_H
#define SMARTPARK_APPS_MACOS_PLATEPROMPT_H

#import <Cocoa/Cocoa.h>

class ParkingBridge;

// 弹出「选择或输入车牌」对话框，候选来自 ParkingBridge::parkedPlateNumbers()（在场车辆）。
//
// 返回：确认时是去掉首尾空白后的车牌（可能是空串，表示用户没填）；取消返回 nil。
// 主菜单的「出库」「寻车」和仪表盘的「反向寻车」卡片共用同一个对话框，
// 保证三处的操作手感一致（弹出即聚焦、可直接打字、可从下拉里挑）。
NSString *SmartParkPromptForParkedPlate(ParkingBridge *bridge,
                                        NSString *title,
                                        NSString *message,
                                        NSString *confirmTitle);

#endif
