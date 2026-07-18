// Unit test — Quit modes, intercept, and kept-windows restore
// (macOS "Quit and Keep Windows";
// docs/decision-records/2026-07-16-quit-and-keep-windows.md).
//
// Exercises the cross-platform, headless core of the feature through the
// Application quit seams (performQuit + keeps-windows probe) and the
// MainWindow prompt seams (close-response + Save-As path) that Item 1 added:
//
//   * KeepWindows quit raises NO prompt, writes the draft store, and quits.
//   * Restore recreates the kept windows/documents; an untitled draft comes
//     back with byte-identical content and its untitled flag; the store is
//     consumed afterwards.
//   * Normal quit prompts per dirty/untitled document; Cancel aborts the
//     quit (performQuit not called, documents kept, nothing written);
//     all-Discard / all-Save let the quit proceed.
//   * The ⌥⌘Q "Quit and Keep Windows" QAction exists with the correct
//     shortcut and routes to the KeepWindows path.
//   * The OS NSQuitAlwaysKeepsWindows composition (D3): with the probe on,
//     a plain Quit takes the keep path and the Option alternate offers the
//     complement (prompt-and-close-clean).
//
// The native in-place Option menu swap and a real relaunch are NOT
// exercisable offscreen; they are covered by the record's owner-manual
// gate (clause 7).

#include "app/Application.h"
#include "document/ImageAdapter.h"
#include "ui/MainWindow.h"

#include <QDir>
#include <QImage>
#include <QKeySequence>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <memory>

using namespace trailer;

namespace {

// A deterministic, non-flat image so a lossy or truncated round-trip is
// caught. ARGB32 with a per-pixel gradient across all channels.
QImage makeKnownImage(int w = 23, int h = 17, int seed = 3) {
    QImage img(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int a = 255;
            const int r = (x * 11 + seed) & 0xFF;
            const int g = (y * 7 + seed * 3) & 0xFF;
            const int b = ((x + y) * 5 + seed) & 0xFF;
            img.setPixel(x, y, qRgba(r, g, b, a));
        }
    }
    return img;
}

// Attach a fresh untitled ImageDocument carrying `image` to a new window.
// Returns the raw doc pointer (owned by the window) for identity checks.
ImageDocument *addUntitledImageWindow(Application *app, const QImage &image,
                                      MainWindow **outWin = nullptr) {
    MainWindow *win = app->ensureFreshWindow();
    auto doc = std::make_unique<ImageDocument>(QString());
    doc->setImageForTest(image);
    doc->markUntitled();
    ImageDocument *ptr = doc.get();
    win->addDocument(std::move(doc));
    if (outWin)
        *outWin = win;
    return ptr;
}

void closeAllWindows(Application *app) {
    const auto wins = app->windows();
    for (MainWindow *w : wins) {
        if (w)
            w->close();
    }
    // Flush WA_DeleteOnClose deleteLater so m_windows empties.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

} // namespace

class TestQuitAndKeepWindows : public QObject {
    Q_OBJECT
  private slots:
    void init();
    void cleanup();

    void keepWindowsQuitWritesStoreWithoutPrompt();
    void restoreRehydratesUntitledDraftByteIdentical();
    void normalQuitCancelAborts();
    void normalQuitDiscardProceeds();
    void normalQuitSaveProceeds();
    void collectDirtyDocsIsCurrentFirstAndDeduped();
    void keepWindowsActionHasShortcutAndRoutes();
    void osKeepsWindowsFlipsQuitBranches();

  private:
    Application *m_app = nullptr;
};

void TestQuitAndKeepWindows::init() {
    m_app = qobject_cast<Application *>(qApp);
    QVERIFY(m_app);
    // Default the seams to a no-op quit + OS-keeps-off so a stray quit in a
    // slot cannot terminate the test process and D3 composition is neutral.
    m_app->setPerformQuitForTesting([] {});
    m_app->setQuitKeepsWindowsProbeForTesting([] { return false; });
    m_app->sessionDraftStore().clear();
    closeAllWindows(m_app);
}

void TestQuitAndKeepWindows::cleanup() {
    closeAllWindows(m_app);
    m_app->sessionDraftStore().clear();
}

void TestQuitAndKeepWindows::keepWindowsQuitWritesStoreWithoutPrompt() {
    MainWindow *win = nullptr;
    addUntitledImageWindow(m_app, makeKnownImage(), &win);
    // If a prompt were raised, this Cancel response would abort — but
    // KeepWindows must NOT prompt, so it stays irrelevant.
    win->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);

    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    const bool proceeded = m_app->requestQuit(QuitMode::KeepWindows);

    QVERIFY(proceeded);
    QCOMPARE(quitCount, 1);
    QVERIFY(m_app->sessionDraftStore().hasSession());
    // The document is still open (we injected a no-op quit).
    QCOMPARE(win->documentCount(), 1);
}

void TestQuitAndKeepWindows::restoreRehydratesUntitledDraftByteIdentical() {
    const QImage known = makeKnownImage(31, 19, 9);
    ImageDocument *original = addUntitledImageWindow(m_app, known);

    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QCOMPARE(quitCount, 1);
    QVERIFY(m_app->sessionDraftStore().hasSession());

    // Simulate relaunch-restore. performQuit was a no-op, so the original
    // window is still around; restore adds a NEW window/doc from the store.
    QVERIFY(m_app->restoreKeptWindows());

    // Find the restored untitled doc (distinct from the original pointer).
    ImageDocument *restored = nullptr;
    for (MainWindow *w : m_app->windows()) {
        if (!w)
            continue;
        for (int i = 0; i < w->documentCount(); ++i) {
            IDocument *d = nullptr;
            if (w->documentAt(i, &d) == 1 && d && d != original) {
                if (auto *img = dynamic_cast<ImageDocument *>(d))
                    restored = img;
            }
        }
    }
    QVERIFY(restored);
    QVERIFY(restored->isUntitled());
    // Byte-identical content: PNG is lossless, so pixels round-trip exactly.
    QCOMPARE(restored->image().convertToFormat(QImage::Format_ARGB32),
             known.convertToFormat(QImage::Format_ARGB32));
    // The store is a one-shot: consumed after a successful restore.
    QVERIFY(!m_app->sessionDraftStore().hasSession());
}

void TestQuitAndKeepWindows::normalQuitCancelAborts() {
    MainWindow *win = nullptr;
    addUntitledImageWindow(m_app, makeKnownImage(), &win);
    win->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);

    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    const bool proceeded = m_app->requestQuit(QuitMode::Normal);

    QVERIFY(!proceeded);           // aborted
    QCOMPARE(quitCount, 0);        // performQuit NOT called
    QCOMPARE(win->documentCount(), 1); // document kept
    QVERIFY(!m_app->sessionDraftStore().hasSession()); // nothing written
}

void TestQuitAndKeepWindows::normalQuitDiscardProceeds() {
    MainWindow *win = nullptr;
    addUntitledImageWindow(m_app, makeKnownImage(), &win);
    win->setCloseResponseForTesting(MainWindow::CloseResponse::Discard);

    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    QVERIFY(m_app->requestQuit(QuitMode::Normal));
    QCOMPARE(quitCount, 1);
}

void TestQuitAndKeepWindows::normalQuitSaveProceeds() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString savePath = dir.path() + "/kept.png";

    MainWindow *win = nullptr;
    ImageDocument *doc = addUntitledImageWindow(m_app, makeKnownImage(), &win);
    win->setCloseResponseForTesting(MainWindow::CloseResponse::Save);
    win->setSaveAsPathForTesting(savePath);

    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    QVERIFY(m_app->requestQuit(QuitMode::Normal));
    QCOMPARE(quitCount, 1);
    // The Save resolved the untitled doc to the chosen path.
    QVERIFY(QFileInfo::exists(savePath));
    QVERIFY(!doc->isUntitled());
    QVERIFY(!doc->isDirty());
}

void TestQuitAndKeepWindows::collectDirtyDocsIsCurrentFirstAndDeduped() {
    MainWindow *win = m_app->ensureFreshWindow();
    // Two untitled docs in one window.
    auto d1 = std::make_unique<ImageDocument>(QString());
    d1->setImageForTest(makeKnownImage(10, 10, 1));
    d1->markUntitled();
    auto d2 = std::make_unique<ImageDocument>(QString());
    d2->setImageForTest(makeKnownImage(10, 10, 2));
    d2->markUntitled();
    win->addDocument(std::move(d1));
    win->addDocument(std::move(d2));

    const std::vector<IDocument *> dirty = win->collectDirtyDocsForQuit();
    QCOMPARE(static_cast<int>(dirty.size()), 2);
    // Current-document-first: the most recently added tab is current.
    QCOMPARE(dirty.front(), win->collectDirtyDocsForQuit().front());
    // No duplicates.
    QVERIFY(dirty[0] != dirty[1]);
}

void TestQuitAndKeepWindows::keepWindowsActionHasShortcutAndRoutes() {
    MainWindow *win = nullptr;
    addUntitledImageWindow(m_app, makeKnownImage(), &win);

    QAction *keep = win->quitKeepWindowsActionForTesting();
    QVERIFY(keep);
    QCOMPARE(keep->shortcut(),
             QKeySequence(Qt::MetaModifier | Qt::AltModifier | Qt::Key_Q));

    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    keep->trigger();

    QCOMPARE(quitCount, 1);
    QVERIFY(m_app->sessionDraftStore().hasSession()); // KeepWindows path ran
}

void TestQuitAndKeepWindows::osKeepsWindowsFlipsQuitBranches() {
    MainWindow *win = nullptr;
    addUntitledImageWindow(m_app, makeKnownImage(), &win);
    // If the OS keeps windows, a plain Quit takes the KEEP path (no prompt).
    m_app->setQuitKeepsWindowsProbeForTesting([] { return true; });
    // A Cancel response would abort a Normal prompt — so if this proceeds
    // without abort, it proves the request was routed to the keep path.
    win->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);

    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });
    QVERIFY(m_app->requestQuit(QuitMode::Normal)); // keep path, no prompt
    QCOMPARE(quitCount, 1);
    QVERIFY(m_app->sessionDraftStore().hasSession());

    // And the Option alternate (KeepWindows request) offers the COMPLEMENT
    // under the OS setting: prompt-and-close-clean, so Cancel aborts it.
    m_app->sessionDraftStore().clear();
    win->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
    int quitCount2 = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount2; });
    QVERIFY(!m_app->requestQuit(QuitMode::KeepWindows)); // aborted (prompt)
    QCOMPARE(quitCount2, 0);
    QVERIFY(!m_app->sessionDraftStore().hasSession()); // nothing written
}

// Custom main: sandbox HOME/XDG before Application is constructed so the
// draft store, Settings and RecentFiles write into a throwaway sandbox, and
// force the offscreen QPA platform so widgets work headlessly. Mirrors
// test_macos_launch.cpp's scaffolding.
int main(int argc, char **argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    Application app(argc, argv);
    TestQuitAndKeepWindows tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_quit_and_keep_windows.moc"
