// NOT COMPILED ON macOS in CI — ScreenCaptureKit is Apple-only; requires an on-device build + the GPSC.2 smoke test before the flag default flips.
//
// Native (Apple-only) implementation of the ScreenCaptureKit picker
// backend: screenCaptureKitAvailable() and captureViaPickerToPng(). Built
// with ARC (-fobjc-arc, set in CMakeLists.txt). The pure policy helpers
// live in ScreenCaptureBackend.cpp (all platforms); the non-Apple builds
// link MacScreenCapture_stub.cpp instead of this file.
//
// This file must not touch Qt beyond QString (toNSString / fromNSString) —
// it is a self-contained bridge from the SCContentSharingPicker /
// SCScreenshotManager async callback model to a synchronous, blocking C++
// call the two capture call sites can use like the screencapture path.

#include "ScreenCaptureBackend.h"

#include <QString>

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// Encode a CGImage to a PNG at `path` using ImageIO. Returns YES on success.
// With ARC the ImageIO CF objects are not memory-managed, so the
// destination is explicitly CFReleased.
static BOOL TrailerWriteCGImageAsPNG(CGImageRef image, NSString *path) {
    if (image == NULL || path.length == 0) {
        return NO;
    }
    NSURL *url = [NSURL fileURLWithPath:path];
    if (url == nil) {
        return NO;
    }
    CFStringRef pngType = (__bridge CFStringRef)UTTypePNG.identifier;
    CGImageDestinationRef dest =
        CGImageDestinationCreateWithURL((__bridge CFURLRef)url, pngType, 1, NULL);
    if (dest == NULL) {
        return NO;
    }
    CGImageDestinationAddImage(dest, image, NULL);
    const BOOL ok = CGImageDestinationFinalize(dest);
    CFRelease(dest);
    return ok;
}

// Observer bridging the picker's async callbacks to a pollable result.
// resultCode holds a trailer::PickerCaptureResult stored as NSInteger so the
// pure enum stays in the header without leaking ObjC types into it.
API_AVAILABLE(macos(14.0))
@interface TrailerPickerObserver : NSObject <SCContentSharingPickerObserver>
@property (nonatomic) BOOL done;
@property (nonatomic) NSInteger resultCode;
@property (nonatomic, copy) NSString *outPath;
@property (nonatomic, copy) NSString *errorMessage;
@end

@implementation TrailerPickerObserver

// User picked a source: capture it via SCScreenshotManager's completion-
// handler API and encode the result to PNG. The completion handler is
// delivered off the main queue, so the final state mutation is hopped back
// to the main queue to stay in step with the run-loop poll in
// captureViaPickerToPng.
- (void)contentSharingPicker:(SCContentSharingPicker *)picker
         didUpdateWithFilter:(SCContentFilter *)filter
                   forStream:(SCStream *)stream {
    (void)picker;
    (void)stream;
    if (self.done) {
        return;
    }

    SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
    // Size the capture to the picked content's native pixel dimensions:
    // contentRect is in points, pointPixelScale converts to backing pixels.
    const CGRect rect = filter.contentRect;
    const CGFloat scale = filter.pointPixelScale > 0.0f ? filter.pointPixelScale : 1.0f;
    const NSInteger pxWidth = (NSInteger)llround((double)rect.size.width * scale);
    const NSInteger pxHeight = (NSInteger)llround((double)rect.size.height * scale);
    if (pxWidth > 0 && pxHeight > 0) {
        config.width = pxWidth;
        config.height = pxHeight;
    }
    config.showsCursor = NO;

    NSString *path = self.outPath;
    __weak TrailerPickerObserver *weakSelf = self;
    [SCScreenshotManager
        captureImageWithFilter:filter
                 configuration:config
             completionHandler:^(CGImageRef image, NSError *error) {
                 dispatch_async(dispatch_get_main_queue(), ^{
                     TrailerPickerObserver *strongSelf = weakSelf;
                     if (strongSelf == nil || strongSelf.done) {
                         return;
                     }
                     if (error != nil || image == NULL) {
                         strongSelf.errorMessage = error != nil
                                                       ? error.localizedDescription
                                                       : @"ScreenCaptureKit returned no image";
                         strongSelf.resultCode =
                             (NSInteger)trailer::PickerCaptureResult::Failed;
                     } else if (TrailerWriteCGImageAsPNG(image, path)) {
                         strongSelf.resultCode = (NSInteger)trailer::PickerCaptureResult::Ok;
                     } else {
                         strongSelf.errorMessage = @"Failed to encode the captured image as PNG";
                         strongSelf.resultCode =
                             (NSInteger)trailer::PickerCaptureResult::Failed;
                     }
                     strongSelf.done = YES;
                 });
             }];
}

// User dismissed the picker without choosing a source. Delivered off the
// main queue, so the state mutation is hopped back to the main queue to stay
// in step with the run-loop poll in captureViaPickerToPng.
- (void)contentSharingPicker:(SCContentSharingPicker *)picker
          didCancelForStream:(SCStream *)stream {
    (void)picker;
    (void)stream;
    __weak TrailerPickerObserver *weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        TrailerPickerObserver *strongSelf = weakSelf;
        if (strongSelf == nil || strongSelf.done) {
            return;
        }
        strongSelf.resultCode = (NSInteger)trailer::PickerCaptureResult::Cancelled;
        strongSelf.done = YES;
    });
}

// The picker could not start (e.g. another picker session is active).
// Delivered off the main queue, so the state mutation is hopped back to the
// main queue to stay in step with the run-loop poll in captureViaPickerToPng.
- (void)contentSharingPickerStartDidFailWithError:(NSError *)error {
    __weak TrailerPickerObserver *weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        TrailerPickerObserver *strongSelf = weakSelf;
        if (strongSelf == nil || strongSelf.done) {
            return;
        }
        strongSelf.errorMessage = error != nil ? error.localizedDescription
                                                : @"The content picker failed to start";
        strongSelf.resultCode = (NSInteger)trailer::PickerCaptureResult::Failed;
        strongSelf.done = YES;
    });
}

@end

namespace trailer {

bool screenCaptureKitAvailable() {
    if (@available(macOS 14.0, *)) {
        // Guard both the OS floor and the actual class presence — a future
        // SDK/runtime skew where the framework loads but the picker class is
        // missing must read as unavailable, not crash on first message.
        return NSClassFromString(@"SCContentSharingPicker") != nil;
    }
    return false;
}

PickerCaptureResult captureViaPickerToPng(const QString &outPngPath, bool wholeDisplay,
                                          QString *errorOut) {
    if (![NSThread isMainThread]) {
        if (errorOut) {
            *errorOut = QStringLiteral("captureViaPickerToPng must be called on the main thread");
        }
        return PickerCaptureResult::Failed;
    }

    if (!screenCaptureKitAvailable()) {
        if (errorOut) {
            *errorOut = QStringLiteral("ScreenCaptureKit is unavailable on this system");
        }
        return PickerCaptureResult::Unavailable;
    }

    PickerCaptureResult result = PickerCaptureResult::Failed;

    if (@available(macOS 14.0, *)) {
        @autoreleasepool {
            TrailerPickerObserver *observer = [[TrailerPickerObserver alloc] init];
            observer.outPath = outPngPath.toNSString();
            observer.resultCode = (NSInteger)PickerCaptureResult::Failed;

            SCContentSharingPicker *picker = SCContentSharingPicker.sharedPicker;

            SCContentSharingPickerConfiguration *config =
                [[SCContentSharingPickerConfiguration alloc] init];
            config.allowedPickerModes =
                SCContentSharingPickerModeSingleDisplay | SCContentSharingPickerModeSingleWindow;
            // TODO: exclude Trailer's own windows via config.excludedWindowIDs
            // once a reliable CGWindowID list is threaded down to this layer.
            // The picker path currently takes no Qt window handle, so this is
            // skipped rather than guessed at.

            picker.configuration = config;
            [picker addObserver:observer];
            picker.active = YES;

            const SCShareableContentStyle style =
                wholeDisplay ? SCShareableContentStyleDisplay : SCShareableContentStyleWindow;
            [picker presentPickerUsingContentStyle:style];

            // Bridge async -> sync: spin the current (main) run loop until the
            // observer resolves. A dispatch_semaphore wait would deadlock —
            // the picker delivers its callbacks on this same run loop, so it
            // must keep running rather than block.
            //
            // Qt-reentrancy caveat: a nested NSRunLoop in NSDefaultRunLoopMode
            // can fire Qt timers/events reentrantly on macOS — a known risk to
            // validate under the on-device GPSC.2 smoke test.
            //
            // Bounded by a wall-clock deadline so a wedged picker (no pick, no
            // cancel, no failure callback) can't spin the main run loop forever.
            NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:120.0];
            while (!observer.done) {
                @autoreleasepool {
                    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                             beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
                }
                if (!observer.done && [deadline timeIntervalSinceNow] <= 0) {
                    observer.resultCode = (NSInteger)PickerCaptureResult::Failed;
                    if (errorOut) {
                        *errorOut = QStringLiteral("Timed out waiting for the content picker");
                    }
                    break;
                }
            }

            // Tear the picker session down before returning so a subsequent
            // capture starts clean (the picker is a shared singleton).
            [picker removeObserver:observer];
            picker.active = NO;

            result = (PickerCaptureResult)observer.resultCode;
            if (errorOut != nullptr && observer.errorMessage != nil) {
                *errorOut = QString::fromNSString(observer.errorMessage);
            }
        }
    }

    return result;
}

} // namespace trailer
