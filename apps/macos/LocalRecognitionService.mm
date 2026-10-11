#import "LocalRecognitionService.h"
#import <AppKit/AppKit.h>
#include <cstdlib>

@implementation LocalRecognitionService{
    NSTask *_task;
    NSString *_socket;
    NSString *_diagnostics;
    NSUInteger _generation;
    BOOL _starting;
    BOOL _closed;
}

+ (instancetype)sharedService{
    static LocalRecognitionService *service;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ service = [[self alloc] init]; });
    return service;
}

+ (void)preloadDefault{
    LocalRecognitionService *owner = [self sharedService];
    NSUInteger generation;
    @synchronized(owner){ generation = owner->_generation; }
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        const char *custom = getenv("SMARTPARK_LPR_SCRIPT");
        if (custom != nullptr && custom[0] != '\0'){
            return; // An arbitrary script need not support resident inference.
        }
        NSMutableDictionary *environment = [[[NSProcessInfo processInfo] environment] mutableCopy];
        NSMutableArray<NSString *> *interpreters = [NSMutableArray array];
        for (NSString *kind in @[@"LPR", @"OCR"]){
            NSString *key = [NSString stringWithFormat:@"SMARTPARK_%@_PY", kind];
            const char *value = getenv(key.UTF8String);
            NSString *path = value && value[0] ? [NSString stringWithUTF8String:value] : nil;
            if (path == nil || ![[NSFileManager defaultManager] fileExistsAtPath:path]){
                path = [NSHomeDirectory() stringByAppendingPathComponent:
                    [NSString stringWithFormat:@".smartpark/%@/bin/python", kind.lowercaseString]];
            }
            [interpreters addObject:path];
        }
        environment[@"PYTHONDONTWRITEBYTECODE"] = @"1";
        environment[@"PYTHONIOENCODING"] = @"utf-8";
        environment[@"YOLO_OFFLINE"] = @"true";
        environment[@"SMARTPARK_LPR_CROP_DIR"] = [NSTemporaryDirectory()
            stringByAppendingPathComponent:@"smartpark-lpr-preview"];
        @synchronized(owner){
            if (owner->_generation != generation || owner->_closed){ return; }
            [owner startWithScript:@SMARTPARK_LPR_SCRIPT_PATH
                detectorPython:interpreters[0] ocrPython:interpreters[1] environment:environment];
        }
    });
}

- (instancetype)init{
    if ((self = [super init])){
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(shutdown:)
            name:NSApplicationWillTerminateNotification object:nil];
    }
    return self;
}

- (void)dealloc{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    if (_task.running){ [_task terminate]; }
}

- (NSTask *)task{ @synchronized(self){ return _task; } }
- (NSString *)diagnostics{ @synchronized(self){ return _diagnostics; } }

- (void)shutdown:(NSNotification *)notification{
    @synchronized(self){ _closed = YES; }
    [self stop];
}

- (void)stop{
    NSTask *task;
    @synchronized(self){
        ++_generation;
        task = _task;
        _task = nil;
        _socket = nil;
        _starting = NO;
    }
    if (task.running){ [task terminate]; }
}

- (NSString *)startWithScript:(NSString *)script
              detectorPython:(NSString *)detectorPython
                   ocrPython:(NSString *)ocrPython
                 environment:(NSDictionary<NSString *, NSString *> *)environment{
    NSString *socket;
    NSUInteger generation;
    @synchronized(self){
        if (_closed){ return nil; }
        if (_starting || _task.running){ return _socket; }
        _socket = [NSTemporaryDirectory() stringByAppendingPathComponent:
            [NSString stringWithFormat:@"sp-lpr-%@.sock", [[NSUUID UUID].UUIDString substringToIndex:16]]];
        socket = _socket;
        _task = nil; // Never expose an exited generation's PID to a new client.
        generation = ++_generation;
        _starting = YES;
        _diagnostics = nil;
    }
    NSDictionary *childEnvironment = [environment copy];
    __weak LocalRecognitionService *weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        NSString *directory = [script stringByDeletingLastPathComponent];
        NSString *service = [directory stringByAppendingPathComponent:@"lpr_service.py"];
        NSString *sample = [[directory stringByDeletingLastPathComponent]
            stringByAppendingPathComponent:@"examples/plates/blue-01.jpg"];
        NSTask *task = [[NSTask alloc] init];
        task.executableURL = [NSURL fileURLWithPath:detectorPython];
        task.arguments = @[@"-B", service, @"--socket", socket, @"--ocr-python", ocrPython,
            @"--warmup-image", sample, @"--parent-pid",
            [NSString stringWithFormat:@"%d", [NSProcessInfo processInfo].processIdentifier]];
        task.environment = childEnvironment;
        task.qualityOfService = NSQualityOfServiceUtility;
        task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
        NSPipe *errors = [NSPipe pipe];
        task.standardError = errors;
        LocalRecognitionService *owner = weakSelf;
        if (owner == nil){ return; }
        @synchronized(owner){ if (owner->_generation != generation || owner->_closed){ return; } }
        NSError *error = nil;
        BOOL launched = [task launchAndReturnError:&error];
        @synchronized(owner){
            if (owner->_generation != generation || owner->_closed){
                if (task.running){ [task terminate]; }
            } else {
                owner->_starting = NO;
                owner->_task = launched ? task : nil;
                if (!launched){ owner->_diagnostics = error.localizedDescription; }
            }
        }
        owner = nil;
        if (!launched){ return; }
        NSMutableData *tail = [NSMutableData data];
        for (;;){
            NSData *chunk = [errors.fileHandleForReading readDataOfLength:4096];
            if (chunk.length == 0){ break; }
            [tail appendData:chunk];
            if (tail.length > 8192){ [tail replaceBytesInRange:NSMakeRange(0, tail.length - 8192) withBytes:nullptr length:0]; }
        }
        [task waitUntilExit];
        owner = weakSelf;
        if (owner != nil){
            @synchronized(owner){
                if (owner->_generation == generation && task.terminationStatus != 0){
                    owner->_diagnostics = [[NSString alloc] initWithData:tail encoding:NSUTF8StringEncoding]
                        ?: @"本机模型加载失败，请重试识别。";
                }
            }
        }
    });
    return socket;
}
@end
