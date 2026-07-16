#include "platform/ScreenCapturePermission.h"

#include "settings/Settings.h"

#ifdef Q_OS_MACOS
#include <QMessageBox>
#include <QPushButton>
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
