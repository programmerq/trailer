#include "uxrecord/UxPlatformCapture.h"

namespace trailer {

namespace {

// Recorder-enabled builds on platforms without a capture backend yet
// (Linux, Windows). The platform-neutral recorder — Qt event stream,
// semantic events, markers, session report — works fully; screen,
// camera, foreground-app tracking, and global input observation
// announce themselves as unavailable in events.jsonl so an analysis
// pass knows why those artefacts are missing. A future Windows/Linux
// backend replaces this file in CMakeLists.txt without touching any
// Trailer-side recorder logic.
class StubUxPlatformCapture : public UxPlatformCapture {
  public:
    explicit StubUxPlatformCapture(UxCaptureContext context) : m_context(std::move(context)) {}

    bool isSupported() const override { return false; }

    void start() override {
        if (m_context.emitEvent) {
            m_context.emitEvent(
                QStringLiteral("platform_capture_unavailable"),
                QJsonObject{{QStringLiteral("reason"),
                             QStringLiteral("no capture backend for this platform; "
                                            "screen/camera/global-input are not recorded")}});
        }
    }

    void stop() override {}

    void setPaused(bool paused) override { m_paused = paused; }
    bool isPaused() const override { return m_paused; }

  private:
    UxCaptureContext m_context;
    bool m_paused = false;
};

} // namespace

std::unique_ptr<UxPlatformCapture> createUxPlatformCapture(UxCaptureContext context) {
    return std::make_unique<StubUxPlatformCapture>(std::move(context));
}

} // namespace trailer
