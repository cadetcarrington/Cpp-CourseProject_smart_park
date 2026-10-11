#import <Foundation/Foundation.h>

// 车牌照片留档：入库识别成功后把车牌图复制到固定目录，出库时按车牌取回最近一张。
//
// 目录：~/Library/Application Support/smartpark/plate-photos/
// 文件名：<车牌>_<yyyyMMdd-HHmmss>.<ext>，例如 京A12345_20261011-104233.jpg
//
// 只做「复制 + 按车牌找最新」，不碰数据库；照片是给出入口人工核对的旁证，
// 不参与业务判定（进出场仍以车牌为准）。同名同一秒重复归档时自动加 -1/-2 后缀。
// 失败一律返回 nil（不抛异常），原因写进 error。
@interface PlatePhotoArchive : NSObject

// 归档目录（不存在时创建）。
+ (NSString *)directoryWithError:(NSError **)error;

// 把 sourcePath 复制进归档目录，返回落地后的路径。
+ (NSString *)archiveImageAtPath:(NSString *)sourcePath
                           plate:(NSString *)plate
                           error:(NSError **)error;

// 该车牌最近一次归档的照片路径；没有归档过返回 nil。
+ (NSString *)latestPhotoForPlate:(NSString *)plate;

// 该车牌的全部归档照片，新的在前（文件名里的时间戳可直接排序）。
+ (NSArray<NSString *> *)photosForPlate:(NSString *)plate;

@end
