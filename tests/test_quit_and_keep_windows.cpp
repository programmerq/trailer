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

#include "annotation/Annotation.h"
#include "annotation/AnnotationJson.h"
#include "app/Application.h"
#include "document/ImageAdapter.h"
#include "document/PdfAdapter.h"
#include "ui/MainWindow.h"
#include "util/TempPath.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QPainter>
#include <QPageSize>
#include <QPdfWriter>
#include <QRectF>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <memory>
#include <optional>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace trailer;

namespace {

// True only when the process runs under the Wine emulator. Canonical detection
// (matches tests/test_discard_file_integrity.cpp): Wine exports
// ntdll!wine_get_version; real Windows and Linux/macOS return false. Used to
// QSKIP the two cases that delete a file still held open by a live document —
// see kWineOpenFileDeleteSkip and docs/backlog/2026-07-21-wine-keep-restore-
// file-move-open-handle.md.
bool runningUnderWine() {
#ifdef Q_OS_WIN
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
#else
    return false;
#endif
}

// Why these two round-trips skip under Wine: both simulate the user MOVING or
// DELETING the kept file between ⌥⌘Q and relaunch by QFile::remove()-ing it
// while the ORIGINAL document is still alive (performQuit is a no-op stub in
// these tests, so the pre-quit window/document is never torn down). On the
// Windows/Wine file model a file held open by a live handle cannot be deleted,
// so QFile::remove() returns false; POSIX unlink-of-an-open-file succeeds, so
// Linux runs the full assertion set. For the backing-file case the qpdf editor
// was additionally adopted from the annotation-sweep WORKER thread, whose
// handle Wine does not release even on main-thread teardown (the #90 cross-
// thread-handle limitation). The real keep flow releases the document when the
// process exits at ⌥⌘Q, before the user moves the file. Skipped under Wine (our
// only automated Windows signal; the native-Windows job is disabled); asserted
// in full on Linux. See docs/backlog/2026-07-21-wine-keep-restore-file-move-
// open-handle.md.
constexpr const char *kWineOpenFileDeleteSkip =
    "Wine/Windows: QFile::remove() of the kept file fails while the original "
    "document still holds it open (the harness keeps the pre-quit document alive "
    "via a no-op performQuit); POSIX unlink-of-open succeeds, so Linux asserts "
    "this in full. See docs/backlog/2026-07-21-wine-keep-restore-file-move-open-"
    "handle.md.";

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

// A minimal non-image, dirty document standing in for a PDF with unsaved
// annotations: dirty, editable, titled, and — crucially — NOT an
// ImageDocument, so the kept-windows capture cannot draft it losslessly and
// must fall back to the per-doc prompt (MAJOR 1). save() clears the dirty
// flag so a Save response resolves it.
class FakeDirtyPdfDoc : public IDocument {
  public:
    explicit FakeDirtyPdfDoc(QString path) : m_path(std::move(path)) {}
    QString displayName() const override { return QStringLiteral("fake.pdf"); }
    QString filePath() const override { return m_path; }
    QWidget *createView(QWidget *parent) override { return new QWidget(parent); }
    DocumentType documentType() const override { return DocumentType::Pdf; }
    bool supportsEditing() const override { return true; }
    bool isDirty() const override { return m_dirty; }
    bool save(const QString &newPath = {}) override {
        if (!newPath.isEmpty())
            m_path = newPath;
        m_dirty = false;
        ++m_saveCount;
        return true;
    }
    int saveCount() const { return m_saveCount; }

  private:
    QString m_path;
    bool m_dirty = true;
    int m_saveCount = 0;
};

// Write a minimal valid one-page PDF to `path` so a real PdfDocument can
// open it. Mirrors the QPdfWriter fixture pattern in test_adapters.cpp /
// test_pdf_editor.cpp — cheaper than shipping a binary PDF fixture.
QString writeTinyPdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter painter(&writer);
    painter.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter, QStringLiteral("Keep me"));
    painter.end();
    return path;
}

// A fully-populated Annotation so a lossy JSON round-trip is caught: every
// field carries a distinctive non-default value.
Annotation makeRichAnnotation() {
    Annotation a;
    a.id = 7;
    a.page = 2;
    a.type = AnnotationType::Ink;
    a.bounds = QRectF(1.5, 2.5, 30.0, 40.0);
    a.points = {QPointF(1.0, 2.0), QPointF(3.5, 4.25), QPointF(9.0, 8.0)};
    a.pressures = {0.1f, 0.5f, 0.9f};
    a.quads = {QRectF(0.0, 0.0, 5.0, 6.0), QRectF(7.0, 8.0, 9.0, 10.0)};
    a.text = QStringLiteral("hello é world");
    a.imagePath = QStringLiteral("/tmp/sig.png");
    a.style.stroke = QColor(10, 20, 30, 200);
    a.style.fill = QColor(40, 50, 60, 128);
    a.style.strokeWidth = 3.75;
    a.style.fontPointSize = 18;
    a.style.dash = DashStyle::Dotted;
    a.style.fontFamily = QStringLiteral("Georgia");
    a.style.fontWeight = 75;
    a.style.zoomFactor = 4.5;
    return a;
}

// Open a REAL one-page PDF from `path` into a new window and give it an
// unsaved annotation so it is annotation-dirty (isDirty(), NOT structural).
// Returns the raw doc pointer (owned by the window).
PdfDocument *addAnnotationDirtyPdfWindow(Application *app, const QString &path,
                                         MainWindow **outWin = nullptr) {
    MainWindow *win = app->ensureFreshWindow();
    auto doc = std::make_unique<PdfDocument>(path);
    PdfDocument *ptr = doc.get();
    Annotation a;
    a.type = AnnotationType::Rectangle;
    a.page = 0;
    a.bounds = QRectF(10.0, 12.0, 40.0, 22.0);
    a.style.stroke = QColor(200, 30, 30);
    ptr->annotations()->add(a); // the changed hook marks the doc dirty
    win->addDocument(std::move(doc));
    if (outWin)
        *outWin = win;
    return ptr;
}

// Open a REAL one-page PDF from `path` into a new window (no edits). Returns
// the raw doc pointer (owned by the window) for structural edits + identity.
PdfDocument *addPdfWindow(Application *app, const QString &path,
                          MainWindow **outWin = nullptr) {
    MainWindow *win = app->ensureFreshWindow();
    auto doc = std::make_unique<PdfDocument>(path);
    PdfDocument *ptr = doc.get();
    win->addDocument(std::move(doc));
    if (outWin)
        *outWin = win;
    return ptr;
}

// Open a REAL on-disk image, then DELETE its backing file underneath the open
// document (CF-7 "deleted underneath"). The doc stays CLEAN (never edited) and
// TITLED, but its file is gone, so its in-memory raster is the last copy:
// isDirty()==false, isUntitled()==false, externalChangeState()==Deleted, and
// hasUnsavedWork()==true. Returns the raw doc pointer (owned by the window).
// The caller asserts the deleted-underneath preconditions via QVERIFY (a
// helper cannot, since QVERIFY returns void). `path` must be a writable
// location whose file this helper creates and then removes.
ImageDocument *addDeletedUnderneathImageWindow(Application *app, const QImage &image,
                                               const QString &path,
                                               MainWindow **outWin = nullptr) {
    image.save(path, "PNG");
    MainWindow *win = app->ensureFreshWindow();
    auto doc = std::make_unique<ImageDocument>(path); // loads + captures baseline
    ImageDocument *ptr = doc.get();
    win->addDocument(std::move(doc));
    // Yank the backing file out from under the open, clean, titled doc.
    QFile::remove(path);
    if (outWin)
        *outWin = win;
    return ptr;
}

// The restored doc is a NEW pointer distinct from `original`; find it across
// all windows. Mirrors the inline scan the image round-trip tests use.
PdfDocument *findRestoredPdf(Application *app, IDocument *original) {
    for (MainWindow *w : app->windows()) {
        if (!w)
            continue;
        for (int i = 0; i < w->documentCount(); ++i) {
            IDocument *d = nullptr;
            if (w->documentAt(i, &d) == 1 && d && d != original)
                if (auto *p = dynamic_cast<PdfDocument *>(d))
                    return p;
        }
    }
    return nullptr;
}

QByteArray sha256Of(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
}

// The `kind` string the draft store wrote for the first doc of the first
// window, read straight from the on-disk manifest. Lets a test assert the
// descriptor kind (structural-draft vs annotated-path) that was persisted.
QString firstDocManifestKind(Application *app) {
    const QString manifest =
        QDir::cleanPath(app->sessionDraftStore().storeDir() + "/manifest.json");
    QFile f(manifest);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray windows = doc.object().value("windows").toArray();
    if (windows.isEmpty())
        return {};
    const QJsonArray docs = windows.first().toObject().value("docs").toArray();
    if (docs.isEmpty())
        return {};
    return docs.first().toObject().value("kind").toString();
}

// Structural state of a restored PDF's first page. Saves the live editor to a
// throwaway temp (a DIFFERENT file, so no same-file/Wine handle-release issue)
// and reads /Rotate + /CropBox + page count via raw qpdf — the assertion style
// of test_pdf_editor.cpp::rotatePageCommandIsReversible. NOTE: save() re-points
// the doc's Save target to the temp, so callers must assert filePath() BEFORE
// calling this.
struct FirstPageInfo {
    bool ok = false;
    int pageCount = 0;
    int rotate = 0;
    bool hasCropBox = false;
    QRectF cropBox; // [x0,y0] origin + w,h in PDF points
    QRectF mediaBox;
};
FirstPageInfo inspectRestoredFirstPage(PdfDocument *doc) {
    FirstPageInfo info;
    ScopedTempFile tmp(QStringLiteral("keep_inspect_XXXXXX.pdf"));
    if (!tmp.isValid() || !doc->save(tmp.path()))
        return info;
    try {
        QPDF pdf;
        pdf.processFile(tmp.path().toLocal8Bit().constData());
        auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
        info.pageCount = static_cast<int>(pages.size());
        if (pages.empty()) {
            info.ok = true;
            return info;
        }
        QPDFPageObjectHelper page = pages.front();
        QPDFObjectHandle obj = page.getObjectHandle();
        QPDFObjectHandle rot = obj.getKey("/Rotate");
        info.rotate = rot.isInteger() ? static_cast<int>(rot.getIntValue()) : 0;
        auto boxToRect = [](QPDFObjectHandle box) -> QRectF {
            const double x0 = box.getArrayItem(0).getNumericValue();
            const double y0 = box.getArrayItem(1).getNumericValue();
            const double x1 = box.getArrayItem(2).getNumericValue();
            const double y1 = box.getArrayItem(3).getNumericValue();
            return QRectF(x0, y0, x1 - x0, y1 - y0);
        };
        QPDFObjectHandle media = page.getMediaBox(/*copy_if_shared=*/true);
        if (media.isArray() && media.getArrayNItems() >= 4)
            info.mediaBox = boxToRect(media);
        QPDFObjectHandle crop = obj.getKey("/CropBox");
        if (crop.isArray() && crop.getArrayNItems() >= 4) {
            info.hasCropBox = true;
            info.cropBox = boxToRect(crop);
        }
        info.ok = true;
    } catch (const std::exception &) {
        info.ok = false;
    }
    return info;
}

// The /Rotate of every page, in order, of a restored PDF. Saves the live
// editor to a throwaway temp (a DIFFERENT file, so no same-file handle issue)
// and reads each page via raw qpdf. Used to prove a page MOVE reordered the
// pages (a rotated marker page lands at its new index). NOTE: save()
// re-points the doc's Save target to the temp, so callers must assert
// filePath() BEFORE calling this. Returns empty on failure.
std::vector<int> restoredPageRotations(PdfDocument *doc) {
    std::vector<int> rots;
    ScopedTempFile tmp(QStringLiteral("keep_rots_XXXXXX.pdf"));
    if (!tmp.isValid() || !doc->save(tmp.path()))
        return rots;
    try {
        QPDF pdf;
        pdf.processFile(tmp.path().toLocal8Bit().constData());
        for (QPDFPageObjectHelper &page : QPDFPageDocumentHelper(pdf).getAllPages()) {
            QPDFObjectHandle rot = page.getObjectHandle().getKey("/Rotate");
            rots.push_back(rot.isInteger() ? static_cast<int>(rot.getIntValue()) : 0);
        }
    } catch (const std::exception &) {
        rots.clear();
    }
    return rots;
}

// Write a multi-page PDF (`pages` one-page-each) to `path`.
QString writeMultiPagePdf(const QString &path, int pages, const QString &label) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter painter(&writer);
    for (int i = 0; i < pages; ++i) {
        painter.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter,
                         QStringLiteral("%1 %2").arg(label).arg(i + 1));
        if (i < pages - 1)
            writer.newPage();
    }
    painter.end();
    return path;
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
    void restorePreservesDevicePixelRatioAndCaptureOrigin();
    void keepWindowsDirtyNonImageCancelAborts();
    void keepWindowsDirtyNonImageSaveResolves();
    void keepWindowsFailedSaveDoesNotSilentlyQuit();
    void keepWindowsUnencodableDocNotSilentlyDropped();
    void normalQuitCancelAborts();
    void normalQuitDiscardProceeds();
    void normalQuitSaveProceeds();
    void collectDirtyDocsIsCurrentFirstAndDeduped();
    void keepWindowsActionHasShortcutAndRoutes();
    void osProbeDoesNotFlipExplicitQuitCommands();
    void keepWindowsAnnotationDirtyPdfNeverPrompts();
    void keepWindowsAnnotationDirtyPdfRestoresEditableAndDirty();
    void keepWindowsAnnotationDirtyPdfUsesAnnotatedPathKind();
    void keepWindowsStructuralPdfPersistsWithoutPrompt();
    void keepWindowsStructuralPdfUsesStructuralDraftKind();
    void keepWindowsStructuralPdfWithPendingRedactionPrompts();
    void keepWindowsStructuralPdfSnapshotFailurePrompts();
    void restoreRotatedPdfKeepsRotationDirtyAndOriginalPath();
    void restoreDeletedPagePdfKeepsPageCountDirtyAndOriginalPath();
    void restoreCroppedPdfKeepsCropBoxDirtyAndOriginalPath();
    void restoreMovedPagePdfKeepsOrderDirtyAndOriginalPath();
    void restoreInsertedPagesPdfSurvivesWithoutSource();
    void restoreStructuralPdfMovedOriginalReturnsUntitledDirty();
    void restoreAnnotatedPdfDeletedBackingReturnsUntitledDirty();
    void keepWindowsAnnotatedPdfDeletedBackingSnapshotFailurePrompts();
    void keepWindowsClearsStaleRecoverySidecar();
    void restoreCombinedStructuralAndAnnotationKeepsBothEditable();
    void annotationJsonRoundTripsAllFields();
    void keepWindowsDirtyTitledImageRestoresDirtyWithPath();
    void deletedUnderneathDocIsPromptedOnNormalQuit();
    void keepWindowsKeepsDeletedDocWithoutWritingBackingFile();

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

void TestQuitAndKeepWindows::restorePreservesDevicePixelRatioAndCaptureOrigin() {
    // A HiDPI screenshot: dpr 2, capture-origin. A PNG blob does not carry
    // Qt's dpr, so without the manifest field the restored doc would report
    // dpr 1 and render double-sized. NOTE: QImage::operator== ignores dpr,
    // so we assert devicePixelRatio() and isCaptureOrigin() EXPLICITLY.
    QImage hidpi = makeKnownImage(20, 20, 5);
    hidpi.setDevicePixelRatio(2.0);

    MainWindow *win = m_app->ensureFreshWindow();
    auto doc = std::make_unique<ImageDocument>(QString());
    doc->setImageForTest(hidpi, /*captureOrigin=*/true);
    doc->markUntitled();
    ImageDocument *original = doc.get();
    win->addDocument(std::move(doc));
    QCOMPARE(original->image().devicePixelRatio(), 2.0);
    QVERIFY(original->isCaptureOrigin());

    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QCOMPARE(quitCount, 1);
    QVERIFY(m_app->sessionDraftStore().hasSession());

    QVERIFY(m_app->restoreKeptWindows());

    ImageDocument *restored = nullptr;
    for (MainWindow *w : m_app->windows()) {
        if (!w)
            continue;
        for (int i = 0; i < w->documentCount(); ++i) {
            IDocument *d = nullptr;
            if (w->documentAt(i, &d) == 1 && d && d != original)
                if (auto *img = dynamic_cast<ImageDocument *>(d))
                    restored = img;
        }
    }
    QVERIFY(restored);
    // The headline assertions: dpr and capture-origin survived the round-trip.
    QCOMPARE(restored->image().devicePixelRatio(), 2.0);
    QVERIFY(restored->isCaptureOrigin());
}

void TestQuitAndKeepWindows::keepWindowsDirtyNonImageCancelAborts() {
    // A dirty non-image (PDF-like) doc cannot be drafted losslessly, so
    // KeepWindows must fall back to the per-doc prompt rather than silently
    // storing it as a clean path ref (which drops its unsaved edits). With a
    // Cancel response the whole quit aborts — nothing written, nothing quit.
    MainWindow *win = m_app->ensureFreshWindow();
    auto doc = std::make_unique<FakeDirtyPdfDoc>(QStringLiteral("/tmp/does-not-matter.pdf"));
    win->addDocument(std::move(doc));
    win->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);

    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    const bool proceeded = m_app->requestQuit(QuitMode::KeepWindows);

    QVERIFY(!proceeded);                                // prompt Cancel aborted
    QCOMPARE(quitCount, 0);                             // did NOT silently quit
    QVERIFY(!m_app->sessionDraftStore().hasSession());  // nothing written
    QCOMPARE(win->documentCount(), 1);                  // doc kept
}

void TestQuitAndKeepWindows::keepWindowsDirtyNonImageSaveResolves() {
    // With a Save response the dirty non-image doc is resolved (saved) before
    // the quit proceeds — no silent loss, and the quit completes.
    MainWindow *win = m_app->ensureFreshWindow();
    auto doc = std::make_unique<FakeDirtyPdfDoc>(QStringLiteral("/tmp/resolved.pdf"));
    FakeDirtyPdfDoc *ptr = doc.get();
    win->addDocument(std::move(doc));
    win->setCloseResponseForTesting(MainWindow::CloseResponse::Save);

    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QCOMPARE(quitCount, 1);
    QVERIFY(!ptr->isDirty());            // the prompt's Save resolved the edits
    QCOMPARE(ptr->saveCount(), 1);       // save() was actually invoked
}

void TestQuitAndKeepWindows::keepWindowsFailedSaveDoesNotSilentlyQuit() {
    // MAJOR 3(b): if the draft-store save fails, KeepWindows must NOT silently
    // performQuit (which would lose the still-unsaved drafts). It falls back
    // to the Normal prompt; a Cancel there aborts the quit.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Park a regular FILE where the store directory's parent must be, so the
    // atomic save's mkpath(staging) fails no matter the uid.
    const QString blocker = dir.path() + "/blocker";
    QFile bf(blocker);
    QVERIFY(bf.open(QIODevice::WriteOnly));
    bf.write("x");
    bf.close();
    m_app->setSessionDraftStoreDirForTesting(blocker + "/session-drafts");

    MainWindow *win = nullptr;
    addUntitledImageWindow(m_app, makeKnownImage(), &win); // a draftable doc
    // The fallback prompt sees a Cancel → the quit aborts.
    win->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);

    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    const bool proceeded = m_app->requestQuit(QuitMode::KeepWindows);

    QVERIFY(!proceeded);      // did not silently quit — fell back + aborted
    QCOMPARE(quitCount, 0);   // performQuit NOT called
    QCOMPARE(win->documentCount(), 1);

    // Restore the store to a clean sandbox location so cleanup()/other slots
    // are unaffected by the sabotaged directory.
    m_app->setSessionDraftStoreDirForTesting(dir.path() + "/clean-drafts");
    m_app->sessionDraftStore().clear();
}

void TestQuitAndKeepWindows::keepWindowsUnencodableDocNotSilentlyDropped() {
    // MINOR 6: a dirty/untitled image doc whose raster is NULL cannot be
    // PNG-encoded. It must NOT be silently dropped (stored as an empty blob
    // that restore skips). canDraftForKeep() gates on a non-null image, so
    // such a doc falls back to the per-doc prompt; a Cancel aborts the quit.
    MainWindow *win = m_app->ensureFreshWindow();
    auto doc = std::make_unique<ImageDocument>(QString()); // no image set → null raster
    doc->markUntitled();
    win->addDocument(std::move(doc));
    win->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);

    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    const bool proceeded = m_app->requestQuit(QuitMode::KeepWindows);

    QVERIFY(!proceeded);                                // prompt Cancel aborted
    QCOMPARE(quitCount, 0);                             // not silently dropped/quit
    QVERIFY(!m_app->sessionDraftStore().hasSession());  // nothing written
    QCOMPARE(win->documentCount(), 1);
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
    IDocument *raw1 = d1.get();
    IDocument *raw2 = d2.get();
    win->addDocument(std::move(d1));
    win->addDocument(std::move(d2));

    const std::vector<IDocument *> dirty = win->collectDirtyDocsForQuit();
    QCOMPARE(static_cast<int>(dirty.size()), 2);
    // Current-document-first: the most recently added tab (d2) is current, so
    // it must sort ahead of the earlier one — asserted against the ACTUAL doc
    // identity, not a second call to the same function (which was tautological).
    QCOMPARE(dirty.front(), raw2);
    QCOMPARE(dirty.back(), raw1);
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

void TestQuitAndKeepWindows::osProbeDoesNotFlipExplicitQuitCommands() {
    // Decoupling regression (owner finding, 2026-07-19): the explicit ⌘Q /
    // ⌥⌘Q commands are NOT flipped by the OS NSQuitAlwaysKeepsWindows probe.
    // Whatever the probe reports, KeepWindows (⌥⌘Q) NEVER prompts and Normal
    // (⌘Q) ALWAYS prompts for a dirty document. Assert BOTH probe states.
    for (bool probe : {false, true}) {
        // --- ⌥⌘Q (KeepWindows): never prompts, whatever the probe says. ---
        MainWindow *keepWin = nullptr;
        addUntitledImageWindow(m_app, makeKnownImage(), &keepWin);
        m_app->setQuitKeepsWindowsProbeForTesting([probe] { return probe; });
        // A Cancel response would abort IF a prompt were raised — it must not be.
        keepWin->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
        int keepQuit = 0;
        m_app->setPerformQuitForTesting([&] { ++keepQuit; });
        QVERIFY(m_app->requestQuit(QuitMode::KeepWindows)); // no prompt → proceeds
        QCOMPARE(keepQuit, 1);
        QVERIFY(m_app->sessionDraftStore().hasSession());
        m_app->sessionDraftStore().clear();
        closeAllWindows(m_app);

        // --- ⌘Q (Normal): always prompts; Cancel aborts, whatever the probe. ---
        MainWindow *normWin = nullptr;
        addUntitledImageWindow(m_app, makeKnownImage(), &normWin);
        m_app->setQuitKeepsWindowsProbeForTesting([probe] { return probe; });
        normWin->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
        int normQuit = 0;
        m_app->setPerformQuitForTesting([&] { ++normQuit; });
        QVERIFY(!m_app->requestQuit(QuitMode::Normal)); // prompt Cancel aborts
        QCOMPARE(normQuit, 0);
        QVERIFY(!m_app->sessionDraftStore().hasSession()); // nothing written
        closeAllWindows(m_app);
    }
}

void TestQuitAndKeepWindows::keepWindowsAnnotationDirtyPdfNeverPrompts() {
    // The owner's headline case: a PDF with UNSAVED ANNOTATIONS must ride ⌥⌘Q
    // with NO prompt — regardless of the OS probe — and be captured for
    // restore (not resolved away by a Save/Discard prompt).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/annotated.pdf";
    writeTinyPdf(path);

    for (bool probe : {false, true}) {
        MainWindow *win = nullptr;
        PdfDocument *pdf = addAnnotationDirtyPdfWindow(m_app, path, &win);
        QVERIFY(pdf->isDirty());               // annotation edit made it dirty
        QVERIFY(!pdf->hasStructuralEdits());    // …and it is annotation-only
        // A Cancel would abort IF a prompt were raised — it must not be.
        win->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
        m_app->setQuitKeepsWindowsProbeForTesting([probe] { return probe; });

        int quitCount = 0;
        m_app->setPerformQuitForTesting([&] { ++quitCount; });
        QVERIFY(m_app->requestQuit(QuitMode::KeepWindows)); // no prompt → proceeds
        QCOMPARE(quitCount, 1);
        QVERIFY(m_app->sessionDraftStore().hasSession());   // captured, not prompted
        m_app->sessionDraftStore().clear();
        closeAllWindows(m_app);
    }
}

void TestQuitAndKeepWindows::keepWindowsAnnotationDirtyPdfRestoresEditableAndDirty() {
    // Round-trip: an annotation-dirty PDF kept via ⌥⌘Q returns from the store
    // reopened from its ORIGINAL path, with the unsaved annotation re-applied
    // as an editable object and the document STILL DIRTY (not saved/discarded).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/annotated.pdf";
    writeTinyPdf(path);

    PdfDocument *original = addAnnotationDirtyPdfWindow(m_app, path);
    const int originalCount = original->annotations()->count();
    QVERIFY(originalCount >= 1);

    m_app->setPerformQuitForTesting([] {});
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QVERIFY(m_app->sessionDraftStore().hasSession());

    QVERIFY(m_app->restoreKeptWindows());

    PdfDocument *restored = nullptr;
    for (MainWindow *w : m_app->windows()) {
        if (!w)
            continue;
        for (int i = 0; i < w->documentCount(); ++i) {
            IDocument *d = nullptr;
            if (w->documentAt(i, &d) == 1 && d && d != original)
                if (auto *p = dynamic_cast<PdfDocument *>(d))
                    restored = p;
        }
    }
    QVERIFY(restored);
    QCOMPARE(restored->filePath(), path);                 // reopened from disk
    QVERIFY(restored->isDirty());                          // returned STILL DIRTY
    QCOMPARE(restored->annotations()->count(), originalCount); // annotation kept
    // Editable: the restored annotation carries its geometry (not flattened
    // into page content), so the store holds it as a live object.
    const std::vector<Annotation> &anns = restored->annotations()->annotations();
    QVERIFY(!anns.empty());
    QCOMPARE(anns.front().type, AnnotationType::Rectangle);
    QCOMPARE(anns.front().bounds, QRectF(10.0, 12.0, 40.0, 22.0));
    // The store is a one-shot: consumed after a successful restore.
    QVERIFY(!m_app->sessionDraftStore().hasSession());
}

void TestQuitAndKeepWindows::keepWindowsAnnotationDirtyPdfUsesAnnotatedPathKind() {
    // The default path is unchanged for an annotation-ONLY dirty PDF: it is
    // still persisted as an AnnotatedPath descriptor (on-disk path + JSON
    // annotations), NOT the new StructuralDraft blob kind. Only structural
    // edits opt into the full-document blob.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/annotated.pdf";
    writeTinyPdf(path);

    PdfDocument *pdf = addAnnotationDirtyPdfWindow(m_app, path);
    QVERIFY(pdf->isDirty());
    QVERIFY(!pdf->hasStructuralEdits());

    m_app->setPerformQuitForTesting([] {});
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QVERIFY(m_app->sessionDraftStore().hasSession());
    QCOMPARE(firstDocManifestKind(m_app), QStringLiteral("annotated-path"));
}

void TestQuitAndKeepWindows::keepWindowsStructuralPdfPersistsWithoutPrompt() {
    // NEW behaviour (structural-pdf-keep-fidelity): a PDF with STRUCTURAL edits
    // (rotate/delete/crop) now rides ⌥⌘Q with NO prompt, exactly like an
    // annotation-only or image edit — its edited bytes are captured to the
    // draft store as a StructuralDraft blob. This REPLACES the old
    // fall-back-to-prompt residual (which asserted the opposite).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/structural.pdf";
    writeTinyPdf(path);

    MainWindow *win = nullptr;
    PdfDocument *pdf = addPdfWindow(m_app, path, &win);
    pdf->rotatePage(0, 90); // a structural (qpdf page-graph) edit
    QVERIFY(pdf->isDirty());
    QVERIFY(pdf->hasStructuralEdits());

    // A Cancel response WOULD abort if a prompt were raised — it must not be.
    win->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    const bool proceeded = m_app->requestQuit(QuitMode::KeepWindows);

    QVERIFY(proceeded);                               // no prompt → proceeds
    QCOMPARE(quitCount, 1);                            // quit ran
    QVERIFY(m_app->sessionDraftStore().hasSession());  // captured, not prompted
    QCOMPARE(win->documentCount(), 1);                 // doc still open (no-op quit)
}

void TestQuitAndKeepWindows::keepWindowsStructuralPdfUsesStructuralDraftKind() {
    // The persisted descriptor for a structural PDF is the new StructuralDraft
    // kind: an edited-PDF blob plus the original path. Assert it straight from
    // the on-disk manifest so the kind routing is pinned.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/structural.pdf";
    writeTinyPdf(path);

    PdfDocument *pdf = addPdfWindow(m_app, path);
    pdf->rotatePage(0, 90);
    QVERIFY(pdf->hasStructuralEdits());

    m_app->setPerformQuitForTesting([] {});
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QVERIFY(m_app->sessionDraftStore().hasSession());
    QCOMPARE(firstDocManifestKind(m_app), QStringLiteral("structural-draft"));
}

void TestQuitAndKeepWindows::keepWindowsStructuralPdfWithPendingRedactionPrompts() {
    // FIX 2 (never-worry-save floor): a PDF that is structurally dirty AND
    // carries a PENDING (un-applied) redaction annotation must NOT ride ⌥⌘Q
    // silently — writeRecoverySnapshot() would BURN the redaction into page
    // content, so a silent keep would return it applied and no longer editable
    // (a silent irreversible commit). It must fall back to the per-doc prompt,
    // exactly as it did before the structural-keep feature. Proof: with a
    // Cancel response staged, the prompt aborts the quit — a silent keep would
    // ignore Cancel and proceed.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/redact.pdf";
    writeTinyPdf(path);

    MainWindow *win = nullptr;
    PdfDocument *pdf = addPdfWindow(m_app, path, &win);
    pdf->rotatePage(0, 90); // structural
    Annotation r;
    r.type = AnnotationType::Redaction; // pending, un-applied
    r.page = 0;
    r.bounds = QRectF(20.0, 20.0, 60.0, 20.0);
    pdf->annotations()->add(r);
    QVERIFY(pdf->hasStructuralEdits());

    win->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    const bool proceeded = m_app->requestQuit(QuitMode::KeepWindows);

    QVERIFY(!proceeded);                                // routed to prompt; Cancel aborts
    QCOMPARE(quitCount, 0);                             // quit did NOT run
    QVERIFY(!m_app->sessionDraftStore().hasSession());  // nothing kept-and-burned
    QCOMPARE(win->documentCount(), 1);                  // doc kept, still open
}

void TestQuitAndKeepWindows::keepWindowsStructuralPdfSnapshotFailurePrompts() {
    // FIX 1 (never-worry-save floor): if the edited-blob snapshot cannot be
    // produced for a structural PDF, canDraftForKeep must PROVE that up front
    // and return false so requestQuit PROMPTS the doc — rather than reporting
    // it draftable and then silently OMITTING it from the capture (its edits
    // would vanish with no prompt on next launch). The snapshot-failure seam
    // forces writeRecoverySnapshot() to fail; a Cancel response then aborts the
    // quit, proving the prompt path ran.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/nosnap.pdf";
    writeTinyPdf(path);

    MainWindow *win = nullptr;
    PdfDocument *pdf = addPdfWindow(m_app, path, &win);
    pdf->rotatePage(0, 90); // structural
    pdf->setForceRecoverySnapshotFailureForTesting(true);
    QVERIFY(pdf->hasStructuralEdits());

    win->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    const bool proceeded = m_app->requestQuit(QuitMode::KeepWindows);

    QVERIFY(!proceeded);                                // snapshot preflight failed → prompt → Cancel aborts
    QCOMPARE(quitCount, 0);                             // quit did NOT run
    QVERIFY(!m_app->sessionDraftStore().hasSession());  // NOT silently dropped
    QCOMPARE(win->documentCount(), 1);                  // doc kept, still open
}

void TestQuitAndKeepWindows::restoreRotatedPdfKeepsRotationDirtyAndOriginalPath() {
    // Round-trip: rotate page 0 → ⌥⌘Q keep → relaunch/restore → the rotation
    // is intact (/Rotate == 90 in the restored editor), the restored doc is
    // dirty, its Save target is the ORIGINAL path, and the ORIGINAL file on
    // disk is byte-unchanged by the keep+restore.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/rotated.pdf";
    writeTinyPdf(path);
    const QByteArray originalDigest = sha256Of(path);
    QVERIFY(!originalDigest.isEmpty());

    PdfDocument *original = addPdfWindow(m_app, path);
    original->rotatePage(0, 90);
    QVERIFY(original->hasStructuralEdits());

    m_app->setPerformQuitForTesting([] {});
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QVERIFY(m_app->sessionDraftStore().hasSession());

    QVERIFY(m_app->restoreKeptWindows());

    PdfDocument *restored = findRestoredPdf(m_app, original);
    QVERIFY(restored);
    QCOMPARE(restored->filePath(), path);  // Save re-points to the ORIGINAL
    QVERIFY(restored->isDirty());          // recovered edits are unsaved
    // The original file is untouched by capture + restore (blob-only path).
    QCOMPARE(sha256Of(path), originalDigest);
    // Structural edit intact: /Rotate survives into the restored editor graph.
    const FirstPageInfo info = inspectRestoredFirstPage(restored);
    QVERIFY(info.ok);
    QCOMPARE(info.rotate, 90);
    QVERIFY(!m_app->sessionDraftStore().hasSession()); // one-shot consumed
}

void TestQuitAndKeepWindows::restoreDeletedPagePdfKeepsPageCountDirtyAndOriginalPath() {
    // Round-trip for a DELETE: a 3-page PDF with page 0 deleted returns as a
    // 2-page dirty doc pointing at the original path; original bytes unchanged.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/multi.pdf";
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        for (int i = 0; i < 3; ++i) {
            painter.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter,
                             QStringLiteral("Page %1").arg(i + 1));
            if (i < 2)
                writer.newPage();
        }
        painter.end();
    }
    const QByteArray originalDigest = sha256Of(path);
    QVERIFY(!originalDigest.isEmpty());

    PdfDocument *original = addPdfWindow(m_app, path);
    QCOMPARE(original->pageCount(), 3);
    original->deletePages({0});
    QCOMPARE(original->pageCount(), 2);
    QVERIFY(original->hasStructuralEdits());

    m_app->setPerformQuitForTesting([] {});
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QVERIFY(m_app->sessionDraftStore().hasSession());

    QVERIFY(m_app->restoreKeptWindows());

    PdfDocument *restored = findRestoredPdf(m_app, original);
    QVERIFY(restored);
    QCOMPARE(restored->filePath(), path);
    QVERIFY(restored->isDirty());
    QCOMPARE(restored->pageCount(), 2);          // delete survived the round-trip
    QCOMPARE(sha256Of(path), originalDigest);    // original untouched
    const FirstPageInfo info = inspectRestoredFirstPage(restored);
    QVERIFY(info.ok);
    QCOMPARE(info.pageCount, 2);
    QVERIFY(!m_app->sessionDraftStore().hasSession());
}

void TestQuitAndKeepWindows::restoreCroppedPdfKeepsCropBoxDirtyAndOriginalPath() {
    // Round-trip for a CROP (#102): the page's CropBox survives into the
    // restored editor, inset from the MediaBox by the requested margins.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/cropme.pdf";
    writeTinyPdf(path);
    const QByteArray originalDigest = sha256Of(path);
    QVERIFY(!originalDigest.isEmpty());

    PdfDocument *original = addPdfWindow(m_app, path);
    QVERIFY(original->cropPage(0, /*left=*/20.0, /*top=*/30.0, /*right=*/20.0,
                               /*bottom=*/30.0));
    QVERIFY(original->hasStructuralEdits());

    m_app->setPerformQuitForTesting([] {});
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QVERIFY(m_app->sessionDraftStore().hasSession());

    QVERIFY(m_app->restoreKeptWindows());

    PdfDocument *restored = findRestoredPdf(m_app, original);
    QVERIFY(restored);
    QCOMPARE(restored->filePath(), path);
    QVERIFY(restored->isDirty());
    QCOMPARE(sha256Of(path), originalDigest);
    const FirstPageInfo info = inspectRestoredFirstPage(restored);
    QVERIFY(info.ok);
    QVERIFY(info.hasCropBox);                       // crop survived the round-trip
    QVERIFY(!info.mediaBox.isNull());
    // CropBox is inset from the MediaBox by the requested margins: left +20,
    // bottom +30 from the media origin (PDF bottom-left coords).
    QVERIFY(info.cropBox.left() > info.mediaBox.left() + 0.5);
    QVERIFY(info.cropBox.bottom() < info.mediaBox.bottom() - 0.5);
    QVERIFY(info.cropBox.width() < info.mediaBox.width());
    QVERIFY(info.cropBox.height() < info.mediaBox.height());
    QVERIFY(!m_app->sessionDraftStore().hasSession());
}

void TestQuitAndKeepWindows::restoreMovedPagePdfKeepsOrderDirtyAndOriginalPath() {
    // FIX 5: round-trip for a page MOVE. A 3-page PDF whose page 0 is rotated
    // (a marker) then moved to the end returns with the marker page at its new
    // index, the doc dirty, its Save target the ORIGINAL path, and the original
    // file byte-unchanged.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/moveme.pdf";
    writeMultiPagePdf(path, 3, QStringLiteral("Page"));
    const QByteArray originalDigest = sha256Of(path);
    QVERIFY(!originalDigest.isEmpty());

    PdfDocument *original = addPdfWindow(m_app, path);
    QCOMPARE(original->pageCount(), 3);
    original->rotatePage(2, 90);  // mark the LAST page with /Rotate 90
    original->movePage(2, 0);     // move it to the front → rotations [90,0,0]
    QVERIFY(original->hasStructuralEdits());

    m_app->setPerformQuitForTesting([] {});
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QVERIFY(m_app->sessionDraftStore().hasSession());

    QVERIFY(m_app->restoreKeptWindows());

    PdfDocument *restored = findRestoredPdf(m_app, original);
    QVERIFY(restored);
    QCOMPARE(restored->filePath(), path);       // Save re-points to the ORIGINAL
    QVERIFY(restored->isDirty());
    QCOMPARE(restored->pageCount(), 3);
    QCOMPARE(sha256Of(path), originalDigest);   // original untouched
    // The rotated marker page moved to the front: the move survived the round-trip.
    const std::vector<int> rots = restoredPageRotations(restored);
    QCOMPARE(rots, (std::vector<int>{90, 0, 0}));
    QVERIFY(!m_app->sessionDraftStore().hasSession());
}

void TestQuitAndKeepWindows::restoreInsertedPagesPdfSurvivesWithoutSource() {
    // FIX 5 (highest-value): round-trip for INSERT-pages-from-a-file. Foreign
    // pages inserted from a SEPARATE source file must be materialized into the
    // self-contained edited blob, so they survive a keep+restore even when the
    // source file is DELETED before restore. Proves the blob carries the
    // inserted page content, not a reference to the source.
    if (runningUnderWine())
        QSKIP(kWineOpenFileDeleteSkip); // QFile::remove(source) below; see helper
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/main.pdf";
    writeMultiPagePdf(path, 1, QStringLiteral("Main"));
    const QByteArray originalDigest = sha256Of(path);
    QVERIFY(!originalDigest.isEmpty());
    const QString source = dir.path() + "/insert-source.pdf";
    writeMultiPagePdf(source, 2, QStringLiteral("Inserted"));

    PdfDocument *original = addPdfWindow(m_app, path);
    QCOMPARE(original->pageCount(), 1);
    QVERIFY(original->insertPagesFrom(source, 1)); // insert 2 pages after page 0
    QCOMPARE(original->pageCount(), 3);
    QVERIFY(original->hasStructuralEdits());

    m_app->setPerformQuitForTesting([] {});
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QVERIFY(m_app->sessionDraftStore().hasSession());

    // Delete the insert SOURCE before restore: the inserted pages must already
    // live in the captured blob, not depend on the source still being present.
    QVERIFY(QFile::remove(source));
    QVERIFY(!QFileInfo::exists(source));

    QVERIFY(m_app->restoreKeptWindows());

    PdfDocument *restored = findRestoredPdf(m_app, original);
    QVERIFY(restored);
    QCOMPARE(restored->filePath(), path);       // Save re-points to the ORIGINAL
    QVERIFY(restored->isDirty());
    QCOMPARE(restored->pageCount(), 3);         // inserted pages survived w/o source
    QCOMPARE(sha256Of(path), originalDigest);   // original main untouched
    const FirstPageInfo info = inspectRestoredFirstPage(restored);
    QVERIFY(info.ok);
    QCOMPARE(info.pageCount, 3);
    QVERIFY(!m_app->sessionDraftStore().hasSession());
}

void TestQuitAndKeepWindows::restoreStructuralPdfMovedOriginalReturnsUntitledDirty() {
    // FIX 3 (never-worry-save floor): the StructuralDraft blob is the COMPLETE
    // edited PDF and does not need the original to render (the original is only
    // the Save re-association target). So if the original is MOVED or DELETED
    // between ⌥⌘Q and restore, the captured work must NOT be silently dropped —
    // it returns as an UNTITLED, dirty doc (edits intact) whose first Save
    // prompts Save-As, mirroring the image Draft untitled restore.
    if (runningUnderWine())
        QSKIP(kWineOpenFileDeleteSkip); // QFile::remove(path) below; see helper
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/gone.pdf";
    writeTinyPdf(path);

    PdfDocument *original = addPdfWindow(m_app, path);
    original->rotatePage(0, 90);
    QVERIFY(original->hasStructuralEdits());

    m_app->setPerformQuitForTesting([] {});
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QVERIFY(m_app->sessionDraftStore().hasSession());

    // The original disappears before restore (moved/deleted by the user).
    QVERIFY(QFile::remove(path));
    QVERIFY(!QFileInfo::exists(path));

    QVERIFY(m_app->restoreKeptWindows());

    PdfDocument *restored = findRestoredPdf(m_app, original);
    QVERIFY(restored);                    // NOT dropped despite the missing original
    QVERIFY(restored->isUntitled());      // no on-disk home → first Save prompts Save-As
    QVERIFY(restored->filePath().isEmpty());
    QVERIFY(restored->isDirty());
    // The structural edit is intact in the recovered, self-sufficient blob.
    const FirstPageInfo info = inspectRestoredFirstPage(restored);
    QVERIFY(info.ok);
    QCOMPARE(info.rotate, 90);
    QVERIFY(!m_app->sessionDraftStore().hasSession());
}

void TestQuitAndKeepWindows::restoreAnnotatedPdfDeletedBackingReturnsUntitledDirty() {
    // Never-worry-save floor (deleted-backing annotated PDF): an
    // annotation-ONLY dirty PDF whose on-disk backing is DELETED underneath
    // between ⌥⌘Q and restore must NOT be silently dropped. The pristine
    // original+JSON AnnotatedPath approach can't reopen a deleted file, so the
    // keep captures a SELF-SUFFICIENT annotation-baked blob (the same path a
    // structural PDF takes) and restore returns it as an UNTITLED, dirty doc
    // with the unsaved annotation still present and EDITABLE. Before this fix
    // the AnnotatedPath restore branch did `!QFileInfo::exists(dd.path) →
    // continue`, silently dropping the doc and its annotations.
    if (runningUnderWine())
        QSKIP(kWineOpenFileDeleteSkip); // QFile::remove(path) below; see helper
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/annotated-gone.pdf";
    writeTinyPdf(path);

    PdfDocument *original = addAnnotationDirtyPdfWindow(m_app, path);
    QVERIFY(original->isDirty());
    QVERIFY(!original->hasStructuralEdits()); // annotation-only dirtiness
    const int originalCount = original->annotations()->count();
    QVERIFY(originalCount >= 1);

    // Force the qpdf editor to load WHILE THE FILE STILL EXISTS, so the live
    // editor holds an open handle on the backing file. On Linux an unlink of a
    // still-open file leaves the inode readable through that handle, so the
    // in-memory snapshot arm (writeRecoverySnapshot serializing the live editor
    // graph) can still succeed after the directory entry is gone.
    // hasPendingDestructiveAnnotation() drives ensureAnnotationsLoadedSync() →
    // ensureEditorLoaded(), which opens that handle; it returns false here (the
    // annotation is a plain Rectangle, not a redaction/signature).
    QVERIFY(!original->hasPendingDestructiveAnnotation());

    // The backing file disappears BEFORE the keep (deleted by the user).
    QVERIFY(QFile::remove(path));
    QVERIFY(!QFileInfo::exists(path));

    m_app->setPerformQuitForTesting([] {});
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows)); // no prompt → self-sufficient blob
    QVERIFY(m_app->sessionDraftStore().hasSession());   // captured, not dropped

    QVERIFY(m_app->restoreKeptWindows());

    PdfDocument *restored = findRestoredPdf(m_app, original);
    QVERIFY(restored);                    // NOT silently dropped despite the gone backing
    QVERIFY(restored->isUntitled());      // no on-disk home → first Save prompts Save-As
    QVERIFY(restored->filePath().isEmpty());
    QVERIFY(restored->isDirty());         // returned STILL DIRTY
    // The unsaved annotation survived, editable (a live store object with its
    // geometry, not flattened into page content).
    QCOMPARE(restored->annotations()->count(), originalCount);
    const std::vector<Annotation> &anns = restored->annotations()->annotations();
    QVERIFY(!anns.empty());
    QCOMPARE(anns.front().type, AnnotationType::Rectangle);
    QCOMPARE(anns.front().bounds, QRectF(10.0, 12.0, 40.0, 22.0));
    QVERIFY(!m_app->sessionDraftStore().hasSession()); // one-shot, consumed
}

void TestQuitAndKeepWindows::keepWindowsAnnotatedPdfDeletedBackingSnapshotFailurePrompts() {
    // Never-worry-save floor (deleted-backing annotated PDF, prompt arm): when
    // the self-sufficient snapshot CANNOT be produced for a deleted-backing
    // annotated PDF (e.g. the editor was never loaded so qpdf can no longer
    // read the gone file — modelled here with the force-failure seam),
    // canDraftForKeep must PROVE that up front and return false so requestQuit
    // PROMPTS the doc rather than silently dropping it. A Cancel response then
    // aborts the quit, proving the prompt path ran and nothing was lost.
    if (runningUnderWine())
        QSKIP(kWineOpenFileDeleteSkip); // QFile::remove(path) below; see helper
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/annotated-nosnap.pdf";
    writeTinyPdf(path);

    MainWindow *win = nullptr;
    PdfDocument *pdf = addAnnotationDirtyPdfWindow(m_app, path, &win);
    QVERIFY(pdf->isDirty());
    QVERIFY(!pdf->hasStructuralEdits());
    pdf->setForceRecoverySnapshotFailureForTesting(true);

    // Backing deleted → the annotation-dirty doc now needs the self-sufficient
    // blob path (AnnotatedPath can't reopen a gone file), and the snapshot the
    // preflight attempts is forced to fail.
    QVERIFY(QFile::remove(path));
    QVERIFY(!QFileInfo::exists(path));

    win->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    const bool proceeded = m_app->requestQuit(QuitMode::KeepWindows);

    QVERIFY(!proceeded);                                // snapshot preflight failed → prompt → Cancel aborts
    QCOMPARE(quitCount, 0);                             // quit did NOT run
    QVERIFY(!m_app->sessionDraftStore().hasSession());  // NOT silently dropped
    QCOMPARE(win->documentCount(), 1);                  // doc kept, still open
}

void TestQuitAndKeepWindows::keepWindowsClearsStaleRecoverySidecar() {
    // FIX 4: the #90 autosave recovery sidecar is keyed by the backing path and
    // is written on autosave ticks — but a ⌥⌘Q keep SUPERSEDES it (the doc
    // reopens dirty with the kept state). If the keep flow left the older
    // sidecar behind, a later File→Open of the original would resurrect the
    // superseded pre-keep state via pendingRecovery(). Assert the keep clears
    // the sidecar for a kept doc that has an on-disk original.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/sidecar.pdf";
    writeTinyPdf(path);

    PdfDocument *pdf = addPdfWindow(m_app, path);
    pdf->rotatePage(0, 90);
    QVERIFY(pdf->hasStructuralEdits());

    // Simulate a prior autosave tick: write a recovery sidecar for this backing
    // file and record it in the store (what MainWindow's autosave does).
    RecoveryStore &store = m_app->recoveryStore();
    const QString sidecar = store.sidecarPathFor(path);
    QVERIFY(pdf->writeRecoverySnapshot(sidecar));
    store.recordSnapshot(path, sidecar);
    QVERIFY(store.lookup(path).has_value());  // sidecar is present pre-keep

    m_app->setPerformQuitForTesting([] {});
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QVERIFY(m_app->sessionDraftStore().hasSession());

    // The keep superseded the sidecar: it (and its index entry) are gone, so a
    // later reopen of the original will NOT resurrect superseded state.
    QVERIFY(!store.lookup(path).has_value());
    QVERIFY(!store.pendingRecovery(path).has_value());
    QVERIFY(!QFileInfo::exists(sidecar));
    // The backing file itself was never touched by the sidecar clear.
    QVERIFY(QFileInfo::exists(path));
}

void TestQuitAndKeepWindows::restoreCombinedStructuralAndAnnotationKeepsBothEditable() {
    // Combined case: rotate a page AND add a regular annotation → keep →
    // restore → the rotation is intact AND the annotation is still present and
    // EDITABLE (a live store object, not burned into page content).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/combined.pdf";
    writeTinyPdf(path);
    const QByteArray originalDigest = sha256Of(path);
    QVERIFY(!originalDigest.isEmpty());

    PdfDocument *original = addPdfWindow(m_app, path);
    original->rotatePage(0, 90); // structural
    Annotation a;
    a.type = AnnotationType::Rectangle;
    a.page = 0;
    a.bounds = QRectF(10.0, 12.0, 40.0, 22.0);
    a.style.stroke = QColor(200, 30, 30);
    original->annotations()->add(a); // annotation-dirty on top of structural
    QVERIFY(original->hasStructuralEdits());
    const int originalAnnCount = original->annotations()->count();
    QVERIFY(originalAnnCount >= 1);

    m_app->setPerformQuitForTesting([] {});
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QVERIFY(m_app->sessionDraftStore().hasSession());
    // Structural presence routes it to the blob kind even with an annotation.
    QCOMPARE(firstDocManifestKind(m_app), QStringLiteral("structural-draft"));

    QVERIFY(m_app->restoreKeptWindows());

    PdfDocument *restored = findRestoredPdf(m_app, original);
    QVERIFY(restored);
    QCOMPARE(restored->filePath(), path);
    QVERIFY(restored->isDirty());
    QCOMPARE(sha256Of(path), originalDigest); // original untouched

    // Annotation is restored as an editable live object with its geometry.
    QCOMPARE(restored->annotations()->count(), originalAnnCount);
    const std::vector<Annotation> &anns = restored->annotations()->annotations();
    QVERIFY(!anns.empty());
    QCOMPARE(anns.front().type, AnnotationType::Rectangle);
    QCOMPARE(anns.front().bounds, QRectF(10.0, 12.0, 40.0, 22.0));

    // Rotation is intact in the restored editor graph. (inspect saves to a
    // temp AFTER the annotation checks, since save() re-points the target.)
    const FirstPageInfo info = inspectRestoredFirstPage(restored);
    QVERIFY(info.ok);
    QCOMPARE(info.rotate, 90);
    QVERIFY(!m_app->sessionDraftStore().hasSession());
}

void TestQuitAndKeepWindows::annotationJsonRoundTripsAllFields() {
    // The serializer underpinning PDF annotation persistence must preserve
    // EVERY field so a restored annotation is byte-faithful and editable.
    const Annotation a = makeRichAnnotation();
    const Annotation b = annotationFromJson(annotationToJson(a));

    QCOMPARE(b.id, a.id);
    QCOMPARE(b.page, a.page);
    QCOMPARE(b.type, a.type);
    QCOMPARE(b.bounds, a.bounds);
    QCOMPARE(b.points, a.points);
    QCOMPARE(b.pressures, a.pressures);
    QCOMPARE(b.quads, a.quads);
    QCOMPARE(b.text, a.text);
    QCOMPARE(b.imagePath, a.imagePath);
    QCOMPARE(b.style.stroke, a.style.stroke);
    QCOMPARE(b.style.fill, a.style.fill);
    QCOMPARE(b.style.strokeWidth, a.style.strokeWidth);
    QCOMPARE(b.style.fontPointSize, a.style.fontPointSize);
    QCOMPARE(b.style.dash, a.style.dash);
    QCOMPARE(b.style.fontFamily, a.style.fontFamily);
    QCOMPARE(b.style.fontWeight, a.style.fontWeight);
    QCOMPARE(b.style.zoomFactor, a.style.zoomFactor);
}

void TestQuitAndKeepWindows::keepWindowsDirtyTitledImageRestoresDirtyWithPath() {
    // Part 4: a dirty TITLED image kept via ⌥⌘Q returns with its ORIGINAL
    // path preserved and the document still dirty (its unsaved raster edits
    // survive byte-for-byte). Complements the untitled-draft round-trip.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/titled.png";
    const QImage known = makeKnownImage(21, 13, 4);
    QVERIFY(known.save(path, "PNG"));

    MainWindow *win = m_app->ensureFreshWindow();
    auto doc = std::make_unique<ImageDocument>(path);
    doc->setImageForTest(known);
    ImageDocument *original = doc.get();
    win->addDocument(std::move(doc));
    // A real (titled) raster edit makes the doc dirty without clearing its
    // path. Capture the post-edit pixels as the expected round-trip result.
    original->flipHorizontal();
    const QImage expected = original->image();
    QVERIFY(!original->isUntitled());
    QVERIFY(original->isDirty());

    m_app->setPerformQuitForTesting([] {});
    QVERIFY(m_app->requestQuit(QuitMode::KeepWindows));
    QVERIFY(m_app->sessionDraftStore().hasSession());

    QVERIFY(m_app->restoreKeptWindows());

    ImageDocument *restored = nullptr;
    for (MainWindow *w : m_app->windows()) {
        if (!w)
            continue;
        for (int i = 0; i < w->documentCount(); ++i) {
            IDocument *d = nullptr;
            if (w->documentAt(i, &d) == 1 && d && d != original)
                if (auto *img = dynamic_cast<ImageDocument *>(d))
                    restored = img;
        }
    }
    QVERIFY(restored);
    QVERIFY(!restored->isUntitled());
    QCOMPARE(restored->filePath(), path);  // original path preserved
    QVERIFY(restored->isDirty());          // returned still dirty
    QCOMPARE(restored->image().convertToFormat(QImage::Format_ARGB32),
             expected.convertToFormat(QImage::Format_ARGB32));
}

void TestQuitAndKeepWindows::deletedUnderneathDocIsPromptedOnNormalQuit() {
    // CF-7 × #78 integration: a CLEAN, TITLED doc whose backing file was
    // DELETED underneath has hasUnsavedWork()==true but isDirty()==false. The
    // ⌘Q (Normal) quit collection must include it — its in-memory buffer is
    // the last copy — so ⌘Q PROMPTS instead of silently quitting and dropping
    // the buffer. Driven through collectDirtyDocsForQuit() and the Normal quit
    // seam directly (the app-level ⌘Q shortcut is not reachable headless).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/deleted-underneath.png";

    MainWindow *win = nullptr;
    ImageDocument *doc =
        addDeletedUnderneathImageWindow(m_app, makeKnownImage(19, 15, 6), path, &win);

    // The deleted-underneath state: clean, titled, but unsaved work exists.
    QVERIFY(!doc->isDirty());
    QVERIFY(!doc->isUntitled());
    QCOMPARE(doc->externalChangeState(), ExternalChangeState::Deleted);
    QVERIFY(doc->hasUnsavedWork());
    QVERIFY(!QFileInfo::exists(path)); // backing file really is gone

    // (a) The quit collection includes the deleted-underneath doc. Were the
    // predicate still bare isDirty()||isUntitled(), this clean titled doc
    // would be absent and ⌘Q would quit silently.
    const std::vector<IDocument *> collected = win->collectDirtyDocsForQuit();
    QVERIFY(std::find(collected.begin(), collected.end(),
                      static_cast<IDocument *>(doc)) != collected.end());

    // (a, end-to-end) A Normal quit therefore raises the per-doc prompt; a
    // Cancel response aborts the whole quit. If the doc were NOT collected the
    // prompt would never fire and requestQuit would proceed (return true).
    win->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    const bool proceeded = m_app->requestQuit(QuitMode::Normal);

    QVERIFY(!proceeded);                // Cancel at the prompt aborted the quit
    QCOMPARE(quitCount, 0);             // did NOT silently quit
    QCOMPARE(win->documentCount(), 1);  // doc kept
    QVERIFY(!QFileInfo::exists(path));  // Cancel wrote nothing to the backing file
}

void TestQuitAndKeepWindows::keepWindowsKeepsDeletedDocWithoutWritingBackingFile() {
    // CF-7 × #78 integration, keep path: ⌥⌘Q (Quit and Keep Windows) keeps a
    // deleted-underneath image doc WITHOUT prompting and WITHOUT writing its
    // (deleted) backing file. The raster is the last copy, so it is DRAFTED
    // into the session store (app-data) — not stored as a dangling path ref —
    // and comes back byte-identical on restore.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/deleted-underneath-keep.png";
    const QImage known = makeKnownImage(23, 17, 8);

    MainWindow *win = nullptr;
    ImageDocument *doc = addDeletedUnderneathImageWindow(m_app, known, path, &win);
    QVERIFY(doc->hasUnsavedWork());
    QVERIFY(!doc->isDirty());
    QVERIFY(!QFileInfo::exists(path));

    // A Cancel response would abort IF a prompt were raised — the keep path
    // must NOT prompt (the raster is draftable), so it stays irrelevant.
    win->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
    int quitCount = 0;
    m_app->setPerformQuitForTesting([&] { ++quitCount; });

    const bool proceeded = m_app->requestQuit(QuitMode::KeepWindows);

    QVERIFY(proceeded);                                // kept without a prompt
    QCOMPARE(quitCount, 1);
    QVERIFY(m_app->sessionDraftStore().hasSession());  // drafted, not dropped
    QVERIFY(!QFileInfo::exists(path));                 // backing file NOT written

    // The deleted doc's raster survived: restore brings it back byte-for-byte
    // (proving it was drafted, not stored as a dangling path to the gone file).
    QVERIFY(m_app->restoreKeptWindows());
    ImageDocument *restored = nullptr;
    for (MainWindow *w : m_app->windows()) {
        if (!w)
            continue;
        for (int i = 0; i < w->documentCount(); ++i) {
            IDocument *d = nullptr;
            if (w->documentAt(i, &d) == 1 && d && d != doc)
                if (auto *img = dynamic_cast<ImageDocument *>(d))
                    restored = img;
        }
    }
    QVERIFY(restored);
    QCOMPARE(restored->image().convertToFormat(QImage::Format_ARGB32),
             known.convertToFormat(QImage::Format_ARGB32));
    QVERIFY(!QFileInfo::exists(path)); // restore did not recreate the backing file
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
    // See tests/test_image_scale.cpp's main() for why this is needed on
    // macOS: QSettings(org, app) defaults to NativeFormat there, which
    // ignores the HOME sandboxing above. This binary in particular closes
    // real MainWindows as part of its quit-flow testing, which writes
    // DocumentTypeDefaults/RecentFiles via MainWindow::closeEvent() -- on
    // macOS, without this, that state would land in the REAL, shared
    // ~/Library/Preferences/ domain (this was the actual leak this class
    // of bug came from, per the 2026-07-26 investigation).
    QSettings::setDefaultFormat(QSettings::IniFormat);

    Application app(argc, argv);
    TestQuitAndKeepWindows tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_quit_and_keep_windows.moc"
