#import "AppDelegate.h"
#import "MainWindowController.h"

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification{
    self.mainWindowController = [[MainWindowController alloc] init];
    [self.mainWindowController showWindow:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender{
    return YES;
}

@end
