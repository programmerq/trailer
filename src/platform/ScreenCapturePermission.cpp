#include "platform/ScreenCapturePermission.h"

#include "settings/Settings.h"

#include <QDesktopServices>
#include <QObject>
#include <QUrl>

#ifdef Q_OS_MACOS
#include <QMessageBox>
#include <QPushButton>
// CGPreflightScreenCaptureAccess is declared here. It is a plain C function,
// so this translation unit stays plain C++; we only need CoreGraphics linked
// (see CMakeLists.txt, the if(APPLE) block on trailer_core).
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace trailer {

bool shouldShowScreenCaptureExplainer(Settings &settings) {
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

ScreenCaptureFlowAction decideScreenCaptureFlow(ScreenCapturePermissionState state,
                                                bool explainerAcknowledged) {
    switch (state) {
    case ScreenCapturePermissionState::Granted:
        return ScreenCaptureFlowAction::Proceed;
    case ScreenCapturePermissionState::Denied:
        return ScreenCaptureFlowAction::DegradeDenied;
    case ScreenCapturePermissionState::Undetermined:
        // Never asked (or can't tell from denied): show the one-time explainer
        // first, then drive the OS request. Once acknowledged, go straight to
        // the request — it prompts if truly undetermined and no-ops if denied.
        return explainerAcknowledged ? ScreenCaptureFlowAction::RequestAccess
                                     : ScreenCaptureFlowAction::ShowExplainerFirst;
    }
    return ScreenCaptureFlowAction::Proceed; // unreachable; keeps the compiler happy
}

QString screenRecordingSettingsUrlString() {
    return QStringLiteral(
        "x-apple.systempreferences:com.apple.preference.security"
        "?Privacy_ScreenCapture");
}

bool openScreenRecordingSettings() {
    return QDesktopServices::openUrl(QUrl(screenRecordingSettingsUrlString()));
}

QString screenRecordingNeededMessage() {
    return QObject::tr(
        "Trailer needs Screen Recording permission to capture the screen. "
        "Enable it in System Settings ▸ Privacy & Security ▸ Screen Recording, "
        "then reopen Trailer.");
}

ScreenCapturePermissionState queryScreenCapturePermissionState() {
#ifdef Q_OS_MACOS
    // The Granted check is authoritative: CGPreflightScreenCaptureAccess()
    // reflects the live TCC state (including immediately after a
    // `tccutil reset ScreenCapture`), without triggering the system prompt.
    if (CGPreflightScreenCaptureAccess())
        return ScreenCapturePermissionState::Granted;
    // CGPreflight cannot distinguish Denied from Undetermined; return
    // Undetermined and let the request path (CGRequestScreenCaptureAccess)
    // arbitrate — it prompts the OS when truly undetermined (re-registering the
    // app, e.g. after `tccutil reset`) and is a silent no-op when actually
    // denied. Recovery of a newly-granted permission takes effect on next launch.
    return ScreenCapturePermissionState::Undetermined;
#else
    // Non-macOS uses QScreen::grabWindow and needs no TCC permission.
    return ScreenCapturePermissionState::Granted;
#endif
}

bool requestScreenCaptureAccess() {
#ifdef Q_OS_MACOS
    // Prompts when undetermined (re-registering the app in TCC), silent no-op
    // returning false when denied, true when already granted. Never shows the
    // capture crosshair — that only happens once we shell to screencapture.
    return CGRequestScreenCaptureAccess();
#else
    return true;
#endif
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
    box.setInformativeText(
        QObject::tr("macOS calls this permission “Screen Recording” even for a "
                    "still screenshot, so it may now ask you to allow it. Trailer "
                    "does not record video — it only captures the image you "
                    "select.\n\nYou can review or change this later in System "
                    "Settings ▸ Privacy & Security ▸ Screen Recording."));
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
