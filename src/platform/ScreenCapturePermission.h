#pragma once

#include <QString>

class QWidget;

namespace trailer {

class Settings;

// Settings key (a leaf in the [first_use] bool bag) recording that the
// one-time "Screen Recording" pre-permission explainer has been shown.
// Kept next to the gate so tests and the macOS dialog agree on the name.
inline constexpr char kScreenCaptureExplainerKey[] = "screen_capture_explainer";

// The live TCC permission state for screen capture, as seen by the provider.
//   Granted      — capture will succeed; go straight to the OS selection UI.
//   Denied       — capture will silently produce nothing.
//   Undetermined — never asked (or can't tell apart from denied); routes
//                  through the OS request, which prompts if truly undetermined
//                  and is a silent no-op if actually denied.
//
// Note: CGPreflightScreenCaptureAccess() can only authoritatively report
// Granted; every not-granted case is reported as Undetermined and arbitrated
// by the request path (see queryScreenCapturePermissionState). The Denied
// enumerator is retained for interface completeness and unit-test coverage of
// the decision table — the macOS provider never manufactures it from a sticky
// bool (an earlier design did, and it dead-ended the app after a TCC reset).
enum class ScreenCapturePermissionState { Granted, Denied, Undetermined };

// The action a call site should take, derived purely from the permission
// state and whether the first-run explainer has been acknowledged.
//   Proceed            — run the capture now.
//   ShowExplainerFirst — show the one-time pre-permission explainer, then
//                        (if the user continues) drive the OS permission request.
//   RequestAccess      — drive the OS permission request (prompts when truly
//                        undetermined, silent no-op when denied), then capture
//                        only if it reports success.
//   DegradeDenied      — do NOT launch the OS UI; surface the recoverable
//                        "needs permission" degrade path instead.
enum class ScreenCaptureFlowAction { Proceed, ShowExplainerFirst, RequestAccess, DegradeDenied };

// Pure, platform-agnostic first-run gate for the Screen Recording
// pre-permission explainer. Pure query with NO mutation: returns true while
// the explainer has not yet been acknowledged (meaning "show the explainer")
// and false once it has. Because this does not persist anything, cancelling
// the explainer leaves the flag unset so it re-appears next time — the flag is
// only burned by acknowledgeScreenCaptureExplainer when the user proceeds.
// No UI, no platform calls — testable off-Mac.
bool shouldShowScreenCaptureExplainer(Settings &settings);

// Record that the user proceeded past the explainer (chose Continue), setting
// the "shown" flag in Settings and persisting it (Settings::save) so the
// explainer stays suppressed across launches. Call this ONLY when the user
// continues into the capture — never on cancel. Pure state mutation, no UI —
// testable off-Mac.
void acknowledgeScreenCaptureExplainer(Settings &settings);

// Pure, platform-agnostic decision table mapping (permission state,
// explainer-acknowledged) → the action a call site should take. NO #ifdef —
// fully unit-testable off-Mac. Implements exactly:
//   Granted                         -> Proceed
//   Denied                          -> DegradeDenied
//   Undetermined && !acknowledged   -> ShowExplainerFirst
//   Undetermined &&  acknowledged   -> RequestAccess
ScreenCaptureFlowAction decideScreenCaptureFlow(ScreenCapturePermissionState state,
                                                bool explainerAcknowledged);

// The deep link to the macOS Screen Recording settings pane, and a best-effort
// opener over QDesktopServices (returns openUrl's bool). Exposed for testing
// and shared by both degrade sites so the string lives in one place.
QString screenRecordingSettingsUrlString();
bool openScreenRecordingSettings();

// The graceful-degrade user message shown when capture is not granted. Honest
// whether the OS was just prompted or the permission is actually denied, and
// includes the relaunch nuance. Shared by both call sites (status bar + modal)
// and asserted by tests so the wording stays in one place.
QString screenRecordingNeededMessage();

// Query the live native screen-capture permission state (no persisted state,
// no side effects — a bare read of live TCC).
//   macOS: CGPreflightScreenCaptureAccess() == true -> Granted (authoritative:
//     reflects live TCC, including after `tccutil reset`). Otherwise returns
//     Undetermined — CGPreflight cannot distinguish denied from undetermined,
//     so the request path (requestScreenCaptureAccess) arbitrates.
//   non-macOS: always Granted — that path uses QScreen::grabWindow and needs
//     no TCC. No CoreGraphics reference is compiled off-Mac.
ScreenCapturePermissionState queryScreenCapturePermissionState();

// Drives the OS permission request for screen capture.
//   macOS: CGRequestScreenCaptureAccess() — prompts the user when the
//     permission is undetermined (re-registering the app in TCC, e.g. after a
//     `tccutil reset`), a silent no-op returning false when actually denied,
//     and returns true when already granted. No crosshair is ever shown by
//     this call itself.
//   non-macOS: returns true (no TCC to request).
bool requestScreenCaptureAccess();

#ifdef Q_OS_MACOS
// macOS-only: if the explainer has not been shown before (per the pure gate
// above), present a one-time modal dialog explaining that macOS will prompt
// for Screen Recording permission because the screenshot feature captures the
// screen, and that it can be granted in System Settings > Privacy & Security >
// Screen Recording. Returns true if the caller should proceed with the
// capture, false if the user cancelled from the explainer. When the explainer
// has already been shown, returns true immediately with no UI.
bool maybeShowScreenCaptureExplainer(Settings &settings, QWidget *parent);
#endif

} // namespace trailer
