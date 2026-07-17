// UAT evidence harness — representative offscreen grab() PNGs for the
// screen-capture permission preflight PR (the screen-capture preflight ADR,
// 2026-07-16-capture-permission-preflight.md).
//
// The pre-permission explainer is retired for the stills capture flow (owner
// decision 2026-07-17) — the flow now leans on the OS Screen Recording prompt
// directly — so its evidence shot was removed as misleading. The remaining
// no-window degrade path (Application::showScreenRecordingNeededModal) is
// mac-gated (`#ifdef Q_OS_MACOS`) and cannot render on Linux CI, so this slot
// builds a *representative* widget carrying the SAME message string the
// production code uses and grabs it offscreen. It is representative, not the
// real dialog — the PR text states real-macOS visuals need the owner's manual
// pass.
//
// Set TRAILER_CAPTURE_EVIDENCE_DIR to a directory to have the slot write its
// PNG there (offscreen QWidget::grab(), no display required). With the variable
// unset the slot still constructs + asserts the widget and skips the file write.
//
//   cap_ev_20_degrade   -> capture-degrade-status.png

#include "platform/ScreenCapturePermission.h"

#include <QDir>
#include <QLabel>
#include <QMainWindow>
#include <QObject>
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
    void cap_ev_20_degrade();
};

// degrade: the windowed degrade path shows the honest, recoverable
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
