// UAT evidence harness — offscreen grab() PNGs for the UX recorder's
// visible MainWindow controls (docs/ux-recorder.md, UXR-002).
//
// Compiled only into recorder-enabled builds (TRAILER_UX_RECORDER); the
// widgets it grabs — the "● REC" status-bar chip and the "◉ Recording"
// menu — are cross-platform Qt, so they render and grab offscreen with
// the stub capture backend even though the screen/camera/input capture
// itself is macOS-only.
//
// Runs under QT_QPA_PLATFORM=offscreen like every other uat slot. Each
// slot both asserts the wired behaviour AND, when an evidence directory
// is available, writes a curated grab() PNG there. The directory is the
// committed docs/evidence/ux-recorder/ tree by default (compiled in via
// TRAILER_UX_EVIDENCE_DIR), overridable with $TRAILER_UX_EVIDENCE_DIR.
//
//   ux_ev_10_recChipNormal    -> ux-recorder-rec-chip.png
//   ux_ev_20_recordingMenu    -> ux-recorder-menu.png
//   ux_ev_30_recChipDegraded  -> ux-recorder-rec-chip-no-screen.png
//
// The degraded amber state is forced through the recorder's public
// reportStreamDegraded() seam, so all three states are real offscreen
// captures — no macOS pass is required to produce them.

#include "app/Application.h"
#include "ui/MainWindow.h"
#include "uxrecord/UxRecorder.h"

#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <memory>

using namespace trailer;

namespace {

// The status-bar REC chip installed by uxrecord::attachToMainWindow().
QLabel *recChip(MainWindow *mw) {
    return mw->findChild<QLabel *>(QStringLiteral("uxRecorderIndicator"));
}

// The top-level "◉ Recording" menu installed by the recorder hooks.
QMenu *recordingMenu(MainWindow *mw) {
    for (QAction *top : mw->menuBar()->actions()) {
        if (QMenu *m = top->menu(); m && top->text().contains(QStringLiteral("Recording")))
            return m;
    }
    return nullptr;
}

QString evidenceDir() {
    const QString override = QString::fromLocal8Bit(qgetenv("TRAILER_UX_EVIDENCE_DIR"));
    if (!override.isEmpty())
        return override;
#ifdef TRAILER_UX_EVIDENCE_DIR
    return QStringLiteral(TRAILER_UX_EVIDENCE_DIR);
#else
    return QString();
#endif
}

// Grab `w` offscreen and write it under the evidence directory.
void saveShot(QWidget *w, const QString &name) {
    const QString dir = evidenceDir();
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    QApplication::processEvents();
    const QPixmap pm = w->grab();
    QVERIFY2(!pm.isNull(), qPrintable(QStringLiteral("grab returned null for %1").arg(name)));
    QVERIFY2(pm.width() > 0 && pm.height() > 0,
             qPrintable(QStringLiteral("grab was 0-sized for %1").arg(name)));
    QVERIFY2(pm.save(QDir(dir).filePath(name)),
             qPrintable(QStringLiteral("failed to write %1").arg(name)));
}

} // namespace

class TestUatUxRecorderEvidence : public QObject {
    Q_OBJECT
  private slots:
    void init();
    void cleanup();
    void ux_ev_10_recChipNormal();
    void ux_ev_20_recordingMenu();
    void ux_ev_30_recChipDegraded();

  private:
    // A live recorder registers itself as the process-wide active
    // recorder (g_activeRecorder), which uxrecord::attachToMainWindow()
    // reads when the MainWindow is constructed. Owned per-slot so its
    // window connections stay valid for the grab.
    std::unique_ptr<UxRecorder> m_recorder;
    QTemporaryDir m_sessionRoot;

    MainWindow *freshRecorderWindow() {
        // Close any window from a previous slot so the new one attaches
        // to the recorder we start below.
        for (auto *w : QApplication::topLevelWidgets()) {
            if (qobject_cast<MainWindow *>(w))
                w->close();
        }
        QApplication::processEvents();

        m_recorder = std::make_unique<UxRecorder>(m_sessionRoot.path(),
                                                  /*withPlatformCapture=*/false);
        if (!m_recorder->start())
            return nullptr;

        auto *app = qobject_cast<Application *>(qApp);
        if (!app)
            return nullptr;
        MainWindow *mw = app->ensureWindow();
        if (mw) {
            // Give the status bar room so the chip is legible in the grab.
            mw->resize(1100, 720);
            QApplication::processEvents();
        }
        return mw;
    }
};

void TestUatUxRecorderEvidence::init() {
    QVERIFY(m_sessionRoot.isValid());
}

void TestUatUxRecorderEvidence::cleanup() {
    if (m_recorder) {
        m_recorder->stop();
        m_recorder.reset();
    }
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// #1 normal REC chip: a recorder-enabled MainWindow carries the red
// "● REC" status-bar chip. Grab the status bar (chip in context) so the
// evidence reads clearly.
void TestUatUxRecorderEvidence::ux_ev_10_recChipNormal() {
    MainWindow *mw = freshRecorderWindow();
    QVERIFY2(mw, "recorder-enabled MainWindow should be constructible offscreen");

    QLabel *chip = recChip(mw);
    QVERIFY2(chip, "REC status-bar chip (uxRecorderIndicator) must exist");
    QCOMPARE(chip->text(), QStringLiteral("● REC"));

    saveShot(mw->statusBar(), QStringLiteral("ux-recorder-rec-chip.png"));
}

// #2 Recording menu: the recorder installs a top-level "◉ Recording"
// menu. Pop it up offscreen so its items render, then grab the menu.
void TestUatUxRecorderEvidence::ux_ev_20_recordingMenu() {
    MainWindow *mw = freshRecorderWindow();
    QVERIFY2(mw, "recorder-enabled MainWindow should be constructible offscreen");

    QMenu *menu = recordingMenu(mw);
    QVERIFY2(menu, "◉ Recording menu must exist");
    QVERIFY2(!menu->actions().isEmpty(), "Recording menu must have items");

    // Realise the menu so its item layout is drawn before the grab.
    menu->popup(QPoint(0, 0));
    QApplication::processEvents();
    QApplication::processEvents();

    saveShot(menu, QStringLiteral("ux-recorder-menu.png"));
    menu->hide();
}

// #3 degraded REC chip: driving a stream degraded (here "screen", as if
// Screen Recording were denied) must relabel the chip to the amber
// "● REC · no screen" persistent state (UXR-002). Forced through the
// public reportStreamDegraded() seam so the amber state is a real
// offscreen capture, not a macOS-only one.
void TestUatUxRecorderEvidence::ux_ev_30_recChipDegraded() {
    MainWindow *mw = freshRecorderWindow();
    QVERIFY2(mw, "recorder-enabled MainWindow should be constructible offscreen");

    QLabel *chip = recChip(mw);
    QVERIFY2(chip, "REC status-bar chip (uxRecorderIndicator) must exist");

    m_recorder->reportStreamDegraded(QStringLiteral("screen"));
    QApplication::processEvents();

    QCOMPARE(chip->text(), QStringLiteral("● REC · no screen"));

    saveShot(mw->statusBar(), QStringLiteral("ux-recorder-rec-chip-no-screen.png"));
}

// Custom main: sandbox HOME before Application is constructed so
// Settings/RecentFiles never touch the real config dir (matches the
// other uat harnesses).
int main(int argc, char **argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    Application app(argc, argv);
    TestUatUxRecorderEvidence tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_ux_recorder_evidence.moc"
