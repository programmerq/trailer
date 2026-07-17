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
