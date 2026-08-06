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
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w)) {
            mw->setCloseResponseForTesting(MainWindow::CloseResponse::Discard);
            mw->close();
        }
    }
    QApplication::processEvents();
    QVERIFY(app());
    app()->settings().setOpenFilesIn(OpenFilesIn::NewWindow);
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
    const QString fresh = writeTinyPdf(m_dir.filePath("cf5_fresh.pdf"));

    app()->settings().setOpenFilesIn(OpenFilesIn::NewWindow);
    app()->openFiles({open});
    QApplication::processEvents();
    QCOMPARE(app()->windowCount(), 1);

    // An empty window alongside the document window — the CF-5 candidate.
    MainWindow *empty = app()->ensureFreshWindow();
    QVERIFY(empty);
    QCOMPARE(empty->documentCount(), 0);
    QCOMPARE(app()->windowCount(), 2);
    QPointer<MainWindow> emptyGuard(empty);

    // Ask for the already-open file. Nothing is created — and crucially
    // the empty window is left untouched, still empty and still available.
    app()->openFiles({open});
    QApplication::processEvents();
    QCOMPARE(app()->windowCount(), 2);
    QVERIFY(!emptyGuard.isNull());
    QCOMPARE(empty->documentCount(), 0);

    // Proof the candidate was never spent: the next genuinely new file
    // lands in the empty window rather than spawning a third.
    //
    // The explicit re-activation is not a workaround — it restores the
    // precondition CF-5 actually states. Its candidate is "the ACTIVE
    // window, if it is one of ours and empty" (or the sole window), and
    // surfacing above legitimately made the document window active,
    // because that is exactly what the user asked for. So the empty
    // window has to be frontmost again for CF-5 to consider it at all;
    // what this asserts is that it is still *eligible*, i.e. still empty
    // and unconsumed.
    empty->activateWindow();
    QApplication::processEvents();
    app()->openFiles({fresh});
    QApplication::processEvents();
    QCOMPARE(app()->windowCount(), 2);
    QCOMPARE(empty->documentCount(), 1);
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
