#include "platform/ScreenCapturePermission.h"

#include "settings/Settings.h"

#ifdef TRAILER_UX_RECORDER
#include <functional>
#include <utility>
#endif

#ifdef Q_OS_MACOS
#include <QMessageBox>
#include <QPushButton>
#endif

namespace trailer {

#ifdef TRAILER_UX_RECORDER
namespace {
// Recorder-only injected deference seam (ADR 0014). Holds the recorder's
// live Screen-Recording-granted probe so this file can consult Mechanism B's
// authoritative TCC state WITHOUT src/platform/ linking against src/uxrecord/.
// A function-pointer/std::function injection (over a direct call into
// uxrecord) keeps the module boundary clean and lets tests drive the granted
// state deterministically. Set from Application at startup; default-empty, in
// which case the gate falls through to its ordinary flag behaviour.
std::function<bool()> &screenRecordingGrantedProbe() {
    static std::function<bool()> probe;
    return probe;
}
} // namespace

void setScreenRecordingGrantedProbe(std::function<bool()> probe) {
    screenRecordingGrantedProbe() = std::move(probe);
}
#endif

bool shouldShowScreenCaptureExplainer(Settings &settings) {
#ifdef TRAILER_UX_RECORDER
    // Recorder builds only (ADR 0014, G14.2): defer to the recorder's live
    // macOS Screen Recording grant. If the OS already reports it granted, no
    // system prompt will fire and this explainer is redundant, so suppress it
    // even on a fresh profile with screen_capture_explainer unset — this closes
    // the double-prompt / split-suppression defect. Compiled out of default
    // builds entirely, so the non-recorder path below stays byte-for-byte (G14.4).
    if (const auto &probe = screenRecordingGrantedProbe(); probe && probe()) {
        return false;
    }
#endif
    const QString key = QString::fromLatin1(kScreenCaptureExplainerKey);
    // Pure query: no mutation. The flag is set only once the user proceeds
    // (acknowledgeScreenCaptureExplainer), so a cancelled explainer re-appears.
    return !settings.firstUseAcknowledged(key);
}

void acknowledgeScreenCaptureExplainer(Settings &settings) {
    const QString key = QString::fromLatin1(kScreenCaptureExplainerKey);
    // Record + persist so the explainer stays suppressed even if the app never
    // reaches its normal aboutToQuit save (crash, force quit). Settings setters
    // mutate in-memory only, so save() explicitly.
    settings.setFirstUseAcknowledged(key, true);
    settings.save();
}

#ifdef Q_OS_MACOS
bool maybeShowScreenCaptureExplainer(Settings &settings, QWidget *parent) {
    if (!shouldShowScreenCaptureExplainer(settings)) {
        // Already acknowledged on a previous use — proceed straight to capture.
        return true;
    }

    QMessageBox box(parent);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QObject::tr("Screen Recording Permission"));
    box.setText(QObject::tr("Trailer is about to capture your screen to import a "
                            "screenshot."));
#ifdef TRAILER_UX_RECORDER
    // Recorder builds align the Screen-Recording references across both flows
    // (this explainer and the recorder's launch gate) on the "→" separator the
    // recorder already uses in its Screen-Recording messaging
    // (MacUxPlatformCapture.mm), so the two dialogs read as one voice (ADR 0014
    // G14.3). Kept as a whole-string #ifdef/#else so the default build's #else
    // branch stays byte-for-byte identical to #59 (G14.4) — only the separator
    // glyph differs; terminology and capitalization are already identical.
    box.setInformativeText(
        QObject::tr("macOS calls this permission “Screen Recording” even for a "
                    "still screenshot, so it may now ask you to allow it. Trailer "
                    "does not record video — it only captures the image you "
                    "select.\n\nYou can review or change this later in System "
                    "Settings → Privacy & Security → Screen Recording."));
#else
    box.setInformativeText(
        QObject::tr("macOS calls this permission “Screen Recording” even for a "
                    "still screenshot, so it may now ask you to allow it. Trailer "
                    "does not record video — it only captures the image you "
                    "select.\n\nYou can review or change this later in System "
                    "Settings ▸ Privacy & Security ▸ Screen Recording."));
#endif
    QPushButton *proceed = box.addButton(QObject::tr("Continue"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(proceed);
    box.exec();
    const bool didProceed = box.clickedButton() == proceed;
    if (didProceed) {
        // Only burn the "shown" flag when the user actually continues into the
        // capture; a cancel leaves it unset so the explainer re-appears next time.
        acknowledgeScreenCaptureExplainer(settings);
    }
    return didProceed;
}
#endif

} // namespace trailer
