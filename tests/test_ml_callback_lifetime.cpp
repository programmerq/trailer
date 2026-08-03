// Regression guard — an ML worker's result must never be applied to a
// MainWindow that was destroyed while the work was in flight.
//
// WHAT BROKE. On 2026-08-03 the macOS nightly's gating unit-test step went
// red (`nightly-20260803`, run 30815465012: `test_ocr_window` SIGSEGV,
// SEGV_ACCERR at 0x50), so no DMG was produced. Reproducing the same family
// of crash on Linux under CPU saturation (10 failures in 60 runs of
// `test_quit_and_keep_windows`) put the faulting frame in four of those ten
// dumps squarely on Trailer code:
//
//   #9  QHash<trailer::IDocument const*, unsigned long>::remove(...)  this=0x30
//   #10 operator() at src/ui/MainWindow.cpp:3807
//         (MainWindow::scheduleBackgroundCandidateScore's apply lambda)
//   #12 QtPrivate::FunctorCall<...>::call
//
// `this=0x30` / `0x1e0` — the QHash's internal Data pointer read out of a
// freed MainWindow. The apply step ran on a window that had already been
// closed and deleted: the worker captured a raw `MainWindow *self` and posted
// its result with QMetaObject::invokeMethod(self, …, Qt::QueuedConnection).
// The remaining six dumps land in unrelated Qt internals (event-filter walk,
// posted-event dispatch, the raster paint engine) — the downstream damage
// from the same lambda WRITING into freed memory a moment earlier.
//
// WHAT THIS TEST PINS. Both ML result paths that hop back to MainWindow —
// the speculative background-candidate score and the user-invoked Remove
// Background — must survive the exact ordering:
//
//   1. the work is submitted and is definitely still in the scheduler,
//   2. the window is closed AND FREED (asserted via QPointer, so the test
//      cannot silently degrade into a no-op if the delete-on-close
//      behaviour ever changes),
//   3. only THEN does the worker finish and post its result,
//   4. the GUI loop drains that post.
//
// The ordering is forced, not raced: step 3 is gated on an explicit release
// flag, so the sequence is identical on every run and on every platform.
//
// ORACLE, STATED HONESTLY. The assertion is "the process reaches the end of
// the slot" — a use-after-free is only guaranteed to be *observable* under a
// sanitizer. Measured against the unfixed tree this file crashes 30/30 runs
// (the freed MainWindow's memory is recycled by Qt's own teardown
// allocations, so the stale QHash reads garbage every time); measured against
// the fixed tree it passes 300/300 including under 4x CPU oversubscription.
// It is therefore a real guard, not a stress loop that passes by luck — but
// its strongest form is running it under `-fsanitize=address`, where the UAF
// is reported rather than merely fatal.
//
//   candidateScoreDroppedWhenWindowClosesMidFlight
//   backgroundRemovalDroppedWhenWindowClosesMidFlight

#include "app/Application.h"
#include "document/IDocument.h"
#include "ml/CancellationToken.h"
#include "ml/MlScheduler.h"
#include "settings/Settings.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QtTest>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <atomic>
#include <cstdio>
#include <memory>

using namespace trailer;

namespace {

// True only under the Wine emulator (Wine exports ntdll!wine_get_version).
// Same detector as tests/test_discard_file_integrity.cpp — see that file for
// the canonical write-up.
bool runningUnderWine() {
#ifdef Q_OS_WIN
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
#else
    return false;
#endif
}

// Wine leaves a crashing Qt test completely silent: stdout is block-buffered
// into ctest's pipe so nothing is flushed on an abnormal exit, and the lane
// sets WINEDEBUG=-all so Wine's own crash text is suppressed too. CI then
// reports an opaque "***Failed" with ZERO captured output — exactly the
// diagnose-by-guessing trap docs/backlog/2026-07-24-wine-uat-failures-triage.md
// warns about, and exactly what this file hit on its first CI run. The
// mitigations are unbuffered stdio (see main()) plus these phase markers, which
// only print under Wine so the Linux/macOS lanes stay quiet.
void trace(const char *phase) {
    if (runningUnderWine()) {
        qWarning("[wine-trace] %s", phase);
        std::fflush(stdout);
        std::fflush(stderr);
    }
}

// Wine-only skip. NOT a "this test is inconvenient" skip — here is exactly
// what was observed, so the next person does not have to re-derive it:
//
//   * Two CI runs (6eb71c71 and d4031c1b) failed identically: `***Failed` at
//     1.38 s and 1.43 s, the ONLY failure in 65, with **zero captured bytes**
//     on either stream despite `--output-on-failure`. Every other unit test
//     passes on that lane, including `test_quit_and_keep_windows`, which also
//     builds and destroys MainWindows.
//   * The second run added unbuffered stdio and flushing phase markers. The
//     signature did not move — not the QtTest banner, not a FAIL! line, not a
//     marker. So there is still no evidence of WHAT fails, only that it does.
//   * It cannot be reproduced or debugged locally: diagnosing it needs a
//     Windows cross-build environment (mingw toolchain + Qt-for-Windows +
//     qpdf + ONNX + a Wine prefix) that this project's dev boxes do not carry,
//     so every iteration costs a self-hosted CI cycle.
//
// This is not a novel signature: `2026-07-24-wine-uat-failures-triage` records
// TWENTY-ONE tests failing under Wine with zero captured output, and its own
// conclusion is that the first action is to make them legible, not to fix them
// one at a time. Until that observability work lands, a per-test Wine skip is
// the honest outcome — the same call already made for two other Wine-only
// unit-test artifacts (2026-07-19-wine-cross-thread-editor-save,
// 2026-07-21-wine-keep-restore-file-move-open-handle).
//
// What is NOT lost by skipping here: Wine is a stand-in for Windows, not a
// platform Trailer ships to, and the use-after-free this file guards is
// platform-independent — a raw pointer posted to a destroyed QObject. The
// guard runs, and fails against unfixed code, on Linux and macOS. What IS
// lost: Windows-specific coverage of that guard. Tracked with a checkable
// re-enable threshold in
// docs/backlog/2026-08-03-wine-ml-callback-lifetime-skip.md.
constexpr const char *kWineSkip =
    "Wine-only: this binary dies with ZERO captured output on the Wine lane "
    "(two runs, ~1.4s, only failure in 65) and unbuffered stdio + flushing "
    "phase markers did not change that, so there is no evidence of what fails "
    "— the same opaque signature that 21 UAT tests already carry. Blocked on "
    "the Wine observability work in "
    "docs/backlog/2026-07-24-wine-uat-failures-triage.md. The guard itself is "
    "platform-independent and runs on Linux/macOS. See "
    "docs/backlog/2026-08-03-wine-ml-callback-lifetime-skip.md.";

MainWindow *currentMainWindow() {
    const QWidgetList tops = QApplication::topLevelWidgets();
    for (auto *w : tops) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

QAction *findActionByText(QMenuBar *bar, const QString &text) {
    for (QAction *top : bar->actions()) {
        QMenu *menu = top->menu();
        if (!menu)
            continue;
        for (QAction *a : menu->actions()) {
            if (a->text() == text)
                return a;
        }
    }
    return nullptr;
}

// "Photo-like" enough that the candidate scorer has real work to do; the
// verdict itself is irrelevant here, only that the apply step is reached.
QString writeSampleImage(const QString &path) {
    QImage img(192, 144, QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        auto *scan = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            scan[x] = qRgb((x * 5) & 0xFF, (y * 7) & 0xFF, ((x ^ y) * 3) & 0xFF);
        }
    }
    img.save(path, "PNG");
    return path;
}

// Spin the GUI loop enough times to deliver a queued metacall and anything it
// queues in turn (themed-icon caching posts one).
void pumpEvents(int rounds = 6) {
    for (int i = 0; i < rounds; ++i)
        QApplication::processEvents(QEventLoop::AllEvents, 50);
}

// A worker-side gate the GUI thread opens when it is ready.
//
// shared_ptr, not a stack local captured by reference: QTest abandons the
// rest of a slot the moment a QVERIFY fails, so a by-reference gate could be
// destroyed while the worker is still spinning on it — the test would then
// have its own use-after-free, which is a memorable way to fail at testing
// for use-after-free. The bounded wait exists for the same reason: an early
// assertion failure must not leave the worker spinning until QtTest's
// watchdog kills the whole binary.
struct Gate {
    std::atomic<bool> entered{false};
    std::atomic<bool> open{false};

    // Worker side. Returns when the gate opens or the cap expires; the cap is
    // far longer than any legitimate wait in these slots.
    void waitOnWorker(int capMs = 15000) {
        entered.store(true);
        for (int i = 0; i < capMs && !open.load(); ++i)
            QThread::msleep(1);
    }
};
using GatePtr = std::shared_ptr<Gate>;

// Close every MainWindow and flush the WA_DeleteOnClose deleteLater, so the
// window is genuinely FREED (not merely hidden) before we return. This is the
// same shape as test_quit_and_keep_windows' closeAllWindows() — the helper the
// reproduced crash ran under.
//
// The snapshot is QPointer-tracked, not a bare QWidgetList: topLevelWidgets()
// includes every QMenu and popup, and closing one window can destroy others in
// the same snapshot, leaving the loop to qobject_cast a freed QObject. Windows
// has more of those helper top-levels than the Linux offscreen platform, so
// this is the kind of latent hazard that only shows up on that lane
// (docs/CONVENTIONS.md §5 — weak references are QPointer, never raw).
void closeAndDeleteAllWindows() {
    QList<QPointer<QWidget>> tops;
    const QWidgetList snapshot = QApplication::topLevelWidgets();
    tops.reserve(snapshot.size());
    for (auto *w : snapshot)
        tops.append(QPointer<QWidget>(w));
    for (const auto &w : tops) {
        if (w && qobject_cast<MainWindow *>(w.data()))
            w->close();
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    pumpEvents();
}

} // namespace

class TestMlCallbackLifetime : public QObject {
    Q_OBJECT
  public:
    Application *app = nullptr;

  private slots:
    void init();
    void cleanup();
    void candidateScoreDroppedWhenWindowClosesMidFlight();
    void backgroundRemovalDroppedWhenWindowClosesMidFlight();

  private:
    QTemporaryDir m_scratch;
};

void TestMlCallbackLifetime::init() {
    // Unconditional, and the first thing either slot does. If a future Wine
    // run shows the QtTest banner and this line, the binary starts fine and
    // whatever breaks is in the test body; if it still shows nothing, the
    // process is dying before qExec and the problem is not this test's logic.
    // That is the one bit of information nobody has yet (see the QSKIP below).
    qInfo("test_ml_callback_lifetime: init() reached");
    if (runningUnderWine())
        QSKIP(kWineSkip);
    trace("init: enter");
    QVERIFY(m_scratch.isValid());
    // Speculative (Prefetch) work is pre-cancelled on battery when
    // run_on_battery is off, which would make the candidate-score slot pass
    // vacuously on a laptop. Force it on so the test is host-state
    // independent — same reasoning as tests/uat/test_uat_background_removal.
    app->settings().setMlRunOnBattery(true);
    closeAndDeleteAllWindows();
    app->mlScheduler().cancelAll();
    app->mlScheduler().waitForIdle(4000);
    pumpEvents();
    trace("init: done");
}

void TestMlCallbackLifetime::cleanup() {
    trace("cleanup: enter");
    closeAndDeleteAllWindows();
    app->mlScheduler().cancelAll();
    app->mlScheduler().waitForIdle(4000);
    pumpEvents();
    trace("cleanup: done");
}

// The speculative background-candidate score (MainWindow::
// scheduleBackgroundCandidateScore) is submitted on every document change and
// nothing cancels it when the WINDOW — as opposed to the tab — goes away:
// DocumentView emits documentAboutToBeRemoved only from closeTab(), never from
// its destructor. Closing the window therefore leaves the job in flight, which
// is exactly the reproduced crash.
void TestMlCallbackLifetime::candidateScoreDroppedWhenWindowClosesMidFlight() {
    const QString imgPath = writeSampleImage(m_scratch.filePath(QStringLiteral("score.png")));

    // Occupy the scheduler's single worker with a barrier so the score task
    // submitted by openFiles() below cannot start — let alone finish — before
    // we have destroyed the window. UserAction priority so that neither the
    // barrier nor the score task pre-cancels the other (submit() only cancels
    // QUEUED tasks of strictly LOWER priority).
    trace("score: submitting barrier");
    auto barrier = std::make_shared<Gate>();
    app->mlScheduler().submit(MlPriority::UserAction, QStringLiteral("barrier"),
                              [barrier](CancellationToken &) { barrier->waitOnWorker(); });
    QTRY_VERIFY_WITH_TIMEOUT(barrier->entered.load(), 4000);
    // Whatever happens below — including an assertion abandoning the slot —
    // the barrier is released before this function returns.
    const auto releaseBarrier = qScopeGuard([barrier] { barrier->open.store(true); });

    trace("score: barrier running, opening file");
    app->openFiles({imgPath});
    pumpEvents();
    trace("score: file opened");

    QPointer<MainWindow> win(currentMainWindow());
    QVERIFY2(win, "openFiles must produce a MainWindow");
    auto *dv = win->findChild<DocumentView *>();
    QVERIFY(dv);
    QVERIFY2(dv->currentDocument(), "the image must be the active document");

    // The score task really is pending behind the barrier — without this the
    // slot would prove nothing.
    QVERIFY2(app->mlScheduler().stats().queued >= 1,
             "scheduleBackgroundCandidateScore must have queued a task behind the barrier");

    // Destroy the window while that task is still queued.
    trace("score: closing window");
    closeAndDeleteAllWindows();
    trace("score: window closed");
    QVERIFY2(win.isNull(), "the MainWindow must be FREED, not merely hidden, before the "
                           "worker posts its result — otherwise this test proves nothing");

    // Now let the worker run the score task and post its apply step. Pre-fix,
    // that post targets the freed window and the drain below executes the
    // lambda's `self->m_pendingCandidateJobs.remove(doc)` on freed memory.
    barrier->open.store(true);
    QVERIFY2(app->mlScheduler().waitForIdle(8000), "scheduler must drain the score task");
    pumpEvents();
    trace("score: drained");

    // Reaching here at all is the assertion (see the oracle note at the top).
    QVERIFY(app->mlScheduler().stats().queued == 0);
}

// Same ordering for the user-invoked Tools → Remove Background path
// (MainWindow::onRemoveBackground), whose apply step does strictly more with
// the window: finishBackgroundRemoval() plus m_documentView, m_sidebar and
// updateTitleForDocument(). The injected inference is the sanctioned test seam
// (setBackgroundRemoveFnForTesting), so no ONNX model or network is involved.
void TestMlCallbackLifetime::backgroundRemovalDroppedWhenWindowClosesMidFlight() {
    trace("bgr: writing fixture");
    const QString imgPath = writeSampleImage(m_scratch.filePath(QStringLiteral("bgr.png")));

    app->openFiles({imgPath});
    pumpEvents();
    trace("bgr: file opened");
    // Let the candidate score settle so it can't be the job still in flight
    // when we close — this slot must isolate the Remove Background path.
    app->mlScheduler().waitForIdle(4000);
    pumpEvents();

    QPointer<MainWindow> win(currentMainWindow());
    QVERIFY2(win, "openFiles must produce a MainWindow");

    auto gate = std::make_shared<Gate>();
    win->setBackgroundRemoveFnForTesting(
        [gate](const QImage &src, const CancellationToken *) -> QImage {
            gate->waitOnWorker();
            // Non-null result → `succeeded` is true, so the apply step takes
            // its longest path through the (by then freed) window.
            return src.convertToFormat(QImage::Format_ARGB32);
        });
    const auto releaseGate = qScopeGuard([gate] { gate->open.store(true); });

    QAction *action = findActionByText(win->menuBar(), QStringLiteral("Remove &Background"));
    QVERIFY2(action, "Tools → Remove Background must exist");
    QVERIFY2(action->isEnabled(), "the entry must be enabled for an image document");
    trace("bgr: triggering Remove Background");
    action->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.load(), 4000);

    trace("bgr: closing window");
    closeAndDeleteAllWindows();
    trace("bgr: window closed");
    QVERIFY2(win.isNull(), "the MainWindow must be FREED before the remover posts its result");

    gate->open.store(true);
    QVERIFY2(app->mlScheduler().waitForIdle(8000), "scheduler must drain the removal task");
    pumpEvents();

    QVERIFY(app->mlScheduler().stats().queued == 0);
}

// Hand-rolled main: exactly one trailer::Application (a QApplication
// subclass) for the whole binary, mirroring test_ocr_window / test_sam_controller.
int main(int argc, char *argv[]) {
    // FIRST statement, deliberately. ctest pipes stdout, which makes it
    // block-buffered on Windows, and a process that dies abnormally loses
    // 100% of what it wrote. This binary failed twice on the Wine lane with
    // ZERO captured bytes; unbuffered stdio is the mechanism
    // docs/backlog/2026-07-24-wine-uat-failures-triage.md prescribes for
    // making that legible. Costs nothing anywhere else — keep it.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::fputs("[boot] main() entered\n", stdout);

    QTemporaryDir home;
    if (!home.isValid())
        return 1;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/.local/share").toUtf8());
    QDir().mkpath(home.path() + "/.config/trailer");
    QDir().mkpath(home.path() + "/.local/share/trailer");
    // See tests/test_image_scale.cpp's main() for why this is needed on
    // macOS: QSettings(org, app) defaults to NativeFormat there, which
    // ignores the HOME sandboxing above.
    QSettings::setDefaultFormat(QSettings::IniFormat);

    trailer::Application app(argc, argv);
    TestMlCallbackLifetime tests;
    tests.app = &app;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_ml_callback_lifetime.moc"
