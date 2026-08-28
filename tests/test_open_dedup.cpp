// Unit tests for the "already-open documents are surfaced, never opened
// twice" rule (owner HITL report 2026-08-06).
//
// Two levels:
//   * trailer::canonicalPathKey  — the one identity rule (util/PathKey.h):
//     symlink collapse, the missing-path absoluteFilePath fallback, and
//     the empty-key contract.
//   * Application::windowForOpenPath / openFiles — the lookup and the open
//     path built on it, across all three open_files_in modes plus the
//     mixed-batch and untitled cases.

#include "app/Application.h"
#include "document/IDocument.h"
#include "settings/Settings.h"
#include "ui/MainWindow.h"
#include "util/PathKey.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cstdio>

using namespace trailer;

namespace {

QString writeTinyPdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(100, 100, QStringLiteral("dedup fixture"));
    p.end();
    return path;
}

QString writeTinyPng(const QString &path) {
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::white);
    img.save(path, "PNG");
    return path;
}

QList<MainWindow *> liveWindows() {
    QList<MainWindow *> out;
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            out.append(mw);
    }
    return out;
}

int totalOpenDocuments() {
    int n = 0;
    for (MainWindow *mw : liveWindows())
        n += mw->documentCount();
    return n;
}

} // namespace

class TestOpenDedup : public QObject {
    Q_OBJECT

  private:
    QTemporaryDir m_dir;

    Application *app() const { return qobject_cast<Application *>(qApp); }

  private slots:
    void init();
    void cleanup();

    // --- canonicalPathKey (util/PathKey.h) ---
    void keyResolvesSymlinkToItsTarget();
    void keyFallsBackToAbsolutePathForMissingFile();
    void keyKeepsDistinctMissingPathsDistinct();
    void keyIsEmptyForEmptyInput();
    void keyNormalisesDotSegments();

    // --- Application::windowForOpenPath ---
    void lookupFindsOpenDocumentAndItsTabIndex();
    void lookupMissesUnopenedPath();
    void lookupIgnoresEmptyPath();
    void lookupIgnoresUntitledDocuments();
    void lookupMatchesThroughSymlink();
    void lookupPrefersOldestWindowHoldingTheFile();

    // --- Application::openFiles ---
    void reopenSurfacesInNewWindowMode();
    void reopenSurfacesInNewTabMode();
    void reopenSurfacesInSameWindowMode();
    void reopenSelectsTheDocumentsOwnTab();
    void mixedBatchOpensOnlyTheNewFiles();
    void repeatedPathWithinOneBatchOpensOnce();
    void allAlreadyOpenBatchSpawnsNoWindow();
    void imageBatchWithOneAlreadyOpenDoesNotSpawnAnEmptyBatchWindow();
    void surfacingDoesNotConsumeTheEmptyWindowReuseCandidate();
    void untitledImportsAreNeverDeduped();
    void distinctFilesStillOpenSeparately();
};

void TestOpenDedup::init() {
    QVERIFY(app());
    app()->settings().setOpenFilesIn(OpenFilesIn::NewWindow);
}

// Destroy every window a slot left behind, after EVERY slot.
//
// The teardown has to be here, in cleanup(), and not only in init(). init()
// runs BEFORE each slot, so it never runs after the LAST one: whatever
// windows the final slot opened — real MainWindows, each holding an open
// PdfDocument and its view — stayed alive through main()'s return and into
// Application / QApplication destruction. Tearing widgets down at that
// point is its own hazard, and it is the one concrete way this file
// differed from every other Application-driving unit test here:
// TestImageScale and TestQuitAndKeepWindows both tear down in cleanup()
// and so reach process exit with zero windows.
//
// That is what broke this file on the Wine cross-build lane. The slots ran
// (~2 s, a full run's worth) and the process then died having printed
// nothing at all — no QTest banner, no PASS lines, no assertion — which is
// a late crash with the results still sitting in an unflushed stdout
// buffer, not a failed comparison. Linux never showed it: the same "could
// not be reproduced locally on Linux" shape TestImageScale::cleanup()'s own
// comment warns about.
//
// `delete`, not `close()`, for the reason spelled out there: close() runs
// MainWindow::closeEvent(), which PERSISTS RecentFiles /
// DocumentTypeDefaults state — exactly the contamination a per-slot
// teardown exists to prevent. It also means no dirty-document prompt can
// block a headless run.
void TestOpenDedup::cleanup() {
    auto *a = app();
    if (!a)
        return;
    const QList<MainWindow *> windows = a->windows();
    for (MainWindow *w : windows)
        delete w;
    QApplication::processEvents();
    // Assert the teardown actually emptied the set rather than assuming it.
    // This is the invariant whose absence broke the Wine lane, so it is
    // worth failing loudly and precisely on the slot that reintroduces it,
    // on every platform, instead of surfacing as a silent late crash.
    QCOMPARE(a->windowCount(), 0);
}

// ---------------------------------------------------------------- keys

void TestOpenDedup::keyResolvesSymlinkToItsTarget() {
#ifdef Q_OS_WIN
    QSKIP("Symlink creation on Windows needs developer mode / elevation.");
#else
    QVERIFY(m_dir.isValid());
    const QString target = writeTinyPdf(m_dir.filePath("key_target.pdf"));
    const QString link = m_dir.filePath("key_link.pdf");
    QVERIFY(QFile::link(target, link));
    QCOMPARE(canonicalPathKey(link), canonicalPathKey(target));
#endif
}

void TestOpenDedup::keyFallsBackToAbsolutePathForMissingFile() {
    QVERIFY(m_dir.isValid());
    const QString missing = m_dir.filePath("does_not_exist.pdf");
    QVERIFY(!QFileInfo::exists(missing));
    // canonicalFilePath() is empty for a missing file; without the
    // fallback the key would be "" and would collide with every other
    // missing path.
    QVERIFY(!canonicalPathKey(missing).isEmpty());
    QCOMPARE(canonicalPathKey(missing), QFileInfo(missing).absoluteFilePath());
}

void TestOpenDedup::keyKeepsDistinctMissingPathsDistinct() {
    QVERIFY(m_dir.isValid());
    const QString a = m_dir.filePath("missing_a.pdf");
    const QString b = m_dir.filePath("missing_b.pdf");
    QVERIFY(canonicalPathKey(a) != canonicalPathKey(b));
}

void TestOpenDedup::keyIsEmptyForEmptyInput() {
    QVERIFY(canonicalPathKey(QString()).isEmpty());
    QVERIFY(canonicalPathKey(QLatin1String("")).isEmpty());
}

void TestOpenDedup::keyNormalisesDotSegments() {
    QVERIFY(m_dir.isValid());
    const QString direct = writeTinyPdf(m_dir.filePath("dots.pdf"));
    QDir().mkpath(m_dir.filePath("sub"));
    const QString roundabout = m_dir.filePath("sub/../dots.pdf");
    QCOMPARE(canonicalPathKey(roundabout), canonicalPathKey(direct));
}

// -------------------------------------------------------------- lookup

void TestOpenDedup::lookupFindsOpenDocumentAndItsTabIndex() {
    QVERIFY(m_dir.isValid());
    const QString a = writeTinyPdf(m_dir.filePath("look_a.pdf"));
    const QString b = writeTinyPdf(m_dir.filePath("look_b.pdf"));

    app()->settings().setOpenFilesIn(OpenFilesIn::NewTab);
    app()->openFiles({a});
    app()->openFiles({b});
    QApplication::processEvents();

    MainWindow *mw = liveWindows().value(0);
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 2);

    int tab = -1;
    QCOMPARE(app()->windowForOpenPath(a, &tab), mw);
    QCOMPARE(tab, 0);
    QCOMPARE(app()->windowForOpenPath(b, &tab), mw);
    QCOMPARE(tab, 1);
}

void TestOpenDedup::lookupMissesUnopenedPath() {
    QVERIFY(m_dir.isValid());
    const QString open = writeTinyPdf(m_dir.filePath("miss_open.pdf"));
    const QString other = writeTinyPdf(m_dir.filePath("miss_other.pdf"));

    app()->openFiles({open});
    QApplication::processEvents();

    int tab = 7; // must be reset to -1 on a miss
    QCOMPARE(app()->windowForOpenPath(other, &tab), nullptr);
    QCOMPARE(tab, -1);
}

void TestOpenDedup::lookupIgnoresEmptyPath() {
    QVERIFY(m_dir.isValid());
    app()->openFiles({writeTinyPdf(m_dir.filePath("empty_probe.pdf"))});
    QApplication::processEvents();
    // An empty key has no identity and must never match — not even a
    // document that also has no path.
    QCOMPARE(app()->windowForOpenPath(QString()), nullptr);
}

void TestOpenDedup::lookupIgnoresUntitledDocuments() {
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(Qt::blue);
    QGuiApplication::clipboard()->setImage(img);
    app()->newFromClipboard();
    QApplication::processEvents();

    MainWindow *mw = liveWindows().value(0);
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 1);

    IDocument *doc = nullptr;
    QVERIFY(mw->documentAt(0, &doc));
    QVERIFY(doc);
    QVERIFY2(doc->isUntitled(), "clipboard import should be untitled");
    const QString tempPath = doc->filePath();
    QVERIFY2(!tempPath.isEmpty(),
             "an untitled import still has a temp backing path — which is "
             "exactly why isUntitled(), not path-emptiness, is the guard");

    // Even asking for that very temp path must not resolve to the
    // untitled document: its location is not one the user chose.
    QCOMPARE(app()->windowForOpenPath(tempPath), nullptr);
}

void TestOpenDedup::lookupMatchesThroughSymlink() {
#ifdef Q_OS_WIN
    QSKIP("Symlink creation on Windows needs developer mode / elevation.");
#else
    QVERIFY(m_dir.isValid());
    const QString target = writeTinyPdf(m_dir.filePath("sym_target.pdf"));
    const QString link = m_dir.filePath("sym_link.pdf");
    QVERIFY(QFile::link(target, link));

    app()->openFiles({target});
    QApplication::processEvents();

    MainWindow *mw = liveWindows().value(0);
    QVERIFY(mw);
    QCOMPARE(app()->windowForOpenPath(link), mw);
#endif
}

void TestOpenDedup::lookupPrefersOldestWindowHoldingTheFile() {
    QVERIFY(m_dir.isValid());
    const QString path = writeTinyPdf(m_dir.filePath("oldest.pdf"));

    // Build the pathological state the fix prevents — the same file in two
    // windows — by hand, so the tie-break is pinned rather than incidental.
    MainWindow *first = app()->ensureFreshWindow();
    MainWindow *second = app()->ensureFreshWindow();
    QVERIFY(first);
    QVERIFY(second);
    first->addDocument(app()->registry().open(path));
    second->addDocument(app()->registry().open(path));
    QApplication::processEvents();
    QCOMPARE(first->documentCount(), 1);
    QCOMPARE(second->documentCount(), 1);

    QCOMPARE(app()->windowForOpenPath(path), first);
}

// ----------------------------------------------------------- openFiles

void TestOpenDedup::reopenSurfacesInNewWindowMode() {
    QVERIFY(m_dir.isValid());
    const QString path = writeTinyPdf(m_dir.filePath("mode_neww.pdf"));

    app()->settings().setOpenFilesIn(OpenFilesIn::NewWindow);
    app()->openFiles({path});
    QApplication::processEvents();
    QCOMPARE(app()->windowCount(), 1);

    app()->openFiles({path});
    QApplication::processEvents();

    QCOMPARE(app()->windowCount(), 1);
    QCOMPARE(totalOpenDocuments(), 1);
}

void TestOpenDedup::reopenSurfacesInNewTabMode() {
    QVERIFY(m_dir.isValid());
    const QString path = writeTinyPdf(m_dir.filePath("mode_newtab.pdf"));

    app()->settings().setOpenFilesIn(OpenFilesIn::NewTab);
    app()->openFiles({path});
    QApplication::processEvents();
    QCOMPARE(app()->windowCount(), 1);

    app()->openFiles({path});
    QApplication::processEvents();

    QCOMPARE(app()->windowCount(), 1);
    QCOMPARE(totalOpenDocuments(), 1);
}

void TestOpenDedup::reopenSurfacesInSameWindowMode() {
    QVERIFY(m_dir.isValid());
    const QString path = writeTinyPdf(m_dir.filePath("mode_same.pdf"));

    app()->settings().setOpenFilesIn(OpenFilesIn::SameWindow);
    app()->openFiles({path});
    QApplication::processEvents();
    QCOMPARE(app()->windowCount(), 1);

    app()->openFiles({path});
    QApplication::processEvents();

    QCOMPARE(app()->windowCount(), 1);
    QCOMPARE(totalOpenDocuments(), 1);
}

void TestOpenDedup::reopenSelectsTheDocumentsOwnTab() {
    QVERIFY(m_dir.isValid());
    const QString a = writeTinyPdf(m_dir.filePath("tabsel_a.pdf"));
    const QString b = writeTinyPdf(m_dir.filePath("tabsel_b.pdf"));

    app()->settings().setOpenFilesIn(OpenFilesIn::NewTab);
    app()->openFiles({a});
    app()->openFiles({b});
    QApplication::processEvents();

    MainWindow *mw = liveWindows().value(0);
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 2);
    QCOMPARE(mw->currentDocumentIndex(), 1); // b's tab, just opened

    // Asking for `a` again must bring `a` forward, not merely raise the
    // window and leave the user staring at `b`.
    app()->openFiles({a});
    QApplication::processEvents();

    QCOMPARE(mw->documentCount(), 2);
    QCOMPARE(mw->currentDocumentIndex(), 0);
    IDocument *doc = nullptr;
    QVERIFY(mw->documentAt(mw->currentDocumentIndex(), &doc));
    QVERIFY(doc);
    QCOMPARE(canonicalPathKey(doc->filePath()), canonicalPathKey(a));
}

void TestOpenDedup::mixedBatchOpensOnlyTheNewFiles() {
    QVERIFY(m_dir.isValid());
    const QString already = writeTinyPdf(m_dir.filePath("mix_open.pdf"));
    const QString newB = writeTinyPdf(m_dir.filePath("mix_b.pdf"));
    const QString newC = writeTinyPdf(m_dir.filePath("mix_c.pdf"));

    app()->settings().setOpenFilesIn(OpenFilesIn::NewWindow);
    app()->openFiles({already});
    QApplication::processEvents();
    QCOMPARE(app()->windowCount(), 1);

    app()->openFiles({newB, newC, already});
    QApplication::processEvents();

    // +2, not +3: the third path was surfaced, not opened.
    QCOMPARE(app()->windowCount(), 3);
    QCOMPARE(totalOpenDocuments(), 3);
}

void TestOpenDedup::repeatedPathWithinOneBatchOpensOnce() {
    QVERIFY(m_dir.isValid());
    const QString path = writeTinyPdf(m_dir.filePath("repeat.pdf"));

    app()->settings().setOpenFilesIn(OpenFilesIn::NewWindow);
    app()->openFiles({path, path, path});
    QApplication::processEvents();

    QCOMPARE(app()->windowCount(), 1);
    QCOMPARE(totalOpenDocuments(), 1);
}

void TestOpenDedup::allAlreadyOpenBatchSpawnsNoWindow() {
    QVERIFY(m_dir.isValid());
    const QString a = writeTinyPdf(m_dir.filePath("allopen_a.pdf"));
    const QString b = writeTinyPdf(m_dir.filePath("allopen_b.pdf"));

    app()->settings().setOpenFilesIn(OpenFilesIn::NewWindow);
    app()->openFiles({a, b});
    QApplication::processEvents();
    QCOMPARE(app()->windowCount(), 2);

    // Everything in the batch is already open: nothing may be created,
    // and in particular no empty window may be spawned to receive it.
    app()->openFiles({a, b});
    QApplication::processEvents();

    QCOMPARE(app()->windowCount(), 2);
    QCOMPARE(totalOpenDocuments(), 2);
}

void TestOpenDedup::imageBatchWithOneAlreadyOpenDoesNotSpawnAnEmptyBatchWindow() {
    QVERIFY(m_dir.isValid());
    const QString img1 = writeTinyPng(m_dir.filePath("batch_1.png"));
    const QString img2 = writeTinyPng(m_dir.filePath("batch_2.png"));

    app()->settings().setOpenFilesIn(OpenFilesIn::NewWindow);
    app()->openFiles({img1, img2});
    QApplication::processEvents();
    // Two images opened together share one window (the tab strip).
    QCOMPARE(app()->windowCount(), 1);
    QCOMPARE(totalOpenDocuments(), 2);

    // Re-asking for both must not build a second batch window that then
    // receives nothing — the image-batch decision reads the fresh paths.
    app()->openFiles({img1, img2});
    QApplication::processEvents();

    QCOMPARE(app()->windowCount(), 1);
    QCOMPARE(totalOpenDocuments(), 2);
}

// CF-5 interaction. The empty-launch-window reuse candidate is a
// consume-once resource reserved for a document that is genuinely being
// opened. A file that turns out to be already open must NOT spend it —
// otherwise the empty window is claimed, filled with nothing, and the
// orphan CF-5 exists to prevent comes back.
void TestOpenDedup::surfacingDoesNotConsumeTheEmptyWindowReuseCandidate() {
    QVERIFY(m_dir.isValid());
    const QString open = writeTinyPdf(m_dir.filePath("cf5_open.pdf"));

    app()->settings().setOpenFilesIn(OpenFilesIn::NewWindow);
    app()->openFiles({open});
    QApplication::processEvents();
    QCOMPARE(app()->windowCount(), 1);
    MainWindow *holder = liveWindows().value(0);
    QVERIFY(holder);
    QCOMPARE(holder->documentCount(), 1);

    // An empty window alongside the document window — the CF-5 candidate.
    MainWindow *empty = app()->ensureFreshWindow();
    QVERIFY(empty);
    QCOMPARE(empty->documentCount(), 0);
    QCOMPARE(app()->windowCount(), 2);
    QPointer<MainWindow> emptyGuard(empty);

    // Ask for the already-open file. Nothing is created, and the empty
    // window is left untouched: still tracked, still alive, still empty.
    //
    // Those three assertions ARE the invariant, observed directly. An
    // earlier draft instead tried to prove it indirectly — re-activate the
    // empty window, open a genuinely new file, and check that file landed
    // in it rather than in a third window. That was wrong twice over:
    //
    //  1. It observed the invariant through activation state. CF-5 picks
    //     its candidate from QApplication::activeWindow(), and the product
    //     code right there says why that is not dependable in this setting
    //     ("the launch-window case where offscreen/headless may not set an
    //     active window" — Application.cpp, pre-dating this PR). The Wine
    //     lane duly failed on that step and only that step: this was the
    //     single failing slot out of 22, and the only one reaching for
    //     activation. Review finding #2 of this PR refused to assert
    //     activation directly for exactly this reason; the same reasoning
    //     simply was not carried across to asserting it *indirectly*.
    //
    //  2. It could not observe anything the direct assertions miss.
    //     `reuseCandidate` is a LOCAL in openFiles(), recomputed from
    //     scratch on every call — there is no cross-call state that a
    //     surface could "spend". A candidate is eligible iff its window is
    //     still tracked and still empty, which is precisely what is
    //     asserted below. The reuse mechanism itself is pre-existing CF-5
    //     behaviour with its own coverage in
    //     tests/uat/test_uat_empty_state.cpp (uat_empty_006/008/009/010),
    //     and re-testing it here bought nothing.
    //
    // Verified by mutation: making the surfaced path call
    // takeReuseOrFresh() fails the documentCount() assertion below, so this
    // still bites on the regression it exists to catch.
    app()->openFiles({open});
    QApplication::processEvents();
    QCOMPARE(app()->windowCount(), 2);
    QVERIFY2(!emptyGuard.isNull(), "the empty window must not be torn down");
    QCOMPARE(app()->windows().contains(empty), true);
    QCOMPARE(empty->documentCount(), 0);

    // And the surfaced document went to the window that already held it,
    // not into the empty one.
    QCOMPARE(app()->windowForOpenPath(open), holder);
}

void TestOpenDedup::untitledImportsAreNeverDeduped() {
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(Qt::green);

    app()->settings().setOpenFilesIn(OpenFilesIn::NewTab);
    QGuiApplication::clipboard()->setImage(img);
    app()->newFromClipboard();
    QApplication::processEvents();
    QGuiApplication::clipboard()->setImage(img);
    app()->newFromClipboard();
    QApplication::processEvents();

    // Two pastes of identical pixels are still two documents.
    QCOMPARE(totalOpenDocuments(), 2);
}

void TestOpenDedup::distinctFilesStillOpenSeparately() {
    QVERIFY(m_dir.isValid());
    const QString a = writeTinyPdf(m_dir.filePath("distinct_a.pdf"));
    const QString b = writeTinyPdf(m_dir.filePath("distinct_b.pdf"));

    app()->settings().setOpenFilesIn(OpenFilesIn::NewWindow);
    app()->openFiles({a});
    app()->openFiles({b});
    QApplication::processEvents();

    // The dedup must not become a "one document ever" rule.
    QCOMPARE(app()->windowCount(), 2);
    QCOMPARE(totalOpenDocuments(), 2);
}

// Custom main: sandbox HOME before Application is constructed so
// Settings/RecentFiles never touch the real config dir. QSettings is
// forced to IniFormat for the reason spelled out in
// tests/test_image_scale.cpp's main() (NativeFormat on macOS bypasses the
// HOME sandbox entirely).
int main(int argc, char **argv) {
    // Unbuffered stdout/stderr, same as tests/test_image_scale.cpp's main().
    // Under Wine these streams are fully buffered, so a process that dies
    // before exiting normally loses every line it ever printed — ctest then
    // reports the failure with a completely EMPTY --output-on-failure block
    // and there is nothing to debug from. That is exactly how this file's
    // first Wine failure presented. Flushing per line means the next one
    // shows its assertion.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    QSettings::setDefaultFormat(QSettings::IniFormat);
    trailer::Application app(argc, argv);
    TestOpenDedup tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_open_dedup.moc"
