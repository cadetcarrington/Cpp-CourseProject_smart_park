#import "PlatePrompt.h"

#import "bridge/ParkingBridge.h"

#include <string>

NSString *SmartParkPromptForParkedPlate(ParkingBridge *bridge,
                                        NSString *title,
                                        NSString *message,
                                        NSString *confirmTitle){
    NSMutableArray<NSString *> *plates = [NSMutableArray array];
    if (bridge != nullptr && bridge->ready()){
        for (const std::string &plate : bridge->parkedPlateNumbers()){
            [plates addObject:[NSString stringWithUTF8String:plate.c_str()]];
        }
    }

    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = plates.count > 0
        ? message
        : [message stringByAppendingString:@"\n\n当前没有可选的在场车辆，也可以直接输入车牌号。"];
    [alert addButtonWithTitle:confirmTitle.length > 0 ? confirmTitle : @"继续"];
    [alert addButtonWithTitle:@"取消"];

    NSComboBox *combo = [[NSComboBox alloc] initWithFrame:NSMakeRect(0, 0, 280, 26)];
    combo.placeholderString = @"输入或选择在场车牌";
    combo.completes = YES;
    combo.numberOfVisibleItems = 8;
    combo.font = [NSFont systemFontOfSize:13];
    for (NSString *plate in plates){
        [combo addItemWithObjectValue:plate];
    }
    if (plates.count > 0){
        combo.stringValue = plates.firstObject;
    }
    alert.accessoryView = combo;
    // 弹出来就能直接打字，不用先点输入框。
    [alert.window setInitialFirstResponder:combo];
    if ([alert runModal] != NSAlertFirstButtonReturn){
        return nil;
    }

    return [combo.stringValue stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
}
