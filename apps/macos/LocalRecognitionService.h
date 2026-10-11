#import <Foundation/Foundation.h>

// Application-owned models; all imports, weight loading and warmup run off UI.
@interface LocalRecognitionService : NSObject
+ (instancetype)sharedService;
+ (void)preloadDefault;
- (NSString *)startWithScript:(NSString *)script
              detectorPython:(NSString *)detectorPython
                   ocrPython:(NSString *)ocrPython
                 environment:(NSDictionary<NSString *, NSString *> *)environment;
@property (nonatomic, readonly) NSTask *task;
@property (nonatomic, readonly) NSString *diagnostics;
- (void)stop;
@end
