#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <PDFKit/PDFKit.h>
#import <UserNotifications/UserNotifications.h>

#include "MacSystemBridge.h"
#include <QString>

// UNUserNotificationCenter 的 delegate 必须全程存活，且要在应用前台时
// 手动回调 presentation 才能看到横幅。ObjC 类不能放在 C++ namespace 内。
@interface SmartParkNotifDelegate : NSObject <UNUserNotificationCenterDelegate>
@end
@implementation SmartParkNotifDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))handler {
    handler(UNNotificationPresentationOptionBanner
            | UNNotificationPresentationOptionSound);
}
@end

namespace macbridge{
namespace{

SmartParkNotifDelegate *notifDelegate = nil;

UNUserNotificationCenter *activeCenter(){
    UNUserNotificationCenter *center =
        [UNUserNotificationCenter currentNotificationCenter];
    if (center == nil){
        return nil;
    }
    if (notifDelegate == nil){
        notifDelegate = [[SmartParkNotifDelegate alloc] init];
        center.delegate = notifDelegate;
        // 授权弹窗异步出现，这里只发起请求不等待（等待会阻塞主线程）。
        UNAuthorizationOptions options = UNAuthorizationOptionAlert
            | UNAuthorizationOptionSound;
        [center requestAuthorizationWithOptions:options
                              completionHandler:^(BOOL granted, NSError *){}];
    }
    return center;
}

NSString *nsString(const QString &text){
    return [NSString stringWithUTF8String:text.toUtf8().constData()];
}
} // namespace

void setMenuBarStatus(const QString &text){
    // NSStatusItem 必须强引用持有，释放即从菜单栏消失。
    static NSStatusItem *statusItem = nil;
    if (statusItem == nil){
        statusItem = [[NSStatusBar systemStatusBar]
            statusItemWithLength:NSVariableStatusItemLength];
    }
    statusItem.button.title = nsString(text);
}

void postNotification(const QString &title, const QString &body){
    UNUserNotificationCenter *center = activeCenter();
    if (center == nil){
        return;
    }
    UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
    content.title = nsString(title);
    content.body = nsString(body);
    content.sound = [UNNotificationSound defaultSound];
    UNNotificationRequest *request =
        [UNNotificationRequest requestWithIdentifier:[NSUUID UUID].UUIDString
                                             content:content
                                             trigger:nil];
    [center addNotificationRequest:request withCompletionHandler:^(NSError *){}];
}

void speakChinese(const QString &text){
    static AVSpeechSynthesizer *synthesizer = nil;
    if (synthesizer == nil){
        synthesizer = [[AVSpeechSynthesizer alloc] init];
    }
    AVSpeechUtterance *utterance =
        [AVSpeechUtterance speechUtteranceWithString:nsString(text)];
    AVSpeechSynthesisVoice *voice =
        [AVSpeechSynthesisVoice voiceWithLanguage:@"zh-CN"];
    if (voice != nil){
        utterance.voice = voice;
    }
    utterance.rate = AVSpeechUtteranceDefaultSpeechRate * 0.9;
    [synthesizer speakUtterance:utterance];
}

bool exportTextToPdf(const QString &title, const QString &body,
                     const QString &pdfPath){
    NSFont *font = [NSFont fontWithName:@"PingFang SC" size:12]
                   ?: [NSFont systemFontOfSize:12];
    NSMutableAttributedString *text = [[NSMutableAttributedString alloc]
        initWithString:[NSString stringWithFormat:@"%@\n\n%@",
                        nsString(title), nsString(body)]
            attributes:@{NSFontAttributeName: font}];
    NSMutableParagraphStyle *paragraph = [[NSMutableParagraphStyle alloc] init];
    paragraph.lineBreakMode = NSLineBreakByCharWrapping;
    paragraph.paragraphSpacing = 6;
    [text addAttribute:NSParagraphStyleAttributeName value:paragraph
                 range:NSMakeRange(0, text.length)];

    // A4 页面（595 x 842 pt），用 NSTextView 的 PDF 生成能力。
    NSTextView *view = [[NSTextView alloc]
        initWithFrame:NSMakeRect(0, 0, 515, 782)];
    [view.textStorage setAttributedString:text];
    NSData *pdfData = [view dataWithPDFInsideRect:view.bounds];

    PDFDocument *document = [[PDFDocument alloc] initWithData:pdfData];
    if (document == nil){
        return NO;
    }
    document.documentAttributes = @{
        PDFDocumentTitleAttribute: nsString(title),
        PDFDocumentAuthorAttribute: @"SmartPark Admin",
        PDFDocumentCreatorAttribute: @"SmartPark"
    };
    NSData *finalData = [document dataRepresentation];
    return [finalData writeToFile:nsString(pdfPath) atomically:YES];
}

bool revealInFinder(const QString &path){
    return [[NSWorkspace sharedWorkspace]
        selectFile:nsString(path) inFileViewerRootedAtPath:@""] != NO;
}

std::optional<std::string> httpPostJson(const std::string &url,
                                        const std::string &apiKey,
                                        const std::string &requestJson,
                                        int timeoutSeconds){
    NSURL *nsUrl = [NSURL URLWithString:
        [NSString stringWithUTF8String:url.c_str()]];
    if (nsUrl == nil){
        return std::nullopt;
    }
    NSMutableURLRequest *request =
        [NSMutableURLRequest requestWithURL:nsUrl];
    request.HTTPMethod = @"POST";
    request.timeoutInterval = timeoutSeconds;
    [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
    if (!apiKey.empty()){
        [request setValue:
            [NSString stringWithFormat:@"Bearer %@",
             QString::fromStdString(apiKey).toUtf8().constData()]
          forHTTPHeaderField:@"Authorization"];
    }
    request.HTTPBody =
        [[NSString stringWithUTF8String:requestJson.c_str()]
            dataUsingEncoding:NSUTF8StringEncoding];

    __block std::optional<std::string> result;
    dispatch_semaphore_t finished = dispatch_semaphore_create(0);
    NSURLSessionDataTask *task = [[NSURLSession sharedSession]
        dataTaskWithRequest:request
          completionHandler:^(NSData *data, NSURLResponse *response,
                              NSError *error) {
            if (error != nil){
                result = std::string("ERROR: ")
                    + error.localizedDescription.UTF8String;
            } else if (data != nil){
                result = std::string(reinterpret_cast<const char *>(data.bytes),
                                     data.length);
            }
            dispatch_semaphore_signal(finished);
        }];
    [task resume];
    dispatch_semaphore_wait(finished, dispatch_time(
        DISPATCH_TIME_NOW, (timeoutSeconds + 5) * NSEC_PER_SEC));
    return result;
}

} // namespace macbridge
