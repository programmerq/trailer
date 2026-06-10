// macOS backend for the UX recorder (see UxPlatformCapture.h and
// docs/ux-recorder.md):
//
//   screen   ScreenCaptureKit display stream → JPEG frame sequence
//            under screen/, retained only while Trailer or Preview is
//            frontmost. Frames-not-video is a deliberate MVP choice:
//            pausing is trivial, every frame self-correlates with the
//            timeline via its filename + screen_frame event, and the
//            analysis side (jq / OpenCV / vision models) consumes
//            images directly. ScreenCaptureKit needs macOS 12.3+; on
//            older systems a screen_capture_unsupported event is
//            recorded and the rest of the session continues.
//   camera   AVFoundation movie capture → camera/camera-000.mov, one
//            continuous segment per session. The camera records the
//            *user* (reactions), so it intentionally keeps rolling
//            across app switches and visual-capture pauses.
//   apps     NSWorkspace activation notifications → app_activated
//            events (bundle id + pid), driving the frontmost gate.
//            "Trailer" is identified by pid rather than bundle id so
//            unbundled dev binaries behave; Preview by bundle id.
//   input    Listen-only CGEventTap on a dedicated CFRunLoop thread.
//            Events are retained only while Trailer or Preview is
//            frontmost; mouse moves and scrolls are batched the same
//            way the Qt-side capture batches them. Requires the Input
//            Monitoring permission; when missing, an
//            input_tap_unavailable event is recorded and everything
//            else keeps working.
//
// All permission prompts originate here and therefore only ever fire
// in recorder-enabled builds that were launched with --ux-record.
//
// Lifetime: system completion handlers (permission prompts, stream
// start) can fire long after the session ends — a quit while a
// permission dialog is still open is enough. Every asynchronous
// callback therefore goes through a shared UxLifeGuard: stop() flips
// alive=false under the guard mutex as its final act, after which
// late callbacks return without touching the (about to be freed)
// capture object or the recorder behind its context functions.

#include "uxrecord/UxPlatformCapture.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
// hidsystem/, not hid/ — the identically-named HID Manager header in
// IOKit/hid/ does not declare IOHIDCheckAccess / IOHIDRequestAccess
// or the kIOHIDRequestType* / kIOHIDAccessType* enums.
#include <IOKit/hidsystem/IOHIDLib.h>

#if __has_include(<ScreenCaptureKit/ScreenCaptureKit.h>)
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#define TRAILER_HAS_SCREENCAPTUREKIT 1
#else
#define TRAILER_HAS_SCREENCAPTUREKIT 0
#endif

#include <unistd.h>

#include <atomic>
#include <memory>
#include <mutex>

namespace trailer {

class MacUxPlatformCapture;

// See the "Lifetime" note in the header comment.
struct UxLifeGuard {
    std::mutex mutex;
    bool alive = true;
};

} // namespace trailer

#if TRAILER_HAS_SCREENCAPTUREKIT
API_AVAILABLE(macos(12.3))
@interface TrailerUxScreenDelegate : NSObject <SCStreamDelegate, SCStreamOutput> {
  @public
    trailer::MacUxPlatformCapture *_owner;
    std::shared_ptr<trailer::UxLifeGuard> _guard;
}
@end
#endif

@interface TrailerUxCameraDelegate : NSObject <AVCaptureFileOutputRecordingDelegate> {
  @public
    trailer::MacUxPlatformCapture *_owner;
    std::shared_ptr<trailer::UxLifeGuard> _guard;
}
@property(nonatomic, strong) dispatch_semaphore_t finishedSemaphore;
@end

namespace trailer {

namespace {

// Screen frame cadence. 3 fps reconstructs "what was on screen while
// the events happened" without producing video-scale data; the JSONL
// stream carries the fine-grained motion. Raise toward 10 for
// animation-heavy investigations at proportional disk cost.
constexpr int kScreenFps = 3;

// JPEG quality for screen frames. 0.6 keeps body text legible on a
// retina display at roughly 150–400 KB per 2560-px frame.
constexpr double kScreenJpegQuality = 0.6;

// Longest output side in pixels. Full 5K/6K retina frames are larger
// than any analysis here needs; 2560 keeps UI text readable.
constexpr int kScreenMaxLongSidePx = 2560;

// Global-input batching (mirrors UxQtEventCapture's values).
constexpr qint64 kTapMouseSampleIntervalMs = 30;
constexpr int kTapMousePathMaxSamples = 150;
constexpr qint64 kTapBatchMaxAgeMs = 1000;

// Cmd+Shift+M — the same chord as Trailer's in-app frustration-marker
// action — observed by the tap so a marker can be dropped while
// *Preview* is frontmost. kVK_ANSI_M.
constexpr int64_t kMarkerHotkeyKeyCode = 46;

enum class FrontKind : int { Other = 0, Trailer = 1, Preview = 2 };

QString frontKindName(int kind) {
    switch (static_cast<FrontKind>(kind)) {
    case FrontKind::Trailer:
        return QStringLiteral("trailer");
    case FrontKind::Preview:
        return QStringLiteral("preview");
    case FrontKind::Other:
        break;
    }
    return QStringLiteral("other");
}

QString tapButtonName(CGEventType type, CGEventRef event) {
    switch (type) {
    case kCGEventLeftMouseDown:
    case kCGEventLeftMouseUp:
        return QStringLiteral("left");
    case kCGEventRightMouseDown:
    case kCGEventRightMouseUp:
        return QStringLiteral("right");
    default:
        break;
    }
    const int64_t number = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
    return number == 2 ? QStringLiteral("middle")
                       : QStringLiteral("button%1").arg(QString::number(number));
}

QString tapModifierNames(CGEventFlags flags) {
    QStringList parts;
    if (flags & kCGEventFlagMaskCommand)
        parts << QStringLiteral("cmd");
    if (flags & kCGEventFlagMaskShift)
        parts << QStringLiteral("shift");
    if (flags & kCGEventFlagMaskAlternate)
        parts << QStringLiteral("alt");
    if (flags & kCGEventFlagMaskControl)
        parts << QStringLiteral("ctrl");
    if (flags & kCGEventFlagMaskSecondaryFn)
        parts << QStringLiteral("fn");
    return parts.join(QLatin1Char('+'));
}

} // namespace

class MacUxPlatformCapture : public UxPlatformCapture {
  public:
    explicit MacUxPlatformCapture(UxCaptureContext context) : m_ctx(std::move(context)) {}

    ~MacUxPlatformCapture() override { stop(); }

    bool isSupported() const override { return true; }

    void start() override;
    void stop() override;

    void setPaused(bool paused) override {
        m_paused.store(paused);
        flushTapBatches();
    }
    bool isPaused() const override { return m_paused.load(); }

    // --- shared with the ObjC delegates / C callbacks ---------------

    void emitEvent(const QString &type, const QJsonObject &data = {}) {
        if (m_ctx.emitEvent) {
            m_ctx.emitEvent(type, data);
        }
    }

    void handleScreenFrame(CMSampleBufferRef sampleBuffer);
    void handleTapEvent(CGEventType type, CGEventRef event);
    void flushTapBatches();

    CFMachPortRef m_tap = nullptr;

  private:
    void startForegroundWatcher();
    void startScreenCapture();
    void startCamera();
    void startCameraSession();
    void cameraSessionBody();
    void startInputTap();
    void stopScreenCapture();
    void stopCamera();
    void stopInputTap();
    void updateFrontmost(NSRunningApplication *app, bool initial);
    void sampleTapMouse(CGEventRef event, bool dragging);
    QString writeJpegFrame(CVImageBufferRef pixelBuffer, quint64 sequence, qint64 elapsed);
#if TRAILER_HAS_SCREENCAPTUREKIT
    void onShareableContent(SCShareableContent *content, NSError *error)
        API_AVAILABLE(macos(12.3));
#endif

    bool retainNow() const {
        return !m_paused.load() && m_front.load() != static_cast<int>(FrontKind::Other);
    }

    UxCaptureContext m_ctx;
    std::shared_ptr<UxLifeGuard> m_guard = std::make_shared<UxLifeGuard>();
    std::atomic<int> m_front{static_cast<int>(FrontKind::Other)};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_stopping{false};

    // Foreground tracking (observer fires on the main queue, same
    // thread as start()/stop(), so no guard is needed for it).
    id m_activationObserver = nil;

    // Screen capture (members typed `id` so the class declaration
    // doesn't need 12.3 availability annotations).
    id m_stream = nil;
    id m_screenDelegate = nil;
    dispatch_queue_t m_screenQueue = nullptr;
    std::atomic<quint64> m_frameSequence{0};

    // Camera capture.
    AVCaptureSession *m_cameraSession = nil;
    AVCaptureMovieFileOutput *m_cameraOutput = nil;
    TrailerUxCameraDelegate *m_cameraDelegate = nil;
    dispatch_queue_t m_cameraQueue = nullptr;

    // Input tap (torn down synchronously in stop() before the guard
    // flips, so its callback can use `this` directly).
    NSThread *m_tapThread = nil;
    CFRunLoopRef m_tapRunLoop = nullptr;
    std::mutex m_tapMutex;
    QJsonArray m_tapMouseSamples;
    qint64 m_tapLastSampleMs = 0;
    qint64 m_tapBatchStartMs = 0;
    int m_tapScrollEvents = 0;
    qint64 m_tapScrollDeltaX = 0;
    qint64 m_tapScrollDeltaY = 0;
};

namespace {

CGEventRef tapCallback(CGEventTapProxy /*proxy*/, CGEventType type, CGEventRef event,
                       void *userInfo) {
    auto *capture = static_cast<MacUxPlatformCapture *>(userInfo);
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        // The WindowServer disables slow/suspended taps; re-arm so a
        // momentary stall doesn't silently end input observation.
        if (capture->m_tap) {
            CGEventTapEnable(capture->m_tap, true);
        }
        return event;
    }
    capture->handleTapEvent(type, event);
    return event;
}

} // namespace

// ---- lifecycle ------------------------------------------------------

void MacUxPlatformCapture::start() {
    m_screenQueue = dispatch_queue_create("org.trailer.ux.screen", DISPATCH_QUEUE_SERIAL);
    m_cameraQueue = dispatch_queue_create("org.trailer.ux.camera", DISPATCH_QUEUE_SERIAL);

    NSString *bundleId = [[NSBundle mainBundle] bundleIdentifier];
    emitEvent(QStringLiteral("platform_capture_started"),
              QJsonObject{{QStringLiteral("trailer_bundle_id"),
                           bundleId ? QString::fromNSString(bundleId)
                                    : QStringLiteral("(unbundled)")},
                          {QStringLiteral("trailer_pid"), static_cast<qint64>(getpid())},
                          {QStringLiteral("preview_bundle_id"), m_ctx.previewBundleId}});

    startForegroundWatcher();
    startScreenCapture();
    startCamera();
    startInputTap();
}

void MacUxPlatformCapture::stop() {
    if (m_stopping.exchange(true)) {
        return;
    }
    flushTapBatches();
    stopInputTap();

    if (m_activationObserver) {
        [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:m_activationObserver];
        m_activationObserver = nil;
    }

    stopScreenCapture();
    // Drain the camera queue so a still-running cameraSessionBody()
    // can't be assigning m_cameraSession/m_cameraOutput while
    // stopCamera() reads them. Any body dispatched after this point
    // exits on the m_stopping flag.
    if (m_cameraQueue) {
        dispatch_sync(m_cameraQueue, ^{});
    }
    stopCamera();
    emitEvent(QStringLiteral("platform_capture_stopped"));

    // Final act: neuter every still-pending asynchronous callback.
    // Must happen *after* the orderly teardown above — the camera /
    // screen delegates emit their closing events during it.
    std::lock_guard<std::mutex> lock(m_guard->mutex);
    m_guard->alive = false;
}

// ---- foreground tracking --------------------------------------------

void MacUxPlatformCapture::updateFrontmost(NSRunningApplication *app, bool initial) {
    FrontKind kind = FrontKind::Other;
    if (app) {
        if (app.processIdentifier == getpid()) {
            kind = FrontKind::Trailer;
        } else if (app.bundleIdentifier &&
                   QString::fromNSString(app.bundleIdentifier) == m_ctx.previewBundleId) {
            kind = FrontKind::Preview;
        }
    }
    const int previous = m_front.exchange(static_cast<int>(kind));
    if (previous != static_cast<int>(kind)) {
        // Close any open input batch at the boundary so a gesture
        // never spans a not-recorded gap.
        flushTapBatches();
    }
    if (initial || previous != static_cast<int>(kind)) {
        emitEvent(QStringLiteral("app_activated"),
                  QJsonObject{{QStringLiteral("bundle_id"),
                               app.bundleIdentifier ? QString::fromNSString(app.bundleIdentifier)
                                                    : QString()},
                              {QStringLiteral("name"),
                               app.localizedName ? QString::fromNSString(app.localizedName)
                                                 : QString()},
                              {QStringLiteral("pid"), static_cast<qint64>(app.processIdentifier)},
                              {QStringLiteral("kind"), frontKindName(static_cast<int>(kind))}});
    }
}

void MacUxPlatformCapture::startForegroundWatcher() {
    updateFrontmost([NSWorkspace sharedWorkspace].frontmostApplication, /*initial=*/true);
    MacUxPlatformCapture *capture = this;
    auto guard = m_guard;
    m_activationObserver = [[[NSWorkspace sharedWorkspace] notificationCenter]
        addObserverForName:NSWorkspaceDidActivateApplicationNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *note) {
                    std::lock_guard<std::mutex> lock(guard->mutex);
                    if (!guard->alive) {
                        return;
                    }
                    NSRunningApplication *app = note.userInfo[NSWorkspaceApplicationKey];
                    capture->updateFrontmost(app, /*initial=*/false);
                }];
}

// ---- screen capture ---------------------------------------------------

void MacUxPlatformCapture::startScreenCapture() {
#if TRAILER_HAS_SCREENCAPTUREKIT
    if (@available(macOS 12.3, *)) {
        if (!CGPreflightScreenCaptureAccess()) {
            emitEvent(QStringLiteral("screen_recording_permission"),
                      QJsonObject{{QStringLiteral("granted"), false},
                                  {QStringLiteral("requesting"), true}});
            // Pops the system prompt (first time only). The grant
            // usually applies to *future* launches, so this session
            // may produce no frames; the content fetch below reports
            // that as screen_capture_failed.
            CGRequestScreenCaptureAccess();
        }

        MacUxPlatformCapture *capture = this;
        auto guard = m_guard;
        [SCShareableContent getShareableContentWithCompletionHandler:^(
                                SCShareableContent *content, NSError *error) {
            std::lock_guard<std::mutex> lock(guard->mutex);
            if (!guard->alive) {
                return;
            }
            capture->onShareableContent(content, error);
        }];
        return;
    }
#endif
    emitEvent(QStringLiteral("screen_capture_unsupported"),
              QJsonObject{{QStringLiteral("reason"),
                           QStringLiteral("ScreenCaptureKit requires macOS 12.3 or newer")}});
}

#if TRAILER_HAS_SCREENCAPTUREKIT
void MacUxPlatformCapture::onShareableContent(SCShareableContent *content, NSError *error) {
    if (error || content.displays.count == 0) {
        emitEvent(QStringLiteral("screen_capture_failed"),
                  QJsonObject{{QStringLiteral("error"),
                               error ? QString::fromNSString(error.localizedDescription)
                                     : QStringLiteral("no shareable displays")},
                              {QStringLiteral("hint"),
                               QStringLiteral("grant Screen Recording to Trailer in System "
                                              "Settings → Privacy & Security, then relaunch")}});
        return;
    }

    // MVP scope: capture the main display and gate frame *retention*
    // on the frontmost app. Compared with window-filtered capture this
    // is far more robust (no re-filtering when windows are created or
    // closed) at the cost of including third-party windows that
    // overlap the screen while Trailer/Preview is frontmost — fine for
    // a private developer recorder; documented in docs/ux-recorder.md.
    SCDisplay *display = content.displays.firstObject;
    const CGDirectDisplayID mainId = CGMainDisplayID();
    for (SCDisplay *candidate in content.displays) {
        if (candidate.displayID == mainId) {
            display = candidate;
            break;
        }
    }

    CGFloat backingScale = 2.0;
    for (NSScreen *screen in [NSScreen screens]) {
        NSNumber *screenNumber = screen.deviceDescription[@"NSScreenNumber"];
        if (screenNumber &&
            static_cast<CGDirectDisplayID>(screenNumber.unsignedIntValue) ==
                display.displayID) {
            backingScale = screen.backingScaleFactor;
            break;
        }
    }
    CGFloat width = display.width * backingScale;
    CGFloat height = display.height * backingScale;
    const CGFloat longSide = MAX(width, height);
    if (longSide > kScreenMaxLongSidePx) {
        const CGFloat ratio = kScreenMaxLongSidePx / longSide;
        width *= ratio;
        height *= ratio;
    }

    SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display
                                                      excludingWindows:@[]];
    SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
    config.width = static_cast<size_t>(width);
    config.height = static_cast<size_t>(height);
    config.minimumFrameInterval = CMTimeMake(1, kScreenFps);
    config.pixelFormat = kCVPixelFormatType_32BGRA;
    config.showsCursor = YES;
    config.queueDepth = 5;

    TrailerUxScreenDelegate *delegate = [[TrailerUxScreenDelegate alloc] init];
    delegate->_owner = this;
    delegate->_guard = m_guard;
    SCStream *stream = [[SCStream alloc] initWithFilter:filter
                                          configuration:config
                                               delegate:delegate];
    NSError *outputError = nil;
    if (![stream addStreamOutput:delegate
                            type:SCStreamOutputTypeScreen
              sampleHandlerQueue:m_screenQueue
                           error:&outputError]) {
        emitEvent(QStringLiteral("screen_capture_failed"),
                  QJsonObject{{QStringLiteral("error"),
                               QString::fromNSString(outputError.localizedDescription)}});
        return;
    }
    m_screenDelegate = delegate;
    m_stream = stream;

    MacUxPlatformCapture *capture = this;
    auto guard = m_guard;
    const int outWidth = static_cast<int>(width);
    const int outHeight = static_cast<int>(height);
    const qint64 displayId = display.displayID;
    [stream startCaptureWithCompletionHandler:^(NSError *startError) {
        std::lock_guard<std::mutex> lock(guard->mutex);
        if (!guard->alive) {
            return;
        }
        if (startError) {
            capture->emitEvent(
                QStringLiteral("screen_capture_failed"),
                QJsonObject{{QStringLiteral("error"),
                             QString::fromNSString(startError.localizedDescription)}});
            return;
        }
        capture->emitEvent(QStringLiteral("screen_capture_started"),
                           QJsonObject{{QStringLiteral("display_id"), displayId},
                                       {QStringLiteral("width"), outWidth},
                                       {QStringLiteral("height"), outHeight},
                                       {QStringLiteral("fps"), kScreenFps},
                                       {QStringLiteral("format"),
                                        QStringLiteral("jpeg_frames")}});
    }];
}
#endif

void MacUxPlatformCapture::stopScreenCapture() {
#if TRAILER_HAS_SCREENCAPTUREKIT
    if (@available(macOS 12.3, *)) {
        SCStream *stream = static_cast<SCStream *>(m_stream);
        if (stream) {
            dispatch_semaphore_t done = dispatch_semaphore_create(0);
            [stream stopCaptureWithCompletionHandler:^(NSError *) {
                dispatch_semaphore_signal(done);
            }];
            dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(2 * NSEC_PER_SEC)));
        }
    }
#endif
    m_stream = nil;
    m_screenDelegate = nil;
}

QString MacUxPlatformCapture::writeJpegFrame(CVImageBufferRef pixelBuffer, quint64 sequence,
                                             qint64 elapsed) {
    if (CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly) !=
        kCVReturnSuccess) {
        return {};
    }
    QString relative;
    void *base = CVPixelBufferGetBaseAddress(pixelBuffer);
    const size_t width = CVPixelBufferGetWidth(pixelBuffer);
    const size_t height = CVPixelBufferGetHeight(pixelBuffer);
    const size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    // BGRA little-endian, alpha ignored — matches the stream's
    // kCVPixelFormatType_32BGRA output. (CGImageByteOrderInfo and
    // CGImageAlphaInfo are distinct enums; C++ refuses to | them
    // without going through the underlying integer type.)
    const uint32_t bitmapInfo = static_cast<uint32_t>(kCGBitmapByteOrder32Little) |
                                static_cast<uint32_t>(kCGImageAlphaNoneSkipFirst);
    CGContextRef context =
        CGBitmapContextCreate(base, width, height, 8, bytesPerRow, colorSpace, bitmapInfo);
    if (context) {
        CGImageRef image = CGBitmapContextCreateImage(context);
        if (image) {
            relative = QStringLiteral("screen/frame-%1-%2.jpg")
                           .arg(sequence, 6, 10, QLatin1Char('0'))
                           .arg(elapsed, 9, 10, QLatin1Char('0'));
            const QString absolute = QDir(m_ctx.sessionDir).filePath(relative);
            NSURL *url = [NSURL fileURLWithPath:absolute.toNSString()];
            CGImageDestinationRef destination = CGImageDestinationCreateWithURL(
                (__bridge CFURLRef)url, CFSTR("public.jpeg"), 1, nullptr);
            if (destination) {
                NSDictionary *properties = @{
                    (__bridge NSString *)kCGImageDestinationLossyCompressionQuality :
                        @(kScreenJpegQuality)
                };
                CGImageDestinationAddImage(destination, image,
                                           (__bridge CFDictionaryRef)properties);
                if (!CGImageDestinationFinalize(destination)) {
                    relative.clear();
                }
                CFRelease(destination);
            } else {
                relative.clear();
            }
            CGImageRelease(image);
        }
        CGContextRelease(context);
    }
    CGColorSpaceRelease(colorSpace);
    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    return relative;
}

void MacUxPlatformCapture::handleScreenFrame(CMSampleBufferRef sampleBuffer) {
    if (m_stopping.load() || !retainNow()) {
        return;
    }
#if TRAILER_HAS_SCREENCAPTUREKIT
    if (@available(macOS 12.3, *)) {
        // Skip idle/incomplete frames (SCK ships frame status in the
        // sample attachments).
        CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
        if (attachments && CFArrayGetCount(attachments) > 0) {
            CFDictionaryRef info =
                static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
            CFTypeRef statusValue =
                CFDictionaryGetValue(info, (__bridge CFStringRef)SCStreamFrameInfoStatus);
            if (statusValue) {
                int status = 0;
                CFNumberGetValue(static_cast<CFNumberRef>(statusValue), kCFNumberIntType,
                                 &status);
                if (status != SCFrameStatusComplete) {
                    return;
                }
            }
        }
    }
#endif
    CVImageBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pixelBuffer) {
        return;
    }
    const qint64 elapsed = m_ctx.elapsedMs ? m_ctx.elapsedMs() : 0;
    const quint64 sequence = ++m_frameSequence;
    const QString relative = writeJpegFrame(pixelBuffer, sequence, elapsed);
    if (!relative.isEmpty()) {
        emitEvent(QStringLiteral("screen_frame"),
                  QJsonObject{{QStringLiteral("file"), relative},
                              {QStringLiteral("frontmost"), frontKindName(m_front.load())}});
    }
}

// ---- camera ------------------------------------------------------------

void MacUxPlatformCapture::startCamera() {
    const AVAuthorizationStatus status =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    switch (status) {
    case AVAuthorizationStatusAuthorized:
        startCameraSession();
        break;
    case AVAuthorizationStatusNotDetermined: {
        emitEvent(QStringLiteral("camera_permission_requested"));
        MacUxPlatformCapture *capture = this;
        auto guard = m_guard;
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL granted) {
                                     std::lock_guard<std::mutex> lock(guard->mutex);
                                     if (!guard->alive) {
                                         return;
                                     }
                                     if (granted) {
                                         capture->startCameraSession();
                                     } else {
                                         capture->emitEvent(
                                             QStringLiteral("camera_permission_denied"));
                                     }
                                 }];
        break;
    }
    default:
        emitEvent(QStringLiteral("camera_permission_denied"),
                  QJsonObject{{QStringLiteral("status"), static_cast<int>(status)}});
        break;
    }
}

void MacUxPlatformCapture::startCameraSession() {
    MacUxPlatformCapture *capture = this;
    auto guard = m_guard;
    dispatch_async(m_cameraQueue, ^{
        std::lock_guard<std::mutex> lock(guard->mutex);
        if (!guard->alive) {
            return;
        }
        capture->cameraSessionBody();
    });
}

void MacUxPlatformCapture::cameraSessionBody() {
    if (m_stopping.load()) {
        return;
    }
    AVCaptureDevice *device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (!device) {
        emitEvent(QStringLiteral("camera_unavailable"),
                  QJsonObject{{QStringLiteral("reason"),
                               QStringLiteral("no default video device")}});
        return;
    }
    NSError *error = nil;
    AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:device
                                                                        error:&error];
    AVCaptureSession *session = [[AVCaptureSession alloc] init];
    // Face-cam resolution: 640×480 keeps a full session in the tens
    // of megabytes and is plenty for reading reactions.
    if ([session canSetSessionPreset:AVCaptureSessionPreset640x480]) {
        session.sessionPreset = AVCaptureSessionPreset640x480;
    }
    AVCaptureMovieFileOutput *output = [[AVCaptureMovieFileOutput alloc] init];
    if (!input || ![session canAddInput:input] || ![session canAddOutput:output]) {
        emitEvent(QStringLiteral("camera_unavailable"),
                  QJsonObject{{QStringLiteral("reason"),
                               error ? QString::fromNSString(error.localizedDescription)
                                     : QStringLiteral("could not assemble capture session")}});
        return;
    }
    [session addInput:input];
    [session addOutput:output];

    TrailerUxCameraDelegate *delegate = [[TrailerUxCameraDelegate alloc] init];
    delegate->_owner = this;
    delegate->_guard = m_guard;
    delegate.finishedSemaphore = dispatch_semaphore_create(0);

    m_cameraSession = session;
    m_cameraOutput = output;
    m_cameraDelegate = delegate;

    [session startRunning];
    const QString relative = QStringLiteral("camera/camera-000.mov");
    const QString absolute = QDir(m_ctx.sessionDir).filePath(relative);
    [output startRecordingToOutputFileURL:[NSURL fileURLWithPath:absolute.toNSString()]
                        recordingDelegate:delegate];
}

void MacUxPlatformCapture::stopCamera() {
    AVCaptureMovieFileOutput *output = m_cameraOutput;
    AVCaptureSession *session = m_cameraSession;
    TrailerUxCameraDelegate *delegate = m_cameraDelegate;
    if (output && output.isRecording) {
        [output stopRecording];
        if (delegate && delegate.finishedSemaphore) {
            // Bounded wait for the moov atom so the .mov is playable;
            // 3 s is generous for a 480p stream.
            dispatch_semaphore_wait(delegate.finishedSemaphore,
                                    dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(3 * NSEC_PER_SEC)));
        }
    }
    if (session && session.isRunning) {
        [session stopRunning];
    }
    m_cameraSession = nil;
    m_cameraOutput = nil;
    m_cameraDelegate = nil;
}

// ---- global input --------------------------------------------------------

void MacUxPlatformCapture::startInputTap() {
    if (IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) != kIOHIDAccessTypeGranted) {
        emitEvent(QStringLiteral("input_monitoring_permission"),
                  QJsonObject{{QStringLiteral("granted"), false},
                              {QStringLiteral("requesting"), true}});
        // Shows the Input Monitoring prompt; like Screen Recording,
        // the grant typically applies to future launches. The tap
        // attempt below still runs — it fails cleanly when denied.
        IOHIDRequestAccess(kIOHIDRequestTypeListenEvent);
    }

    // The tap thread is joined in stopInputTap() before the life
    // guard flips, so the block may capture `this` directly.
    MacUxPlatformCapture *capture = this;
    m_tapThread = [[NSThread alloc] initWithBlock:^{
        const CGEventMask mask =
            CGEventMaskBit(kCGEventMouseMoved) | CGEventMaskBit(kCGEventLeftMouseDragged) |
            CGEventMaskBit(kCGEventRightMouseDragged) |
            CGEventMaskBit(kCGEventOtherMouseDragged) | CGEventMaskBit(kCGEventLeftMouseDown) |
            CGEventMaskBit(kCGEventLeftMouseUp) | CGEventMaskBit(kCGEventRightMouseDown) |
            CGEventMaskBit(kCGEventRightMouseUp) | CGEventMaskBit(kCGEventOtherMouseDown) |
            CGEventMaskBit(kCGEventOtherMouseUp) | CGEventMaskBit(kCGEventScrollWheel) |
            CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) |
            CGEventMaskBit(kCGEventFlagsChanged);
        CFMachPortRef tap =
            CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
                             kCGEventTapOptionListenOnly, mask, tapCallback, capture);
        if (!tap) {
            capture->emitEvent(
                QStringLiteral("input_tap_unavailable"),
                QJsonObject{{QStringLiteral("hint"),
                             QStringLiteral("grant Input Monitoring (or Accessibility) to "
                                            "Trailer in System Settings → Privacy & Security "
                                            "to record input while Preview is frontmost")}});
            return;
        }
        capture->m_tap = tap;
        CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
        capture->m_tapRunLoop = CFRunLoopGetCurrent();
        CFRunLoopAddSource(capture->m_tapRunLoop, source, kCFRunLoopCommonModes);
        CGEventTapEnable(tap, true);
        capture->emitEvent(QStringLiteral("input_tap_started"));

        // stop() can race a just-started thread: CFRunLoopStop on a
        // loop that is not running yet is a no-op, which would leave
        // CFRunLoopRun() blocked forever. Re-check the stop flag at
        // the last possible moment instead.
        if (!capture->m_stopping.load()) {
            CFRunLoopRun(); // until stopInputTap() calls CFRunLoopStop
        }

        CGEventTapEnable(tap, false);
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
        CFRelease(source);
        CFMachPortInvalidate(tap);
        capture->m_tap = nullptr;
        CFRelease(tap);
        capture->m_tapRunLoop = nullptr;
    }];
    m_tapThread.name = @"trailer-ux-input-tap";
    [m_tapThread start];
}

void MacUxPlatformCapture::stopInputTap() {
    if (m_tapRunLoop) {
        CFRunLoopStop(m_tapRunLoop);
    }
    if (m_tapThread) {
        // NSThread has no join; poll briefly (≤1 s) so tap teardown
        // finishes before the recorder closes the event stream.
        for (int i = 0; i < 100 && !m_tapThread.isFinished; ++i) {
            usleep(10 * 1000);
        }
        m_tapThread = nil;
    }
}

void MacUxPlatformCapture::sampleTapMouse(CGEventRef event, bool dragging) {
    const qint64 now = m_ctx.elapsedMs ? m_ctx.elapsedMs() : 0;
    bool flushDue = false;
    {
        std::lock_guard<std::mutex> lock(m_tapMutex);
        if (!m_tapMouseSamples.isEmpty() &&
            now - m_tapLastSampleMs < kTapMouseSampleIntervalMs) {
            return;
        }
        m_tapLastSampleMs = now;
        if (m_tapMouseSamples.isEmpty()) {
            m_tapBatchStartMs = now;
        }
        const CGPoint location = CGEventGetLocation(event);
        m_tapMouseSamples.append(
            QJsonArray{qRound(location.x), qRound(location.y), dragging ? 1 : 0});
        flushDue = m_tapMouseSamples.size() >= kTapMousePathMaxSamples ||
                   now - m_tapBatchStartMs >= kTapBatchMaxAgeMs;
    }
    if (flushDue) {
        flushTapBatches();
    }
}

void MacUxPlatformCapture::flushTapBatches() {
    QJsonArray samples;
    int scrollEvents = 0;
    qint64 scrollX = 0;
    qint64 scrollY = 0;
    {
        std::lock_guard<std::mutex> lock(m_tapMutex);
        std::swap(samples, m_tapMouseSamples);
        scrollEvents = m_tapScrollEvents;
        scrollX = m_tapScrollDeltaX;
        scrollY = m_tapScrollDeltaY;
        m_tapScrollEvents = 0;
        m_tapScrollDeltaX = 0;
        m_tapScrollDeltaY = 0;
    }
    const QString front = frontKindName(m_front.load());
    if (!samples.isEmpty()) {
        emitEvent(QStringLiteral("mouse_path"),
                  QJsonObject{{QStringLiteral("sample_count"), samples.size()},
                              {QStringLiteral("samples"), samples},
                              {QStringLiteral("frontmost"), front}});
    }
    if (scrollEvents > 0) {
        emitEvent(QStringLiteral("wheel"),
                  QJsonObject{{QStringLiteral("events"), scrollEvents},
                              {QStringLiteral("delta_x"), scrollX},
                              {QStringLiteral("delta_y"), scrollY},
                              {QStringLiteral("frontmost"), front}});
    }
}

void MacUxPlatformCapture::handleTapEvent(CGEventType type, CGEventRef event) {
    if (m_stopping.load()) {
        return;
    }
    const int front = m_front.load();
    if (m_paused.load() || front == static_cast<int>(FrontKind::Other)) {
        // Never retain input while unrelated applications are
        // frontmost.
        return;
    }

    switch (type) {
    case kCGEventMouseMoved:
        sampleTapMouse(event, /*dragging=*/false);
        break;
    case kCGEventLeftMouseDragged:
    case kCGEventRightMouseDragged:
    case kCGEventOtherMouseDragged:
        sampleTapMouse(event, /*dragging=*/true);
        break;
    case kCGEventLeftMouseDown:
    case kCGEventRightMouseDown:
    case kCGEventOtherMouseDown:
    case kCGEventLeftMouseUp:
    case kCGEventRightMouseUp:
    case kCGEventOtherMouseUp: {
        flushTapBatches();
        const bool down = (type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown ||
                           type == kCGEventOtherMouseDown);
        const CGPoint location = CGEventGetLocation(event);
        emitEvent(QStringLiteral("mouse_button"),
                  QJsonObject{{QStringLiteral("action"),
                               down ? QStringLiteral("press") : QStringLiteral("release")},
                              {QStringLiteral("button"), tapButtonName(type, event)},
                              {QStringLiteral("global"),
                               QJsonArray{qRound(location.x), qRound(location.y)}},
                              {QStringLiteral("modifiers"),
                               tapModifierNames(CGEventGetFlags(event))},
                              {QStringLiteral("frontmost"), frontKindName(front)}});
        break;
    }
    case kCGEventScrollWheel: {
        std::lock_guard<std::mutex> lock(m_tapMutex);
        m_tapScrollEvents++;
        m_tapScrollDeltaY += CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1);
        m_tapScrollDeltaX += CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2);
        break;
    }
    case kCGEventKeyDown:
    case kCGEventKeyUp: {
        const int64_t keyCode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
        const CGEventFlags flags = CGEventGetFlags(event);
        const bool down = type == kCGEventKeyDown;

        // Global frustration-marker hotkey while *Preview* is
        // frontmost (inside Trailer the in-app QAction owns the
        // chord; gating on Preview avoids double markers).
        if (down && keyCode == kMarkerHotkeyKeyCode && (flags & kCGEventFlagMaskCommand) &&
            (flags & kCGEventFlagMaskShift) &&
            front == static_cast<int>(FrontKind::Preview) && m_ctx.frustrationHotkey) {
            m_ctx.frustrationHotkey();
        }

        QJsonObject data{{QStringLiteral("action"),
                          down ? QStringLiteral("press") : QStringLiteral("release")},
                         {QStringLiteral("key_code"), static_cast<qint64>(keyCode)},
                         {QStringLiteral("modifiers"), tapModifierNames(flags)},
                         {QStringLiteral("auto_repeat"),
                          CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat) != 0},
                         {QStringLiteral("frontmost"), frontKindName(front)}};
        if (m_ctx.captureKeyText) {
            UniChar characters[8];
            UniCharCount length = 0;
            CGEventKeyboardGetUnicodeString(event, 8, &length, characters);
            if (length > 0) {
                const QString text = QString::fromUtf16(
                    reinterpret_cast<const char16_t *>(characters), static_cast<int>(length));
                if (!text.isEmpty() && text.at(0).isPrint()) {
                    data.insert(QStringLiteral("text"), text);
                }
            }
        }
        emitEvent(QStringLiteral("key"), data);
        break;
    }
    case kCGEventFlagsChanged:
        emitEvent(QStringLiteral("modifiers_changed"),
                  QJsonObject{{QStringLiteral("modifiers"),
                               tapModifierNames(CGEventGetFlags(event))},
                              {QStringLiteral("frontmost"), frontKindName(front)}});
        break;
    default:
        break;
    }
}

std::unique_ptr<UxPlatformCapture> createUxPlatformCapture(UxCaptureContext context) {
    return std::make_unique<MacUxPlatformCapture>(std::move(context));
}

} // namespace trailer

// ---- ObjC delegates -----------------------------------------------------

#if TRAILER_HAS_SCREENCAPTUREKIT
@implementation TrailerUxScreenDelegate

- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type {
    if (type != SCStreamOutputTypeScreen) {
        return;
    }
    std::lock_guard<std::mutex> lock(_guard->mutex);
    if (!_guard->alive || !_owner) {
        return;
    }
    _owner->handleScreenFrame(sampleBuffer);
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
    std::lock_guard<std::mutex> lock(_guard->mutex);
    if (!_guard->alive || !_owner) {
        return;
    }
    _owner->emitEvent(QStringLiteral("screen_capture_stopped"),
                      QJsonObject{{QStringLiteral("error"),
                                   error ? QString::fromNSString(error.localizedDescription)
                                         : QString()}});
}

@end
#endif

@implementation TrailerUxCameraDelegate

- (void)captureOutput:(AVCaptureFileOutput *)output
    didStartRecordingToOutputFileAtURL:(NSURL *)fileURL
                       fromConnections:(NSArray<AVCaptureConnection *> *)connections {
    std::lock_guard<std::mutex> lock(_guard->mutex);
    if (!_guard->alive || !_owner) {
        return;
    }
    _owner->emitEvent(QStringLiteral("camera_started"),
                      QJsonObject{{QStringLiteral("file"),
                                   QStringLiteral("camera/camera-000.mov")}});
}

- (void)captureOutput:(AVCaptureFileOutput *)output
    didFinishRecordingToOutputFileAtURL:(NSURL *)fileURL
                        fromConnections:(NSArray<AVCaptureConnection *> *)connections
                                  error:(NSError *)error {
    {
        std::lock_guard<std::mutex> lock(_guard->mutex);
        if (_guard->alive && _owner) {
            _owner->emitEvent(
                QStringLiteral("camera_stopped"),
                QJsonObject{{QStringLiteral("file"), QStringLiteral("camera/camera-000.mov")},
                            {QStringLiteral("error"),
                             error ? QString::fromNSString(error.localizedDescription)
                                   : QString()}});
        }
    }
    if (self.finishedSemaphore) {
        dispatch_semaphore_signal(self.finishedSemaphore);
    }
}

@end
