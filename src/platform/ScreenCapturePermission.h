#pragma once

#include <QString>

#ifdef TRAILER_UX_RECORDER
#include <functional>
#endif

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
// state. The stills capture flow no longer shows a pre-permission explainer
// (retired per the capture-permission-preflight ADR / owner decision
// 2026-07-17), so the decision collapses to two outcomes.
//   Proceed       — run the capture now (permission is Granted).
//   RequestAccess — drive the OS permission request (prompts when truly
//                   undetermined, silent no-op when denied), then capture only
//                   if it reports success. Every not-granted state maps here;
//                   CGRequestScreenCaptureAccess arbitrates prompt-vs-degrade.
enum class ScreenCaptureFlowAction { Proceed, RequestAccess };

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

// Pure, platform-agnostic decision table mapping permission state → the action
// a call site should take. NO #ifdef — fully unit-testable off-Mac. Implements
// exactly:
//   Granted      -> Proceed
//   Denied       -> RequestAccess
//   Undetermined -> RequestAccess
// Every not-granted state is arbitrated by requestScreenCaptureAccess()
// (CGRequestScreenCaptureAccess — prompts if undetermined, silent no-op if
// denied), so no separate explainer/degrade branch is needed at decision time.
ScreenCaptureFlowAction decideScreenCaptureFlow(ScreenCapturePermissionState state);

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

#ifdef TRAILER_UX_RECORDER
// Recorder builds only (ADR 0014). In these builds Mechanism B — the recorder's
// live macOS Screen Recording TCC gate in src/uxrecord/ — is the authoritative
// Screen-Recording surface, and this first-use explainer (Mechanism A) must
// defer to it so the two flows never double-prompt for the same permission.
// Rather than have src/platform/ take a hard compile/link dependency on
// src/uxrecord/, the recorder INJECTS a "is Screen Recording already granted?"
// probe here at startup (Application wires it to trailer::uxScreenRecordingGranted()).
// When the probe is set and reports granted, shouldShowScreenCaptureExplainer()
// suppresses the explainer. The seam also makes the deference deterministically
// testable without a live TCC state. Pass an empty std::function to clear it.
// Entirely absent from default builds, which keep A standalone with no new
// dependency (ADR 0014 consequences / G14.4).
void setScreenRecordingGrantedProbe(std::function<bool()> probe);
#endif

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
