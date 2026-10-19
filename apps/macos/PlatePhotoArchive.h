#import <Foundation/Foundation.h>

// 车牌照片留档：入库识别成功后把车牌图复制到固定目录，出库时按车牌取回最近一张；
// 出库办完再把这次停车的照片挪进 history/，文件名换成入库、出库两个时间。
//
// 目录：<仓库>/examples/platephoto/（从可执行文件往上找含 CMakeLists.txt + examples/plates
// 的目录；找不到时退回 ~/Library/Application Support/smartpark/plate-photos/）。
// 在场期间文件名：<车牌>_<yyyyMMdd-HHmmss>.<ext>，例如 京A12345_20261011-104233.jpg
// 出库后挪进 history/ 并改名：<车牌>_<入库 yyyyMMdd-HHmmss>_<出库 yyyyMMdd-HHmmss>.<ext>，
// 同一趟停车有多张时按时间顺序加 _2、_3 后缀（第一张是入场那张）。
//
// 只做「复制 + 按车牌找 + 出库后挪走」，不碰数据库；照片是给出入口人工核对的旁证，
// 不参与业务判定（进出场仍以车牌为准）。同名同一秒重复归档时自动加 -1/-2 后缀。
// 失败一律返回 nil（不抛异常），原因写进 error。
@interface PlatePhotoArchive : NSObject

// 归档目录（不存在时创建）。
+ (NSString *)directoryWithError:(NSError **)error;

// 出库后的历史目录：归档目录下的 history/（不存在时创建）。
+ (NSString *)historyDirectoryWithError:(NSError **)error;

// 把 sourcePath 复制进归档目录，返回落地后的路径。
+ (NSString *)archiveImageAtPath:(NSString *)sourcePath
                           plate:(NSString *)plate
                           error:(NSError **)error;

// 该车牌最近一次归档的照片路径；没有归档过返回 nil。
+ (NSString *)latestPhotoForPlate:(NSString *)plate;

// 该车牌的全部归档照片，新的在前（文件名里的时间戳可直接排序）。
+ (NSArray<NSString *> *)photosForPlate:(NSString *)plate;

// 出库办完后调用：把该车牌在场期间的照片挪进 history/，按停车时间段改名。
// 返回挪好的路径（按原归档时间先后，第一张是入场照片）；没有照片时返回空数组。
+ (NSArray<NSString *> *)movePhotosForPlate:(NSString *)plate
                                  entryTime:(NSDate *)entryTime
                                   exitTime:(NSDate *)exitTime
                                      error:(NSError **)error;

@end
