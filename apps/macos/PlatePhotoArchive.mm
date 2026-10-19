#import "PlatePhotoArchive.h"

#import <dispatch/dispatch.h>

#include <cstdlib>

namespace{
const NSUInteger kPlatePhotoArchiveError = 1;

// 车牌直接进文件名（APFS 支持中文），只挡掉路径分隔符与空白这些会让文件名
// 变成另一条路径的字符；车牌识别结果不会带这些字符，属于兜底。
NSString *sanitizedPlate(NSString *plate){
    NSCharacterSet *blocked = [NSCharacterSet characterSetWithCharactersInString:@"/\\:*?\"<>|"];
    NSString *clean = [[plate componentsSeparatedByCharactersInSet:blocked]
        componentsJoinedByString:@"_"];
    clean = [clean stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    return clean.length > 0 ? clean : @"未知车牌";
}

NSDateFormatter *timestampFormatter(void){
    static NSDateFormatter *formatter;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        formatter = [[NSDateFormatter alloc] init];
        // 固定 locale/日历：中文环境或其它区域设置都不能改变文件名里的时间格式。
        formatter.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
        formatter.calendar = [[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
        formatter.dateFormat = @"yyyyMMdd-HHmmss";
    });
    return formatter;
}

NSError *archiveError(NSString *message){
    return [NSError errorWithDomain:@"SmartParkPlatePhotoArchive"
                               code:kPlatePhotoArchiveError
                           userInfo:@{NSLocalizedDescriptionKey: message}];
}

// 照片要落在仓库的 examples/platephoto 里（老师/验收时直接翻项目目录就能看到），
// 但 app 是从 build/ 里跑的，不能写死相对路径：从可执行文件所在目录逐级向上找
// 仓库根（同时有 CMakeLists.txt 和 examples/plates 的目录）。找不到（比如把 app
// 拷到 /Applications）就退回应用数据目录，功能不受影响。
NSString *repositoryRoot(void){
    NSFileManager *manager = [NSFileManager defaultManager];
    NSMutableArray<NSString *> *origins = [NSMutableArray array];
    NSString *executable = [[NSBundle mainBundle] executablePath];
    if (executable.length > 0){
        [origins addObject:[executable stringByDeletingLastPathComponent]];
    }
    NSString *working = [manager currentDirectoryPath];
    if (working.length > 0){
        [origins addObject:working];
    }
    for (NSString *origin in origins){
        NSString *candidate = origin;
        for (NSUInteger depth = 0; depth < 10 && candidate.length > 1; ++depth){
            NSString *marker = [candidate stringByAppendingPathComponent:@"CMakeLists.txt"];
            NSString *plates = [candidate stringByAppendingPathComponent:@"examples/plates"];
            if ([manager fileExistsAtPath:marker] && [manager fileExistsAtPath:plates]){
                return candidate;
            }
            candidate = [candidate stringByDeletingLastPathComponent];
        }
    }
    return nil;
}
} // namespace

@implementation PlatePhotoArchive

+ (NSString *)directoryWithError:(NSError **)error{
    // 测试与自定义部署可以换目录（指向临时目录/外置盘），默认落在仓库的 examples/platephoto。
    const char *override = getenv("SMARTPARK_PLATE_PHOTO_DIR");
    NSString *directory = nil;
    if (override != nullptr && override[0] != '\0'){
        directory = [NSString stringWithUTF8String:override];
    } else {
        NSString *root = repositoryRoot();
        if (root != nil){
            directory = [[root stringByAppendingPathComponent:@"examples"]
                stringByAppendingPathComponent:@"platephoto"];
        } else {
            NSArray<NSString *> *bases = NSSearchPathForDirectoriesInDomains(
                NSApplicationSupportDirectory, NSUserDomainMask, YES);
            NSString *base = bases.firstObject;
            if (base.length == 0){
                if (error != nullptr){
                    *error = archiveError(@"找不到应用数据目录");
                }
                return nil;
            }
            directory = [[base stringByAppendingPathComponent:@"smartpark"]
                stringByAppendingPathComponent:@"plate-photos"];
        }
    }
    NSError *createError = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:directory
                                   withIntermediateDirectories:YES
                                                    attributes:nil
                                                         error:&createError]){
        // 已存在且是目录时 createDirectory 会返回 YES；走到这里是真的建不出来。
        if (error != nullptr){
            *error = createError ?: archiveError(@"无法创建车牌照片目录");
        }
        return nil;
    }
    return directory;
}

+ (NSString *)historyDirectoryWithError:(NSError **)error{
    NSString *directory = [self directoryWithError:error];
    if (directory == nil){
        return nil;
    }
    NSString *history = [directory stringByAppendingPathComponent:@"history"];
    NSError *createError = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:history
                                   withIntermediateDirectories:YES
                                                    attributes:nil
                                                         error:&createError]){
        if (error != nullptr){
            *error = createError ?: archiveError(@"无法创建历史照片目录");
        }
        return nil;
    }
    return history;
}

+ (NSString *)archiveImageAtPath:(NSString *)sourcePath
                           plate:(NSString *)plate
                           error:(NSError **)error{
    if (sourcePath.length == 0 || ![[NSFileManager defaultManager] fileExistsAtPath:sourcePath]){
        if (error != nullptr){
            *error = archiveError(@"车牌照片源文件不存在");
        }
        return nil;
    }
    NSError *directoryError = nil;
    NSString *directory = [self directoryWithError:&directoryError];
    if (directory == nil){
        if (error != nullptr){
            *error = directoryError;
        }
        return nil;
    }

    // 保留源文件扩展名（识别流水线给的是 jpg 裁剪图，自定义脚本可能给 png）。
    NSString *extension = [sourcePath pathExtension].lowercaseString;
    if (extension.length == 0){
        extension = @"jpg";
    }
    NSString *stem = [NSString stringWithFormat:@"%@_%@", sanitizedPlate(plate),
        [timestampFormatter() stringFromDate:[NSDate date]]];
    NSString *destination = [directory stringByAppendingPathComponent:
        [stem stringByAppendingPathExtension:extension]];
    NSFileManager *manager = [NSFileManager defaultManager];
    // 同一秒内同车牌连拍（重试）时避免互相覆盖。后缀用下划线而不是连字符：
    // 按文件名排序时 "…_120000.jpg" < "…_120000_1.jpg"（'.' < '_'），
    // 这样「取最新一张」仍然是最后归档的那张。
    for (NSUInteger suffix = 1; [manager fileExistsAtPath:destination]; ++suffix){
        destination = [directory stringByAppendingPathComponent:
            [[NSString stringWithFormat:@"%@_%lu", stem, (unsigned long)suffix]
                stringByAppendingPathExtension:extension]];
    }

    NSError *copyError = nil;
    if (![manager copyItemAtPath:sourcePath toPath:destination error:&copyError]){
        if (error != nullptr){
            *error = copyError ?: archiveError(@"复制车牌照片失败");
        }
        return nil;
    }
    return destination;
}

+ (NSArray<NSString *> *)photosForPlate:(NSString *)plate{
    NSError *directoryError = nil;
    NSString *directory = [self directoryWithError:&directoryError];
    if (directory == nil){
        return @[];
    }
    NSString *prefix = [sanitizedPlate(plate) stringByAppendingString:@"_"];
    NSArray<NSString *> *entries = [[NSFileManager defaultManager]
        contentsOfDirectoryAtPath:directory error:nil] ?: @[];
    NSMutableArray<NSString *> *matches = [NSMutableArray array];
    for (NSString *name in entries){
        if ([name hasPrefix:prefix]){
            [matches addObject:[directory stringByAppendingPathComponent:name]];
        }
    }
    // 文件名里是 yyyyMMdd-HHmmss，按名字倒序即「新的在前」，无需读文件属性。
    return [matches sortedArrayUsingComparator:^NSComparisonResult(NSString *lhs, NSString *rhs){
        return [rhs.lastPathComponent compare:lhs.lastPathComponent];
    }];
}

+ (NSString *)latestPhotoForPlate:(NSString *)plate{
    return [self photosForPlate:plate].firstObject;
}

+ (NSArray<NSString *> *)movePhotosForPlate:(NSString *)plate
                                  entryTime:(NSDate *)entryTime
                                   exitTime:(NSDate *)exitTime
                                      error:(NSError **)error{
    // photosForPlate 是新的在前，挪进 history 时按时间先后改名，第一张就是入场那张。
    NSArray<NSString *> *photos =
        [[self photosForPlate:plate] reverseObjectEnumerator].allObjects;
    if (photos.count == 0){
        return @[];
    }
    NSString *history = [self historyDirectoryWithError:error];
    if (history == nil){
        return @[];
    }
    NSString *plateText = sanitizedPlate(plate);
    NSString *entryStamp = [timestampFormatter() stringFromDate:entryTime ?: [NSDate date]];
    NSString *exitStamp = [timestampFormatter() stringFromDate:exitTime ?: [NSDate date]];
    // 文件名＝车牌 + 入库时间 + 出库时间；同一趟停车有多张照片（入场/出场各一张）时，
    // 第二张起加 _2、_3，顺着序号就能看出先后。
    NSString *stem = [NSString stringWithFormat:@"%@_%@_%@", plateText, entryStamp, exitStamp];
    NSFileManager *manager = [NSFileManager defaultManager];
    NSMutableArray<NSString *> *moved = [NSMutableArray array];
    NSError *failure = nil;
    for (NSUInteger index = 0; index < photos.count; ++index){
        NSString *source = photos[index];
        NSString *extension = [source pathExtension].lowercaseString;
        if (extension.length == 0){
            extension = @"jpg";
        }
        NSString *base = index == 0 ? stem : [NSString stringWithFormat:@"%@_%lu",
            stem, (unsigned long)(index + 1)];
        NSString *destination = [history stringByAppendingPathComponent:
            [base stringByAppendingPathExtension:extension]];
        for (NSUInteger suffix = 1; [manager fileExistsAtPath:destination]; ++suffix){
            destination = [history stringByAppendingPathComponent:
                [[NSString stringWithFormat:@"%@_%lu", base, (unsigned long)suffix]
                    stringByAppendingPathExtension:extension]];
        }
        NSError *moveError = nil;
        if ([manager moveItemAtPath:source toPath:destination error:&moveError]){
            [moved addObject:destination];
        } else {
            // 单张挪失败不影响其它张：记下第一条原因继续，已挪走的仍然可用。
            failure = failure ?: (moveError ?: archiveError(@"挪动车牌照片失败"));
        }
    }
    if (failure != nil && error != nullptr){
        *error = failure;
    }
    return moved;
}

@end
