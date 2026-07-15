#pragma once

#include <QString>

class QWidget;

namespace trailer {

class Settings;

// Settings key (a leaf in the [first_use] bool bag) recording that the
// one-time "Screen Recording" pre-permission explainer has been shown.
// Kept next to the gate so tests and the macOS dialog agree on the name.
inline constexpr char kScreenCaptureExplainerKey[] = "screen_capture_explainer";

// Pure, platform-agnostic first-run gate for the Screen Recording
// pre-permission explainer. Returns true the FIRST time it is called for a
// given Settings — meaning "show the explainer now" — and false on every
// call after, recording the "shown" flag in Settings and persisting it
// (Settings::save) so the decision survives across launches. In short:
// show once, then suppress. No UI, no platform calls — testable off-Mac.
bool consumeScreenCaptureExplainer(Settings &settings);

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
