// UAT evidence harness — representative offscreen grab() PNGs for the
// screen-capture permission preflight PR (the screen-capture preflight ADR,
// 2026-07-16-capture-permission-preflight.md).
//
// The REAL pre-permission explainer (maybeShowScreenCaptureExplainer) and the
// no-window degrade modal (Application::showScreenRecordingNeededModal) are
// both mac-gated (`#ifdef Q_OS_MACOS`) and cannot render on Linux CI. These
// slots therefore build *representative* widgets with the SAME title / text /
// informative-text / button set / message strings the production code uses,
// and grab them offscreen. They are representative, not the real dialogs — the
// PR text states real-macOS visuals need the owner's manual pass.
//
// Set TRAILER_CAPTURE_EVIDENCE_DIR to a directory to have each slot write its
// PNG there (offscreen QWidget::grab(), no display required). With the variable
// unset the slots still construct + assert the widget and skip the file write.
//
//   cap_ev_10_explainer -> capture-explainer.png
//   cap_ev_20_degrade   -> capture-degrade-status.png

#include "platform/ScreenCapturePermission.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QStatusBar>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

QString evidenceDir() {
    return QString::fromLocal8Bit(qgetenv("TRAILER_CAPTURE_EVIDENCE_DIR"));
}

// Grab `w` offscreen and write it under TRAILER_CAPTURE_EVIDENCE_DIR when set.
void saveShot(QWidget *w, const QString &name) {
    const QString dir = evidenceDir();
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    QApplication::processEvents();
    const QPixmap pm = w->grab();
    QVERIFY2(!pm.isNull(), qPrintable(QStringLiteral("grab returned null for %1").arg(name)));
    QVERIFY2(pm.save(QDir(dir).filePath(name)),
             qPrintable(QStringLiteral("failed to write %1").arg(name)));
}

} // namespace

class TestUatCapturePermissionEvidence : public QObject {
    Q_OBJECT
  private slots:
    void cap_ev_10_explainer();
    void cap_ev_20_degrade();
};

// #1 explainer: the one-time pre-permission explainer that precedes the OS
// prompt on first use. Reconstructed with the SAME title / text / informative
// text / Continue+Cancel buttons as maybeShowScreenCaptureExplainer(), grabbed
// offscreen (show()+processEvents, no exec()).
void TestUatCapturePermissionEvidence::cap_ev_10_explainer() {
    // Mirror maybeShowScreenCaptureExplainer's strings verbatim so the
    // representative shot matches the mac dialog.
    QMessageBox box;
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

    // Do NOT exec() — build, show, process, grab.
    box.show();
    QApplication::processEvents();
    QVERIFY2(box.text().contains(QStringLiteral("capture your screen")),
             "explainer must explain the capture");
    QVERIFY2(box.informativeText().contains(QStringLiteral("Screen Recording")),
             "explainer must name the Screen Recording permission");
    saveShot(&box, QStringLiteral("capture-explainer.png"));
    box.close();
}

// #2 degrade: the windowed degrade path shows the honest, recoverable
// screenRecordingNeededMessage() in the status bar (no crosshair, no unbidden
// System Settings auto-open). Grab a small QMainWindow whose status bar carries
// the real message string.
void TestUatCapturePermissionEvidence::cap_ev_20_degrade() {
    const QString msg = screenRecordingNeededMessage();
    QVERIFY2(msg.contains(QStringLiteral("Screen Recording")),
             "the degrade message must name the permission");
    QVERIFY2(msg.contains(QStringLiteral("reopen")),
             "the degrade message must carry the relaunch nuance");

    QMainWindow win;
    win.resize(1040, 120);
    auto *body = new QLabel(QObject::tr("Trailer"), &win);
    body->setAlignment(Qt::AlignCenter);
    win.setCentralWidget(body);
    win.statusBar()->showMessage(msg);
    win.show();
    QApplication::processEvents();
    QVERIFY2(win.statusBar()->currentMessage() == msg,
             "the status bar must carry the needed message verbatim");
    saveShot(&win, QStringLiteral("capture-degrade-status.png"));
    win.close();
}

QTEST_MAIN(TestUatCapturePermissionEvidence)
#include "test_uat_capture_permission_evidence.moc"
