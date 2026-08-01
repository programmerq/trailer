#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "document/DocumentRegistry.h"
#include "document/ImageAdapter.h"
#include "document/PageChangeNotifier.h"
#include "document/PdfAdapter.h"
#include "document/PdfEditor.h"
#include "document/SelectableTextStore.h"
#include "ui/MainWindow.h"
#include "ui/SelectableTextLayer.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QObject>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPdfView>
#include <QPdfWriter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>

using namespace trailer;

namespace {

// Bounding box of the "dark" (text) pixels in a white-background render.
// Used to locate where glyphs actually landed, independent of any text-
// layer geometry, so selection alignment can be checked against the real
// raster.
QRect darkPixelBBox(const QImage &imgIn) {
    const QImage img = imgIn.convertToFormat(QImage::Format_ARGB32);
    int minX = img.width(), minY = img.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = img.pixel(x, y);
            // "Dark enough to be ink": any channel well below white.
            if (qRed(px) < 128 && qGreen(px) < 128 && qBlue(px) < 128) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (maxX < 0)
        return {};
    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

// Hand-writes a minimal one-page PDF whose text uses the PDF spec's
// genuine invisible text-rendering mode (`3 Tr`) — the construct real OCR
// pipelines (ocrmypdf, Acrobat's "searchable image" OCR, pdfsandwich)
// actually emit: a scanned raster with a machine-readable text layer laid
// on top that paints nothing. QPainter/QPdfWriter have no API for text
// render mode (they always emit the default `0 Tr` fill), so the fixture
// writes the object graph directly — object table, xref, and trailer by
// hand — to get a real `3 Tr` operator into the content stream. This is
// deliberately NOT the same trick as writeOcrLayerPdf() in
// tests/uat/test_uat_search_and_markup.cpp (an alpha=0 PEN, still `0 Tr`
// fill text): that fixture proves QPdfSearchModel tolerates invisible
// ink; this one proves QPdfDocument's SELECTION api tolerates the actual
// invisible RENDER MODE, which is a different code path in pdfium.
QString writeInvisibleRenderModePdf(const QString &path, const QString &text) {
    const QByteArray content = "BT /F1 24 Tf 100 700 Td 3 Tr (" + text.toLatin1() + ") Tj ET";
    QList<QByteArray> objects;
    objects << "<< /Type /Catalog /Pages 2 0 R >>";
    objects << "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
    objects << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
               "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>";
    objects << "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
    objects << "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content +
                   "\nendstream";

    QByteArray out = "%PDF-1.4\n";
    QList<int> offsets;
    for (int i = 0; i < objects.size(); ++i) {
        offsets << static_cast<int>(out.size());
        out += QByteArray::number(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const int xrefOffset = static_cast<int>(out.size());
    const int n = static_cast<int>(objects.size()) + 1;
    out += "xref\n0 " + QByteArray::number(n) + "\n";
    out += "0000000000 65535 f \n";
    for (int off : offsets) {
        out += QByteArray::number(off).rightJustified(10, '0') + " 00000 n \n";
    }
    out += "trailer\n<< /Size " + QByteArray::number(n) + " /Root 1 0 R >>\n";
    out += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF";

    QFile f(path);
    [[maybe_unused]] const bool opened = f.open(QIODevice::WriteOnly);
    Q_ASSERT(opened);
    f.write(out);
    f.close();
    return path;
}

} // namespace

class TestAdapters : public QObject {
    Q_OBJECT
  private slots:
    void pdfAdapterAdvertisesPdfExtension();
    void imageAdapterAdvertisesCommonExtensions();
    void registryRoutesPdfToPdfAdapter();
    void registryRoutesPngToImageAdapter();
    void imageDocumentLoadsPng();
    void pdfDocumentReportsInvalidForMissingFile();
    void imageDocumentZoomResizesPixmap();
    void imageDocumentStagedOpenShowsLoadingPlaceholder();
    void imageDocumentFastDecodeNeverFlashesLoadingText();
    void imageDocumentReloadSupersedesInFlightDecode();
    void imageDocumentDecodeFailureReportsNoCapabilities();
    void pdfDocumentAdvertisesCapabilities();
    void pdfDocumentRendersThumbnailsForValidFile();
    void pdfDocumentAcceptsSearchQueryWithoutView();
    void pdfDocumentPageHasTextDistinguishesTextFromBlankPage();
    void pdfDocumentNativeTextLayerFeedsSelectableStore();
    void pdfDocumentNativeTextDragSelectsRealString();
    void pdfDocumentNativeTextAlignsWithRenderedGlyphs();
    void pdfDocumentNativeTextMultiLineOrdering();
    void pdfDocumentInvisibleRenderModeTextIsIngestedAndSelectable();
    void printSupportReflectsValidity();
    void pdfDocumentRotationMarksDirtyAndSaveClears();
    void pdfDocumentDeletePagesRemovesAndMarksDirty();
    void pdfDocumentMovePageReorders();
    void pdfDocumentInterleavedUndoIsChronological();
    void pdfDocumentUndoAllPastOldCapRegimeIsExact();
    void pdfDocumentSmallCapEvictionKeepsLogAndStoreInLockstep();
    void pdfDocumentUndoRedoSurviveForcedLogDesync();
    void pdfDocumentSaveReloadRebuildsUndoLogFromRetainedStacks();
    void pdfAnnotationUndoAfterInWindowEditPreservesLoadedAnnotations();
    void imageDocumentRotateSwapsDimensionsAndMarksDirty();
    void imageDocumentFlipHorizontalMarksDirty();
    void imageDocumentResizeChangesPixelSize();
    void imageDocumentCropReducesSize();
    void imageDocumentAdjustColourModifiesPixels();
    void imageDocumentSaveClearsDirty();
    void imageDocumentExportAsJpegWritesFile();
    void imageDocumentExportsImageAsSinglePagePdf();
    void imageDocumentUndoRestoresPriorState();
    void imageDocumentSaveFlattensAnnotationsIntoPixels();
    void imageDocumentUndoPopsMostRecentAcrossDomains();
    void imageDocumentInterleavedUndoIsChronological();
    void imageDocumentUndoRedoSurviveForcedLogDesync();
    void imageDocumentUndoAllPastOldCapRegimeIsExact();
    void imageDocumentPixelCapEvictionKeepsLogInLockstep();
    void imageDocumentSmallCapAnnotationEvictionKeepsLogInLockstep();
    void imageDocumentFitModeStartsCustom();
    void imageDocumentZoomFitPageEntersFitInViewMode();
    void imageDocumentZoomFitWidthEntersFitToWidthMode();
    void imageDocumentExplicitZoomReturnsToCustomMode();
    void imageDocumentZoomInStepsByQuarter();
    void pdfDocumentZoomInStepsByQuarter();
    void imageDocumentReapplyFitModeRefitsOnResize();
    void imageDocumentResizeDoesNothingInCustomMode();
    void pdfViewReflowsOnResizeInFitInView();
    void pdfDownArrowStepsPageImmediatelyInFitMode();
    void imageDocumentResizeEventTriggersRefit();
    // Item A — image documents become searchable via their OCR store.
    void imageDocumentSupportsSearch();
    void imageDocumentSearchFindsOcrText();
    void imageDocumentSearchIsCaseInsensitive();
    void imageDocumentEmptyStoreSearchNoMatches();
    // Item B — single-page Recognize skips the page-range dialog.
    void recognizeTextSkipsDialogForSinglePage();
    // Item C — honest completion feedback.
    void ocrBatchWithZeroBlocksReportsNoTextFound();
    // Page-changed signal (backlog 2026-07-12-page-changed-signal-no-poll):
    // PdfDocument exposes a real page-changed signal via PageChangeNotifier,
    // and single-frame image documents expose none.
    void pdfDocumentPageChangeNotifierFiresOnPageChange();
    void imageDocumentHasNoPageChangeNotifier();
};

namespace {
// Open a freshly-written white PNG through the adapter. Shared by the
// Item A image-search tests, which seed the returned document's
// SelectableTextStore directly (no OCR run needed).
std::unique_ptr<IDocument> openBlankImage(QTemporaryDir &dir, const QString &name) {
    const QString path = dir.filePath(name);
    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(Qt::white);
    img.save(path, "PNG");
    ImageAdapter adapter;
    return adapter.open(path);
}

// Seed two OCR blocks into a store on page 0 so search has something to
// hit. "Hello World" and "Foobar" both contain 'o', which the multi-
// match advance test relies on.
void seedOcrBlocks(SelectableTextStore *store) {
    OcrEngine::TextBlock a;
    a.text = QStringLiteral("Hello World");
    a.polygon = QPolygon(QRect(10, 10, 80, 20));
    OcrEngine::TextBlock b;
    b.text = QStringLiteral("Foobar");
    b.polygon = QPolygon(QRect(10, 40, 80, 20));
    store->put(0, 1ULL, {a, b});
}
} // namespace

void TestAdapters::pdfAdapterAdvertisesPdfExtension() {
    PdfAdapter adapter;
    QVERIFY(adapter.extensions().contains("pdf"));
    QVERIFY(adapter.mimeTypes().contains("application/pdf"));
}

void TestAdapters::imageAdapterAdvertisesCommonExtensions() {
    ImageAdapter adapter;
    const QStringList exts = adapter.extensions();
    for (const QString &expected : {"png", "jpg", "jpeg", "gif", "bmp", "tiff", "webp"}) {
        QVERIFY2(exts.contains(expected), qPrintable(expected));
    }
}

void TestAdapters::registryRoutesPdfToPdfAdapter() {
    DocumentRegistry reg;
    reg.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = reg.open("/tmp/nonexistent.pdf");
    QVERIFY(doc != nullptr);
    auto *pdf = dynamic_cast<PdfDocument *>(doc.get());
    QVERIFY2(pdf != nullptr, "expected a PdfDocument");
    QVERIFY(!pdf->isValid()); // nonexistent file
}

void TestAdapters::registryRoutesPngToImageAdapter() {
    DocumentRegistry reg;
    reg.registerAdapter(std::make_unique<ImageAdapter>());
    auto doc = reg.open("/tmp/nonexistent.png");
    QVERIFY(doc != nullptr);
    QCOMPARE(doc->displayName(), QStringLiteral("nonexistent.png"));
}

void TestAdapters::imageDocumentLoadsPng() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("tiny.png");

    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QVERIFY(img.save(path, "PNG"));

    ImageAdapter adapter;
    auto doc = adapter.open(path);
    QVERIFY(doc != nullptr);
    QCOMPARE(doc->displayName(), QStringLiteral("tiny.png"));
    QCOMPARE(doc->filePath(), path);

    std::unique_ptr<QWidget> view(doc->createView(nullptr));
    QVERIFY(view != nullptr);
}

void TestAdapters::pdfDocumentReportsInvalidForMissingFile() {
    PdfDocument doc("/tmp/definitely-not-a-real-file.pdf");
    QVERIFY(!doc.isValid());
    QCOMPARE(doc.pageCount(), 0);

    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    QVERIFY(view != nullptr); // falls back to an error label
}

void TestAdapters::imageDocumentZoomResizesPixmap() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("tiny.png");

    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::blue);
    QVERIFY(img.save(path, "PNG"));

    ImageDocument doc(path);
    QVERIFY(doc.supportsZoom());

    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    QVERIFY(view != nullptr);
    // Staged open (ADR 0008): createView paints a placeholder and the real
    // pixmap swaps in off-thread. Await the swap before reading the pixmap.
    QVERIFY(doc.awaitDecodeForTest());

    // Find the QLabel inside the QScrollArea that owns the pixmap.
    auto *scroll = qobject_cast<QScrollArea *>(view.get());
    QVERIFY(scroll != nullptr);
    auto *label = qobject_cast<QLabel *>(scroll->widget());
    QVERIFY(label != nullptr);

    const QSize original = label->pixmap().size();
    doc.zoomIn();
    const QSize zoomed = label->pixmap().size();
    QVERIFY(zoomed.width() > original.width());

    doc.zoomActual();
    QCOMPARE(label->pixmap().size(), original);
}

void TestAdapters::imageDocumentStagedOpenShowsLoadingPlaceholder() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("loading.png");
    QImage img(64, 48, QImage::Format_ARGB32);
    img.fill(Qt::darkGreen);
    QVERIFY(img.save(path, "PNG"));

    ImageDocument doc(path);
    // Force the loading text to be deferred well past this test so the grace
    // window is observable (owner refinement, PR #109).
    doc.setPlaceholderTextDelayForTest(60000);
    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    auto *scroll = qobject_cast<QScrollArea *>(view.get());
    QVERIFY(scroll != nullptr);
    auto *label = qobject_cast<QLabel *>(scroll->widget());
    QVERIFY(label != nullptr);

    // Staged open (ADR 0008): the window is drawn from the header hint but the
    // placeholder starts BLANK — no pixmap and no text during the grace window.
    QVERIFY2(label->pixmap().isNull(), "placeholder must not show a decoded pixmap yet");
    QVERIFY2(label->text().isEmpty(), "placeholder text must be deferred (blank grace window)");
    QVERIFY2(doc.placeholderTextTimerActiveForTest(), "the grace timer must be pending");

    // Once the grace elapses (simulated) while the decode is still in flight,
    // the honest "Loading image…" text appears — a visible loading state, not a
    // blank/stub the user could mistake for an empty or broken file (G3).
    doc.triggerPlaceholderTextTimerForTest();
    QVERIFY2(label->text().contains(QStringLiteral("Loading")),
             qPrintable(QStringLiteral("expected a loading placeholder, got text '%1'")
                            .arg(label->text())));

    // The real pixmap swaps in once the worker decode completes.
    QVERIFY(doc.awaitDecodeForTest());
    QVERIFY2(!label->pixmap().isNull(), "decoded pixmap must replace the placeholder");
    QVERIFY(label->text().isEmpty());
}

void TestAdapters::imageDocumentFastDecodeNeverFlashesLoadingText() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("fast.png");
    QImage img(48, 48, QImage::Format_ARGB32);
    img.fill(Qt::magenta);
    QVERIFY(img.save(path, "PNG"));

    ImageDocument doc(path);
    // A grace delay far longer than the decode: the fast decode must swap in
    // and CANCEL the timer, so the loading text is never shown (no one-frame
    // flash — the owner's PR #109 requirement).
    doc.setPlaceholderTextDelayForTest(60000);
    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    auto *scroll = qobject_cast<QScrollArea *>(view.get());
    QVERIFY(scroll != nullptr);
    auto *label = qobject_cast<QLabel *>(scroll->widget());
    QVERIFY(label != nullptr);
    QVERIFY2(label->text().isEmpty(), "grace window must start blank");

    QVERIFY(doc.awaitDecodeForTest());
    QVERIFY2(!label->pixmap().isNull(), "decoded pixmap must be shown after the fast decode");
    QVERIFY2(label->text().isEmpty(),
             "loading text must NEVER appear when the decode beats the grace delay");
    QVERIFY2(!doc.placeholderTextTimerActiveForTest(),
             "the fast-decode swap must cancel the pending grace timer");
}

void TestAdapters::imageDocumentReloadSupersedesInFlightDecode() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("reload.png");
    QImage red(32, 32, QImage::Format_ARGB32);
    red.fill(Qt::red);
    QVERIFY(red.save(path, "PNG"));

    // Open kicks the off-thread decode of the RED image.
    ImageDocument doc(path);
    QVERIFY2(doc.isDecodePendingForTest(), "open must defer the decode off-thread");

    // Before the decode lands, the file changes on disk and the user reloads.
    // The reload must SUPERSEDE the in-flight open decode (#89), not race it.
    QImage blue(32, 32, QImage::Format_ARGB32);
    blue.fill(Qt::blue);
    QVERIFY(blue.save(path, "PNG"));
    QVERIFY(doc.reloadFromDisk());
    QVERIFY2(!doc.isDecodePendingForTest(),
             "reload must supersede the in-flight open decode");
    // Supersede must also DROP the in-flight watcher + future so the superseded
    // worker's decoded buffer isn't pinned for the doc's lifetime and its stale
    // finished callback can't fire (memory-retention finding).
    QVERIFY2(doc.decodeWatcherClearedForTest(),
             "reload must drop the superseded decode's watcher, not just guard it");

    // Flush any stale worker finished callback; the generation guard must make
    // it a no-op so it can never clobber the reloaded (blue) pixels.
    QVERIFY(doc.awaitDecodeForTest());
    QCoreApplication::processEvents();
    QCOMPARE(doc.image().pixelColor(0, 0), QColor(Qt::blue));
}

void TestAdapters::imageDocumentDecodeFailureReportsNoCapabilities() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("corrupt.ppm");
    // A PPM whose ASCII header fully specifies the size (QImageReader::size()
    // succeeds, so the staged open kicks a decode) but whose binary pixel data
    // is truncated to far fewer than 400*300*3 bytes, so the full decode fails.
    // This is the "pending, then failed decode" path staged open introduces.
    {
        QByteArray ppm = QByteArrayLiteral("P6\n400 300\n255\n");
        ppm.append(100, '\0');
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(ppm);
    }

    ImageDocument doc(path);
    // The header parsed → staged decode kicked → capabilities provisionally
    // report the image as present while it is still decoding.
    QVERIFY2(doc.isDecodePendingForTest(), "a parseable header must kick the staged decode");
    QVERIFY(doc.supportsZoom());
    QCOMPARE(doc.pageCount(), 1);

    // Await the decode; it produces a null image (corrupt body).
    QVERIFY(doc.awaitDecodeForTest());
    QVERIFY(doc.image().isNull());

    // Capabilities MUST now be honest: a null decoded image is not
    // zoomable/editable/searchable and has no page. Otherwise the controls
    // would stay enabled-but-inert against a null image (G3 — lying controls).
    QVERIFY2(!doc.supportsZoom(), "failed decode must not report zoom support");
    QVERIFY2(!doc.supportsEditing(), "failed decode must not report edit support");
    QVERIFY2(!doc.supportsThumbnails(), "failed decode must not report thumbnails");
    QVERIFY2(!doc.supportsSelectableText(), "failed decode must not report selectable text");
    QCOMPARE(doc.pageCount(), 0);
}

void TestAdapters::pdfDocumentAdvertisesCapabilities() {
    PdfDocument doc("/tmp/does-not-exist.pdf");
    QVERIFY(doc.supportsZoom());
    QVERIFY(doc.supportsViewModes());
    // Default mode for new PDFs is Continuous per PdfDocument's initial state.
    QCOMPARE(doc.viewMode(), ViewMode::Continuous);
    doc.setViewMode(ViewMode::SinglePage);
    QCOMPARE(doc.viewMode(), ViewMode::SinglePage);
}

void TestAdapters::pdfDocumentRendersThumbnailsForValidFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("tiny.pdf");

    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.drawText(QRect(100, 100, 400, 100), Qt::AlignCenter, "Page 1");
        writer.newPage();
        painter.drawText(QRect(100, 100, 400, 100), Qt::AlignCenter, "Page 2");
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    QVERIFY(doc.supportsThumbnails());
    QCOMPARE(doc.pageCount(), 2);

    const QImage thumb = doc.renderThumbnail(0, QSize(128, 160));
    QVERIFY(!thumb.isNull());
    QVERIFY(thumb.width() <= 128);
    QVERIFY(thumb.height() <= 160);

    // PdfDocument composites transparent renders onto opaque paper white;
    // sample a corner that stays background on this fixture.
    const QImage thumbArgb = thumb.convertToFormat(QImage::Format_ARGB32);
    QVERIFY(thumbArgb.width() > 8 && thumbArgb.height() > 8);
    const QRgb paper = thumbArgb.pixel(2, 2);
    QCOMPARE(qAlpha(paper), 255);
    QCOMPARE(qRed(paper), 255);
    QCOMPARE(qGreen(paper), 255);
    QCOMPARE(qBlue(paper), 255);

    const QImage oob = doc.renderThumbnail(5, QSize(128, 160));
    QVERIFY(oob.isNull());
}

void TestAdapters::pdfDocumentAcceptsSearchQueryWithoutView() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("searchable.pdf");

    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter, "trailer");
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    QVERIFY(doc.supportsSearch());
    doc.setSearchQuery("trailer");
    doc.findNext();
    doc.findPrevious();
    doc.clearSearch();

    PdfDocument missing("/tmp/definitely-not-a-real-file.pdf");
    QVERIFY(!missing.isValid());
    missing.setSearchQuery("anything");
    missing.findNext();
    missing.clearSearch();
}

// R2/backlog 2026-07-13: pageHasText() is a real per-page probe — true
// for a born-digital page, false for a text-less (blank/scanned) page.
// This is the guard that keeps the Recognize-text notice off text-layer
// docs, distinct from the coarse hasTextLayer() capability stub.
void TestAdapters::pdfDocumentPageHasTextDistinguishesTextFromBlankPage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("mixed.pdf");
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        // Page 0: real text. Page 1: no drawText at all — a text-less
        // page, the closest fixture to an image-only scan.
        painter.drawText(QRect(100, 100, 800, 200), Qt::AlignLeft, "Born digital page");
        writer.newPage();
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    QCOMPARE(doc.pageCount(), 2);
    // hasTextLayer() stays the coarse capability stub (true for any valid
    // PDF) — we must not have perturbed it.
    QVERIFY(doc.hasTextLayer());
    QVERIFY2(doc.pageHasText(0), "Page with drawn text must report pageHasText() true");
    QVERIFY2(!doc.pageHasText(1), "Blank/text-less page must report pageHasText() false");
    // Out-of-range and invalid docs are false, never a crash.
    QVERIFY(!doc.pageHasText(-1));
    QVERIFY(!doc.pageHasText(99));

    PdfDocument missing("/tmp/definitely-not-a-real-file.pdf");
    QVERIFY(!missing.isValid());
    QVERIFY(!missing.pageHasText(0));
}

// R1: the native PDF text layer feeds SelectableTextStore for born-
// digital pages — so selection has something to hit-test even though no
// OCR ever ran. ingestNativeTextLayer() populates the store; it must not
// clobber existing (OCR) results.
void TestAdapters::pdfDocumentNativeTextLayerFeedsSelectableStore() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("native.pdf");
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.drawText(QRect(100, 100, 900, 120), Qt::AlignLeft, "Selectable native line");
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    auto *store = doc.selectableText();
    QVERIFY(store);
    QVERIFY2(!store->hasResults(0), "Store starts empty before ingestion");

    doc.ingestNativeTextLayer(0);
    QVERIFY2(store->hasResults(0), "Native ingestion must populate the store for a text page");
    const auto &blocks = store->blocks(0);
    QVERIFY(!blocks.empty());
    // The joined native text carries the drawn string.
    QStringList parts;
    for (const auto &b : blocks)
        parts << b.text;
    const QString joined = parts.join(QLatin1Char(' '));
    QVERIFY2(joined.contains(QStringLiteral("Selectable")),
             qPrintable(QStringLiteral("native text missing expected word, got: ") + joined));
    // Geometry is real (point-space), not a degenerate rect.
    QVERIFY(!blocks.front().polygon.boundingRect().isEmpty());

    // Re-ingest must NOT clobber: seed a distinct "OCR" entry, re-run,
    // and confirm the stored blocks are the OCR ones (hasResults short-
    // circuits so real recognition output always wins).
    OcrEngine::TextBlock ocr;
    ocr.text = QStringLiteral("OCR-SENTINEL");
    ocr.polygon = QPolygon({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
    store->put(0, 12345ULL, {ocr});
    doc.ingestNativeTextLayer(0);
    QCOMPARE(store->blocks(0).size(), static_cast<size_t>(1));
    QCOMPARE(store->blocks(0).front().text, QStringLiteral("OCR-SENTINEL"));

    // A text-less page stays empty after an ingest attempt.
    doc.ingestNativeTextLayer(1);
    QVERIFY(!store->hasResults(1));
}

// R1 end-to-end: with the native blocks in the store, a drag over the
// text region through a SelectableTextLayer yields the real native
// string (find already worked; this closes the selection gap).
void TestAdapters::pdfDocumentNativeTextDragSelectsRealString() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("dragnative.pdf");
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.drawText(QRect(100, 100, 900, 120), Qt::AlignLeft, "Draggable words");
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    doc.ingestNativeTextLayer(0);
    auto *store = doc.selectableText();
    QVERIFY(store && store->hasResults(0));
    const auto &blocks = store->blocks(0);
    QVERIFY(!blocks.empty());
    const QRect region = blocks.front().polygon.boundingRect();
    QVERIFY(!region.isEmpty());

    // Identity docToView mapping: the layer's block coords are already in
    // the point space this test drags in (same space the adapter's real
    // docToView multiplies by zoom).
    SelectableTextLayer layer;
    layer.resize(1200, 1600);
    layer.setStore(store);
    layer.setCurrentPage(0);
    layer.setDocToView([](QPointF p, int) { return p; });

    // Drag across the whole region — snaps to the block, exactly like the
    // OCR path.
    const QString selected =
        layer.simulateDragForTest(QPointF(region.left() + 1, region.top() + 1),
                                  QPointF(region.right() - 1, region.bottom() - 1));
    QVERIFY2(!selected.isEmpty(), "Drag over native text must yield a non-empty selection");
    QVERIFY2(selected.contains(QStringLiteral("Draggable")),
             qPrintable(QStringLiteral("selection missing expected word, got: ") + selected));
    // The selection equals what Ctrl+C would copy (selectedText() is the
    // clipboard source).
    QCOMPARE(layer.selectedBlockCount(), static_cast<int>(blocks.size()));
}

// R1 real-view alignment (reviewer M1): drive selection through the REAL
// adapter docToView (points→view: origin + p*zoom, as in
// PdfAdapter.cpp) and assert the block lands on the actual rendered
// glyphs — not just that the wiring works under an identity mapping.
void TestAdapters::pdfDocumentNativeTextAlignsWithRenderedGlyphs() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("align.pdf");
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::Letter));
        QPainter painter(&writer);
        QFont f;
        f.setPixelSize(48);
        painter.setFont(f);
        painter.drawText(QRect(120, 160, 1600, 120), Qt::AlignLeft, "AlignmentProbe");
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    doc.ingestNativeTextLayer(0);
    auto *store = doc.selectableText();
    QVERIFY(store && store->hasResults(0));
    const auto &blocks = store->blocks(0);
    QVERIFY(!blocks.empty());
    const QRectF blockPts = blocks.front().polygon.boundingRect();

    // renderPageForOcr renders at 144 DPI = exactly 2× PDF points, so with
    // zoom = 2 and origin = 0 the OCR raster IS the view space. This lets
    // us locate the glyphs independently (dark pixels) and compare against
    // the block mapped through the real docToView.
    const double zoom = 2.0;
    const QPointF origin(0.0, 0.0);
    auto docToView = [origin, zoom](QPointF p) {
        return QPointF(origin.x() + p.x() * zoom, origin.y() + p.y() * zoom);
    };
    const QRectF blockView(docToView(blockPts.topLeft()), docToView(blockPts.bottomRight()));

    const QImage raster = doc.renderPageForOcr(0);
    QVERIFY(!raster.isNull());
    const QRect glyphPx = darkPixelBBox(raster);
    QVERIFY2(!glyphPx.isEmpty(), "rendered page must contain ink");

    // The block, mapped through the real docToView, must overlap the
    // actual glyphs and cover most of them (proves point-space alignment,
    // not a 2×-off placement).
    QVERIFY2(blockView.intersects(QRectF(glyphPx)),
             "native block (via real docToView) must overlap the rendered glyphs");
    const QRectF inter = blockView.intersected(QRectF(glyphPx));
    const double coverage =
        (inter.width() * inter.height()) / (glyphPx.width() * glyphPx.height());
    QVERIFY2(coverage > 0.6,
             qPrintable(QStringLiteral("block/glyph overlap too small: %1 (blockView %2, glyphPx %3)")
                            .arg(coverage)
                            .arg(QStringLiteral("%1,%2 %3x%4")
                                     .arg(blockView.x()).arg(blockView.y())
                                     .arg(blockView.width()).arg(blockView.height()))
                            .arg(QStringLiteral("%1,%2 %3x%4")
                                     .arg(glyphPx.x()).arg(glyphPx.y())
                                     .arg(glyphPx.width()).arg(glyphPx.height()))));

    // And a real drag in VIEW space over the glyph region selects the
    // block: this exercises SelectableTextLayer's docToView-fed hit-test
    // end-to-end.
    SelectableTextLayer layer;
    layer.resize(raster.width() + 4, raster.height() + 4);
    layer.setStore(store);
    layer.setCurrentPage(0);
    layer.setDocToView([docToView](QPointF p, int) { return docToView(p); });
    const QString selected =
        layer.simulateDragForTest(QPointF(glyphPx.left() + 1, glyphPx.top() + 1),
                                  QPointF(glyphPx.right() - 1, glyphPx.bottom() - 1));
    QVERIFY2(selected.contains(QStringLiteral("Alignment")),
             qPrintable(QStringLiteral("view-space drag over glyphs missed the block, got: ") +
                        selected));
}

// R1 multi-line (reviewer #6): a two-line page produces per-line blocks in
// reading order with vertically ordered, non-overlapping rects — proving
// the getAllText index → getSelectionAtIndex per-line mapping on real
// multi-line content, not just a single line.
void TestAdapters::pdfDocumentNativeTextMultiLineOrdering() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("multiline.pdf");
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::Letter));
        // Resolution 72 → device units are PDF points, so the two lines
        // are ~150pt apart and Qt keeps them as distinct text lines (at the
        // default 1200 DPI the same pixel gap collapses to ~10pt and Qt
        // merges them into one line).
        writer.setResolution(72);
        QPainter painter(&writer);
        QFont f;
        f.setPixelSize(24);
        painter.setFont(f);
        painter.drawText(QRect(72, 100, 400, 40), Qt::AlignLeft, "First line alpha");
        painter.drawText(QRect(72, 250, 400, 40), Qt::AlignLeft, "Second line beta");
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    doc.ingestNativeTextLayer(0);
    const auto &blocks = doc.selectableText()->blocks(0);
    QVERIFY2(blocks.size() >= 2,
             qPrintable(QStringLiteral("expected >=2 line blocks, got %1").arg(blocks.size())));

    // Locate the alpha/beta lines regardless of block index order.
    int alphaIdx = -1, betaIdx = -1;
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].text.contains(QStringLiteral("alpha")))
            alphaIdx = static_cast<int>(i);
        if (blocks[i].text.contains(QStringLiteral("beta")))
            betaIdx = static_cast<int>(i);
    }
    QVERIFY2(alphaIdx >= 0, "first line ('alpha') must be its own block");
    QVERIFY2(betaIdx >= 0, "second line ('beta') must be its own block");
    QVERIFY2(alphaIdx != betaIdx, "the two lines must be distinct blocks");

    const QRect alphaR = blocks[static_cast<size_t>(alphaIdx)].polygon.boundingRect();
    const QRect betaR = blocks[static_cast<size_t>(betaIdx)].polygon.boundingRect();
    // The first line sits above the second, and their rects don't overlap
    // vertically (each line mapped to its own geometry, no bleed).
    QVERIFY2(alphaR.center().y() < betaR.center().y(),
             "first line must render above the second");
    QVERIFY2(alphaR.bottom() <= betaR.top(),
             "line rects must not overlap vertically");
}

// Owner dogfood report (2026-07-31): a scanned PDF with an invisible OCR
// text layer ("Has text layer: yes"; Find could locate text in it) did
// not respond to click-drag at all. Root-cause investigation: Qt's own
// getAllText()/getSelection() are RENDER-MODE-AGNOSTIC — real invisible
// text (`3 Tr`, what actual OCR tools emit) round-trips through both APIs
// exactly like normal visible text, verified against writeInvisibleRender
// ModePdf() below. Neither the ingestion path (ingestNativeTextLayer, the
// None-tool / SelectableTextLayer route) nor the getSelection()-based
// path (the Select-tool route AnnotationOverlay uses, PdfAdapter.cpp
// setTextSelectionProvider/setTextSelectionTextProvider) special-cases
// render mode, so this fixture is a REGRESSION GUARD, not a demonstration
// of a fix: if a future Qt/pdfium upgrade ever regresses invisible-text
// extraction, this fails loudly instead of silently reproducing the
// user's report. (The user's own 365-page document may still have a
// document-specific quirk this synthetic fixture can't reproduce without
// the file — see the PR description.)
void TestAdapters::pdfDocumentInvisibleRenderModeTextIsIngestedAndSelectable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("invisible.pdf");
    writeInvisibleRenderModePdf(path, QStringLiteral("InvisibleTrThreeKeyword"));

    PdfDocument doc(path);
    QVERIFY(doc.isValid());

    // Ingestion path (None-tool / SelectableTextLayer route).
    doc.ingestNativeTextLayer(0);
    auto *store = doc.selectableText();
    QVERIFY(store && store->hasResults(0));
    const auto &blocks = store->blocks(0);
    QVERIFY(!blocks.empty());
    QStringList parts;
    for (const auto &b : blocks)
        parts << b.text;
    const QString joined = parts.join(QLatin1Char(' '));
    QVERIFY2(joined.contains(QStringLiteral("InvisibleTrThreeKeyword")),
             qPrintable(QStringLiteral("invisible-text ingestion missed the word, got: ") + joined));

    // getSelection()-based path (Select-tool route). A tight rect around
    // the ingested block's own bounds — the same shape PdfAdapter's
    // setTextSelectionTextProvider drag lambda drives in production —
    // must resolve to non-empty, matching text. Loads its own QPdfDocument
    // (PdfDocument doesn't expose the underlying Qt object) — cheap for a
    // one-page fixture and keeps this a black-box check of the same Qt API
    // AnnotationOverlay's provider calls.
    const QRect region = blocks.front().polygon.boundingRect();
    QVERIFY(!region.isEmpty());
    QPdfDocument qdoc;
    QCOMPARE(qdoc.load(path), QPdfDocument::Error::None);
    const QPdfSelection sel =
        qdoc.getSelection(0, QPointF(region.topLeft()) + QPointF(1, 1),
                          QPointF(region.bottomRight()) - QPointF(1, 1));
    QVERIFY2(sel.isValid(), "getSelection() must succeed over an invisible (3 Tr) text run");
    QVERIFY2(sel.text().contains(QStringLiteral("InvisibleTrThreeKeyword")),
             qPrintable(QStringLiteral("getSelection() missed the word, got: ") + sel.text()));
}

void TestAdapters::printSupportReflectsValidity() {
    PdfDocument missing("/tmp/definitely-not-a-real-file.pdf");
    QVERIFY(!missing.supportsPrint());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = dir.filePath("tiny.png");
    QImage img(8, 8, QImage::Format_ARGB32);
    img.fill(Qt::green);
    QVERIFY(img.save(imagePath, "PNG"));

    ImageDocument image(imagePath);
    QVERIFY(image.supportsPrint());

    ImageDocument missingImage("/tmp/not-an-image.png");
    QVERIFY(!missingImage.supportsPrint());
}

void TestAdapters::pdfDocumentRotationMarksDirtyAndSaveClears() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("rot.pdf");

    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter, "hello");
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    QVERIFY(doc.supportsEditing());
    QVERIFY(!doc.isDirty());

    doc.rotatePage(0, 90);
    QVERIFY(doc.isDirty());
    QCOMPARE(doc.pageCount(), 1);

    QVERIFY(doc.save());
    QVERIFY(!doc.isDirty());

    PdfDocument round(path);
    QVERIFY(round.isValid());
    QCOMPARE(round.pageCount(), 1);
}

void TestAdapters::pdfDocumentDeletePagesRemovesAndMarksDirty() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("multi.pdf");

    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        for (int i = 0; i < 4; ++i) {
            painter.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter,
                             QStringLiteral("Page %1").arg(i + 1));
            if (i < 3)
                writer.newPage();
        }
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    QCOMPARE(doc.pageCount(), 4);

    doc.deletePages({1, 3});
    QVERIFY(doc.isDirty());
    QCOMPARE(doc.pageCount(), 2);

    QVERIFY(doc.save());
    PdfDocument round(path);
    QCOMPARE(round.pageCount(), 2);
}

void TestAdapters::pdfDocumentMovePageReorders() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("move.pdf");

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

    PdfDocument doc(path);
    QCOMPARE(doc.pageCount(), 3);
    doc.movePage(0, 2);
    QVERIFY(doc.isDirty());
    QCOMPARE(doc.pageCount(), 3);
}

namespace {

QString writeTinyPng(const QString &path, int w = 32, int h = 24, QColor colour = Qt::blue) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(colour);
    img.save(path, "PNG");
    return path;
}

} // namespace

void TestAdapters::imageDocumentRotateSwapsDimensionsAndMarksDirty() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("r.png"), 40, 20);

    ImageDocument doc(path);
    QVERIFY(doc.supportsEditing());
    QCOMPARE(doc.imagePixelSize(), QSize(40, 20));
    doc.rotatePage(0, 90);
    QVERIFY(doc.isDirty());
    QCOMPARE(doc.imagePixelSize(), QSize(20, 40));
}

void TestAdapters::imageDocumentFlipHorizontalMarksDirty() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("f.png"));

    ImageDocument doc(path);
    QVERIFY(!doc.isDirty());
    doc.flipHorizontal();
    QVERIFY(doc.isDirty());
    doc.flipVertical();
    QVERIFY(doc.isDirty());
}

void TestAdapters::imageDocumentResizeChangesPixelSize() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("s.png"), 64, 64);

    ImageDocument doc(path);
    QVERIFY(doc.resizeImage(16, 32, true));
    QCOMPARE(doc.imagePixelSize(), QSize(16, 32));
    QVERIFY(doc.isDirty());
    QVERIFY(!doc.resizeImage(0, 10, true));
}

void TestAdapters::imageDocumentCropReducesSize() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("c.png"), 100, 80);

    ImageDocument doc(path);
    QVERIFY(doc.cropToRect(10, 10, 50, 40));
    QCOMPARE(doc.imagePixelSize(), QSize(50, 40));
    QVERIFY(doc.isDirty());
    QVERIFY(!doc.cropToRect(1000, 1000, 10, 10));
}

void TestAdapters::imageDocumentAdjustColourModifiesPixels() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("a.png"), 8, 8, QColor(100, 100, 100));

    ImageDocument doc(path);
    QVERIFY(doc.adjustColour(0.5, 0.0, 0.0));
    QVERIFY(doc.isDirty());
}

void TestAdapters::imageDocumentSaveClearsDirty() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("sv.png"));

    ImageDocument doc(path);
    doc.flipHorizontal();
    QVERIFY(doc.isDirty());
    QVERIFY(doc.save());
    QVERIFY(!doc.isDirty());
}

void TestAdapters::imageDocumentExportAsJpegWritesFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeTinyPng(dir.filePath("src.png"));
    const QString dst = dir.filePath("out.jpg");

    ImageDocument doc(src);
    QVERIFY(doc.exportAs(dst, "jpg", 90));
    QVERIFY(QFileInfo(dst).size() > 0);
    QImage reloaded(dst);
    QVERIFY(!reloaded.isNull());
}

// Regression guard for the HITL ask "File > Export as PDF for image
// documents." An image must export as a genuine, openable one-page
// PDF (the "I have a photo of the form but my CPA wants a PDF" flow).
void TestAdapters::imageDocumentExportsImageAsSinglePagePdf() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = writeTinyPng(dir.filePath("card.png"), 80, 60);
    const QString dst = dir.filePath("card.pdf");

    ImageDocument doc(src);
    // exportAs returns false on a null image, so a true return also
    // confirms the source PNG loaded.
    QVERIFY2(doc.exportAs(dst, "pdf"),
             "Image documents must export to PDF (the 'my CPA wants a PDF' flow)");
    QVERIFY(QFileInfo(dst).size() > 0);

    // Must be a real, openable single-page PDF — not just bytes on
    // disk. Round-trip it through the PDF adapter to prove it.
    PdfDocument reopened(dst);
    QVERIFY2(reopened.isValid(), "Exported file must open as a valid PDF");
    QCOMPARE(reopened.pageCount(), 1);
}

void TestAdapters::imageDocumentUndoRestoresPriorState() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("u.png"), 40, 20);

    ImageDocument doc(path);
    QVERIFY(!doc.canUndo());
    doc.rotatePage(0, 90);
    QCOMPARE(doc.imagePixelSize(), QSize(20, 40));
    QVERIFY(doc.canUndo());
    doc.undo();
    QCOMPARE(doc.imagePixelSize(), QSize(40, 20));
    QVERIFY(!doc.canUndo());
    QVERIFY(doc.canRedo());
    doc.redo();
    QCOMPARE(doc.imagePixelSize(), QSize(20, 40));
    QVERIFY(!doc.canRedo());
}

void TestAdapters::imageDocumentSaveFlattensAnnotationsIntoPixels() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("flat.png"), 64, 48, Qt::blue);

    ImageDocument doc(path);
    QVERIFY(!doc.isDirty());

    Annotation rect;
    rect.page = 0;
    rect.type = AnnotationType::Rectangle;
    rect.bounds = QRectF(10, 10, 30, 20);
    rect.style.stroke = QColor(255, 0, 0);
    rect.style.strokeWidth = 3.0;
    doc.annotations()->add(std::move(rect));
    QVERIFY(doc.isDirty());

    QVERIFY(doc.save());
    QVERIFY(!doc.isDirty());

    QImage reloaded(path);
    QVERIFY(!reloaded.isNull());
    QCOMPARE(reloaded.size(), QSize(64, 48));

    bool foundRed = false;
    for (int y = 8; y <= 12 && !foundRed; ++y) {
        for (int x = 8; x <= 42 && !foundRed; ++x) {
            const QColor c = reloaded.pixelColor(x, y);
            if (c.red() > 180 && c.green() < 80 && c.blue() < 80) {
                foundRed = true;
            }
        }
    }
    QVERIFY2(foundRed, "expected rectangle stroke to appear in saved pixels");

    const QColor interior = reloaded.pixelColor(25, 20);
    QVERIFY2(interior.blue() > 180 && interior.red() < 80,
             "expected unstroked interior to remain the original blue");
}

// rotate → annotate → undo×2 pops in reverse-chronological order: the
// annotation (most recent) first, then the rotate. There is no
// domain-precedence rule — the unified log simply pops the newest
// entry regardless of which stack it lives on.
void TestAdapters::imageDocumentUndoPopsMostRecentAcrossDomains() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("p.png"), 40, 20);

    ImageDocument doc(path);
    QCOMPARE(doc.imagePixelSize(), QSize(40, 20));

    doc.rotatePage(0, 90);
    QCOMPARE(doc.imagePixelSize(), QSize(20, 40));

    Annotation rect;
    rect.page = 0;
    rect.type = AnnotationType::Rectangle;
    rect.bounds = QRectF(2, 2, 10, 10);
    doc.annotations()->add(std::move(rect));
    QCOMPARE(doc.annotations()->count(), 1);

    QVERIFY(doc.canUndo());
    doc.undo();
    QCOMPARE(doc.annotations()->count(), 0);
    QCOMPARE(doc.imagePixelSize(), QSize(20, 40));

    QVERIFY(doc.canUndo());
    doc.undo();
    QCOMPARE(doc.imagePixelSize(), QSize(40, 20));
}

// ImageDocument now shares PdfDocument's unified chronological log:
// interleaved annotation + pixel ops must undo in strict reverse
// order. The old dispatch drained ALL annotation undo first, so
// annotate → rotate → annotate undid both annotations before the
// rotate — a state the user never passed through.
void TestAdapters::imageDocumentInterleavedUndoIsChronological() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("chrono.png"), 40, 20);

    ImageDocument doc(path);
    AnnotationStore *store = doc.annotations();
    QVERIFY(store != nullptr);

    // #1 annotation → #2 rotate (pixel op) → #3 annotation
    Annotation a1;
    a1.page = 0;
    a1.type = AnnotationType::Rectangle;
    a1.bounds = QRectF(2, 2, 10, 8);
    store->add(a1);
    doc.rotatePage(0, 90);
    QCOMPARE(doc.imagePixelSize(), QSize(20, 40));
    Annotation a2;
    a2.page = 0;
    a2.type = AnnotationType::Ellipse;
    a2.bounds = QRectF(4, 4, 8, 6);
    store->add(a2);
    QCOMPARE(store->count(), 2);

    QVERIFY(doc.canUndo());
    QVERIFY(!doc.canRedo());

    // Reverse-chronological: #3, then #2, then #1.
    QVERIFY(doc.undo()); // reverse #3 (annotation)
    QCOMPARE(store->count(), 1);
    QCOMPARE(doc.imagePixelSize(), QSize(20, 40));

    QVERIFY(doc.undo()); // reverse #2 (rotate) — NOT another annotation
    QCOMPARE(store->count(), 1);
    QCOMPARE(doc.imagePixelSize(), QSize(40, 20));

    QVERIFY(doc.undo()); // reverse #1 (annotation)
    QCOMPARE(store->count(), 0);
    QCOMPARE(doc.imagePixelSize(), QSize(40, 20));

    QVERIFY(!doc.canUndo());
    QVERIFY(doc.canRedo());

    // Redo replays forward: #1, #2, #3.
    QVERIFY(doc.redo());
    QCOMPARE(store->count(), 1);
    QCOMPARE(doc.imagePixelSize(), QSize(40, 20));
    QVERIFY(doc.redo());
    QCOMPARE(store->count(), 1);
    QCOMPARE(doc.imagePixelSize(), QSize(20, 40));
    QVERIFY(doc.redo());
    QCOMPARE(store->count(), 2);
    QCOMPARE(doc.imagePixelSize(), QSize(20, 40));

    QVERIFY(doc.canUndo());
    QVERIFY(!doc.canRedo());
}

// Image mirror of pdfDocumentUndoRedoSurviveForcedLogDesync's
// annotation branches: ImageDocument::undo()/redo() carry the same
// warn + drop-orphan + return-false guards, forced the same cheap way
// (AnnotationStore::clearHistory() empties the store's stacks but
// deliberately not the document's log). The pixel branches have no
// seam; the annotation branches pin the pattern.
void TestAdapters::imageDocumentUndoRedoSurviveForcedLogDesync() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("desync.png"), 40, 20);

    // Undo branch: log holds an Annotation entry, store history empty.
    {
        ImageDocument doc(path);
        Annotation a;
        a.page = 0;
        a.type = AnnotationType::Rectangle;
        a.bounds = QRectF(5, 5, 20, 10);
        doc.annotations()->add(a);
        QVERIFY(doc.canUndo());
        doc.annotations()->clearHistory();
        QTest::ignoreMessage(QtWarningMsg,
                             "ImageDocument::undo: log expects an annotation frame but the "
                             "AnnotationStore history is empty; dropping the orphaned entry");
        QVERIFY2(!doc.undo(), "undo() must refuse when the store cannot deliver the frame");
        // The orphaned entry was dropped — the log no longer over-promises.
        QVERIFY(!doc.canUndo());
        // The annotation itself is untouched — the guard is a no-op,
        // not a partial mutation.
        QCOMPARE(doc.annotations()->count(), 1);
    }

    // Redo branch: undo a real annotation first so the redo log is
    // populated, then clear the store's stacks underneath it.
    {
        ImageDocument doc(path);
        Annotation a;
        a.page = 0;
        a.type = AnnotationType::Rectangle;
        a.bounds = QRectF(5, 5, 20, 10);
        doc.annotations()->add(a);
        QVERIFY(doc.undo());
        QVERIFY(doc.canRedo());
        doc.annotations()->clearHistory();
        QTest::ignoreMessage(QtWarningMsg,
                             "ImageDocument::redo: log expects an annotation frame but the "
                             "AnnotationStore redo history is empty; dropping the orphaned entry");
        QVERIFY2(!doc.redo(), "redo() must refuse on a log/store desync");
        QVERIFY(!doc.canRedo());
        QCOMPARE(doc.annotations()->count(), 0);
    }
}

// Mirror of pdfDocumentUndoAllPastOldCapRegimeIsExact for images: 70
// annotation edits + 1 rotate, undo-all must be exactly 71 real
// presses (no silent no-ops), redo-all exactly 71 (no phantoms).
void TestAdapters::imageDocumentUndoAllPastOldCapRegimeIsExact() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("overcap.png"), 40, 20);

    ImageDocument doc(path);
    AnnotationStore *store = doc.annotations();
    QVERIFY(store != nullptr);

    for (int i = 0; i < 70; ++i) {
        Annotation a;
        a.page = 0;
        a.type = AnnotationType::Rectangle;
        a.bounds = QRectF(i % 30, i % 10, 6, 4);
        store->add(a);
    }
    QCOMPARE(store->count(), 70);
    doc.rotatePage(0, 90);
    QCOMPARE(doc.imagePixelSize(), QSize(20, 40));

    int undos = 0;
    while (doc.canUndo()) {
        QVERIFY2(doc.undo(), "undo() returned false while canUndo() was true");
        ++undos;
        QVERIFY2(undos <= 71, "undo log offered more entries than operations performed");
    }
    QCOMPARE(undos, 71);
    QCOMPARE(store->count(), 0);
    QCOMPARE(doc.imagePixelSize(), QSize(40, 20));
    QVERIFY(!doc.canUndo());
    QVERIFY2(!doc.undo(), "undo() must refuse once canUndo() is false");

    int redos = 0;
    while (doc.canRedo()) {
        QVERIFY2(doc.redo(), "redo() returned false while canRedo() was true");
        ++redos;
        QVERIFY2(redos <= undos, "phantom redo entries beyond the number of undos");
    }
    QCOMPARE(redos, 71);
    QCOMPARE(store->count(), 70);
    QCOMPARE(doc.imagePixelSize(), QSize(20, 40));
    QVERIFY(!doc.canRedo());
}

// The pixel snapshot stack has its own cap (kMaxUndoSteps = 32 in
// ImageAdapter.cpp — full QImage copies are much heavier than
// annotation frames). Past it, eviction must stay in lockstep with
// the chronological log exactly like the annotation domain: 35 crops
// after 1 annotation leaves 32 + 1 = 33 real undos, no more offered,
// none a silent no-op.
void TestAdapters::imageDocumentPixelCapEvictionKeepsLogInLockstep() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("pixcap.png"), 300, 100);

    ImageDocument doc(path);
    AnnotationStore *store = doc.annotations();
    QVERIFY(store != nullptr);

    Annotation a;
    a.page = 0;
    a.type = AnnotationType::Rectangle;
    a.bounds = QRectF(5, 5, 20, 10);
    store->add(a);

    // Each crop narrows the image by one pixel so every step (and the
    // exact undo depth) is observable in the width.
    for (int i = 0; i < 35; ++i) {
        const int w = doc.imagePixelSize().width();
        QVERIFY(doc.cropToRect(0, 0, w - 1, 100));
    }
    QCOMPARE(doc.imagePixelSize(), QSize(265, 100));

    // Undoable: 32 retained crop snapshots + the annotation = 33.
    int undos = 0;
    while (doc.canUndo()) {
        QVERIFY2(doc.undo(), "undo() returned false while canUndo() was true");
        ++undos;
        QVERIFY2(undos <= 33, "log offered more undos than the snapshot stack retains");
    }
    QCOMPARE(undos, 33);
    // The 3 oldest crops fell off the capped stack — deliberately
    // unreachable, so the width lands at 300 - 3, not 300.
    QCOMPARE(doc.imagePixelSize(), QSize(297, 100));
    QCOMPARE(store->count(), 0);
    QVERIFY(!doc.canUndo());
    QVERIFY2(!doc.undo(), "undo() must refuse once canUndo() is false");

    int redos = 0;
    while (doc.canRedo()) {
        QVERIFY2(doc.redo(), "redo() returned false while canRedo() was true");
        ++redos;
        QVERIFY2(redos <= undos, "phantom redo entries beyond the number of undos");
    }
    QCOMPARE(redos, 33);
    QCOMPARE(doc.imagePixelSize(), QSize(265, 100));
    QCOMPARE(store->count(), 1);
    QVERIFY(!doc.canRedo());
}

// Annotation-domain eviction sync on an image document, at a small
// injected cap (mirror of the PdfDocument small-cap test): the oldest
// pixel op must stay reachable while excess annotation frames evict
// from both the store and the log.
void TestAdapters::imageDocumentSmallCapAnnotationEvictionKeepsLogInLockstep() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("smallcap.png"), 40, 20);

    ImageDocument doc(path);
    AnnotationStore *store = doc.annotations();
    QVERIFY(store != nullptr);
    store->setMaxUndoDepth(5);

    doc.rotatePage(0, 90);
    QCOMPARE(doc.imagePixelSize(), QSize(20, 40));
    for (int i = 0; i < 10; ++i) {
        Annotation a;
        a.page = 0;
        a.type = AnnotationType::Rectangle;
        a.bounds = QRectF(i, i, 6, 4);
        store->add(a);
    }
    QCOMPARE(store->count(), 10);

    // Undoable: 5 retained annotation frames + the rotate = 6.
    int undos = 0;
    while (doc.canUndo()) {
        QVERIFY2(doc.undo(), "undo() returned false while canUndo() was true");
        ++undos;
        QVERIFY2(undos <= 6, "log offered more undos than the store retains");
    }
    QCOMPARE(undos, 6);
    QCOMPARE(store->count(), 5);
    QCOMPARE(doc.imagePixelSize(), QSize(40, 20));
    QVERIFY(!doc.canUndo());

    int redos = 0;
    while (doc.canRedo()) {
        QVERIFY2(doc.redo(), "redo() returned false while canRedo() was true");
        ++redos;
        QVERIFY2(redos <= undos, "phantom redo entries beyond the number of undos");
    }
    QCOMPARE(redos, 6);
    QCOMPARE(store->count(), 10);
    QCOMPARE(doc.imagePixelSize(), QSize(20, 40));
    QVERIFY(!doc.canRedo());
}

void TestAdapters::imageDocumentFitModeStartsCustom() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("fm.png"), 32, 32);

    ImageDocument doc(path);
    QCOMPARE(doc.zoomMode(), ZoomMode::Custom);
}

void TestAdapters::imageDocumentZoomFitPageEntersFitInViewMode() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("fp.png"), 100, 50);

    ImageDocument doc(path);
    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    // Staged open (ADR 0008): await the off-thread decode swap before the
    // fit math, which needs the decoded image size.
    QVERIFY(doc.awaitDecodeForTest());
    auto *scroll = qobject_cast<QScrollArea *>(view.get());
    QVERIFY(scroll != nullptr);
    // Force a viewport size — without a real show()/event-loop tick
    // the QScrollArea has 0×0 viewport on macOS offscreen.
    scroll->resize(400, 200);
    scroll->viewport()->resize(400, 200);

    doc.zoomFitPage();
    QCOMPARE(doc.zoomMode(), ZoomMode::FitInView);
    // 100×50 inside a 400×200 viewport: width is the tighter dimension
    // (200/50 = 4, 400/100 = 4 — equal). Scale should be 4.
    QVERIFY(qFuzzyCompare(doc.scaleFactor(), 4.0));
}

void TestAdapters::imageDocumentZoomFitWidthEntersFitToWidthMode() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("fw.png"), 50, 200);

    ImageDocument doc(path);
    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    QVERIFY(doc.awaitDecodeForTest()); // ADR 0008: await the off-thread decode swap
    auto *scroll = qobject_cast<QScrollArea *>(view.get());
    QVERIFY(scroll != nullptr);
    scroll->resize(500, 100);
    scroll->viewport()->resize(500, 100);

    doc.zoomFitWidth();
    QCOMPARE(doc.zoomMode(), ZoomMode::FitToWidth);
    // 50px wide image filling a 500px viewport → scale 10.
    QVERIFY(qFuzzyCompare(doc.scaleFactor(), 10.0));
}

void TestAdapters::imageDocumentExplicitZoomReturnsToCustomMode() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("ez.png"), 40, 40);

    ImageDocument doc(path);
    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    QVERIFY(doc.awaitDecodeForTest()); // ADR 0008: await the off-thread decode swap
    auto *scroll = qobject_cast<QScrollArea *>(view.get());
    QVERIFY(scroll != nullptr);
    scroll->resize(200, 200);
    scroll->viewport()->resize(200, 200);

    doc.zoomFitPage();
    QCOMPARE(doc.zoomMode(), ZoomMode::FitInView);

    doc.zoomIn();
    QCOMPARE(doc.zoomMode(), ZoomMode::Custom);

    doc.zoomFitWidth();
    QCOMPARE(doc.zoomMode(), ZoomMode::FitToWidth);

    doc.zoomOut();
    QCOMPARE(doc.zoomMode(), ZoomMode::Custom);

    doc.zoomFitPage();
    doc.zoomActual();
    // zoomActual() reports the Actual mode rather than Custom — the
    // user expressed "100%" as an intent, distinct from "I picked
    // a custom factor that happens to be 1.0" (which Custom captures).
    QCOMPARE(doc.zoomMode(), ZoomMode::Actual);
}

void TestAdapters::imageDocumentZoomInStepsByQuarter() {
    // Guards the zoom-step ratio (kZoomStep). One zoomIn from Actual
    // Size (1.0) should land at ~1.25 (25% coarser step, up from the
    // former 10%). Kept in sync with the PDF path below.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("step.png"), 60, 60);

    ImageDocument doc(path);
    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    QVERIFY(view != nullptr);
    QVERIFY(doc.awaitDecodeForTest()); // ADR 0008: await the off-thread decode swap

    doc.zoomActual();
    QVERIFY(qFuzzyCompare(doc.scaleFactor(), 1.0));
    doc.zoomIn();
    QVERIFY2(std::abs(doc.scaleFactor() - 1.25) < 1e-6,
             qPrintable(QStringLiteral("expected ~1.25 after one zoomIn, got %1")
                            .arg(doc.scaleFactor())));
}

void TestAdapters::pdfDocumentZoomInStepsByQuarter() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("step.pdf");
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter, "page");
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    QVERIFY(view != nullptr);

    doc.zoomActual();
    QVERIFY(qFuzzyCompare(doc.zoomFactor(), 1.0));
    doc.zoomIn();
    QVERIFY2(std::abs(doc.zoomFactor() - 1.25) < 1e-6,
             qPrintable(QStringLiteral("expected ~1.25 after one zoomIn, got %1")
                            .arg(doc.zoomFactor())));
}

void TestAdapters::imageDocumentReapplyFitModeRefitsOnResize() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("rf.png"), 50, 50);

    ImageDocument doc(path);
    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    QVERIFY(doc.awaitDecodeForTest()); // ADR 0008: await the off-thread decode swap
    auto *scroll = qobject_cast<QScrollArea *>(view.get());
    QVERIFY(scroll != nullptr);
    scroll->resize(200, 200);
    scroll->viewport()->resize(200, 200);

    doc.zoomFitPage();
    const double scaleBefore = doc.scaleFactor();
    QVERIFY(qFuzzyCompare(scaleBefore, 4.0));

    // Grow the viewport; reapplyFitMode should re-fit the image to
    // the new size. Simulates the user resizing the window.
    scroll->viewport()->resize(400, 400);
    doc.reapplyFitMode();
    const double scaleAfter = doc.scaleFactor();
    QVERIFY2(qFuzzyCompare(scaleAfter, 8.0),
             qPrintable(QStringLiteral("expected refit to 8.0 after resize, got %1")
                            .arg(scaleAfter)));
}

void TestAdapters::pdfViewReflowsOnResizeInFitInView() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("fit.pdf");

    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter, "page");
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());

    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    QVERIFY(view != nullptr);
    // Since the two-page AUGMENT wiring (decision record
    // 2026-07-21-two-page-layout, D1-A), createView returns a QStackedWidget
    // hosting the QPdfView (Single/Continuous) and the custom TwoPageView, so
    // reach the QPdfView by search rather than by casting the returned widget.
    auto *pdfView = view->findChild<QPdfView *>();
    QVERIFY2(pdfView != nullptr, "expected createView to host a QPdfView");

    // Empirical check of QPdfView's behaviour in FitInView mode. As
    // of Qt 6.11, QPdfView::zoomFactor() always returns the
    // user-supplied factor (default 1.0) even when zoomMode is a
    // fit mode — the rendered scale is computed internally per paint
    // and not exposed. So we observe the vertical scroll range:
    // - If FitInView reflows on resize, the page always fits the
    //   viewport and the scrollbar range is small / unchanged with
    //   the viewport area.
    // - If FitInView didn't reflow, shrinking the viewport would
    //   leave the same render-pixel content in a smaller viewport
    //   and the scroll range would balloon.
    //
    // We force SinglePage so there's exactly one page to fit; the
    // default Continuous mode adds inter-page spacing and an
    // inherent extra scroll length unrelated to the fit math.
    pdfView->setPageMode(QPdfView::PageMode::SinglePage);
    // Show/resize through the wrapping QStackedWidget so its stacked layout
    // sizes the hosted QPdfView to the viewport (a direct child resize would be
    // overridden by that layout). The QPdfView is the stack's current widget in
    // SinglePage/Continuous, so it fills the stack.
    view->show();
    view->resize(800, 600);
    pdfView->setZoomMode(QPdfView::ZoomMode::FitInView);
    QCoreApplication::processEvents();
    QTest::qWait(50);
    const int largeRange =
        pdfView->verticalScrollBar()->maximum() - pdfView->verticalScrollBar()->minimum();

    view->resize(200, 150);
    QCoreApplication::processEvents();
    QTest::qWait(50);
    const int smallRange =
        pdfView->verticalScrollBar()->maximum() - pdfView->verticalScrollBar()->minimum();

    // Both scroll ranges should be small/comparable because FitInView
    // always sizes the page to fit. A non-reflowing implementation
    // would leave the large-render pixels in a 200×150 viewport,
    // producing a very large vertical scroll range. We test the
    // signal is bounded rather than zero (Qt's centring + page
    // margins can produce a tiny non-zero range).
    QVERIFY2(smallRange < 100,
             qPrintable(QStringLiteral("FitInView appears not to reflow on shrink: "
                                       "small viewport scroll range = %1")
                            .arg(smallRange)));
    QVERIFY2(largeRange < 100,
             qPrintable(QStringLiteral("FitInView produced unexpected scroll range: %1")
                            .arg(largeRange)));
}

void TestAdapters::pdfDownArrowStepsPageImmediatelyInFitMode() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("multi.pdf");

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

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    QCOMPARE(doc.pageCount(), 3);
    // SinglePage so Down/PageDown go through the per-page step path
    // rather than the QAbstractScrollArea-level continuous scroll.
    doc.setViewMode(ViewMode::SinglePage);

    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    // createView returns a QStackedWidget wrapping the QPdfView since the
    // two-page AUGMENT wiring (D1-A); find the QPdfView within it.
    auto *pdfView = view->findChild<QPdfView *>();
    QVERIFY(pdfView != nullptr);
    // Show/resize through the wrapping QStackedWidget so its stacked layout
    // sizes the hosted QPdfView (its current widget in SinglePage) to 800x600.
    view->show();
    view->resize(800, 600);
    QCoreApplication::processEvents();

    // Enter fit-to-page mode. In this mode the entire page should
    // fit the viewport, so Down should step immediately to the next
    // page rather than scrolling first.
    doc.zoomFitPage();
    QCoreApplication::processEvents();
    QTest::qWait(20);
    QCOMPARE(doc.currentPage(), 0);

    // Send a Down key event directly to the view.
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QCoreApplication::sendEvent(pdfView, &down);
    QCoreApplication::processEvents();
    QTest::qWait(20);
    QCOMPARE(doc.currentPage(), 1);

    QKeyEvent down2(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QCoreApplication::sendEvent(pdfView, &down2);
    QCoreApplication::processEvents();
    QTest::qWait(20);
    QCOMPARE(doc.currentPage(), 2);

    // At the last page, Down should NOT step further (no wrap).
    QKeyEvent down3(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QCoreApplication::sendEvent(pdfView, &down3);
    QCoreApplication::processEvents();
    QTest::qWait(20);
    QCOMPARE(doc.currentPage(), 2);

    // Up should walk back without first scrolling.
    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QCoreApplication::sendEvent(pdfView, &up);
    QCoreApplication::processEvents();
    QTest::qWait(20);
    QCOMPARE(doc.currentPage(), 1);
}

void TestAdapters::imageDocumentResizeDoesNothingInCustomMode() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("cust.png"), 50, 50);

    ImageDocument doc(path);
    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    QVERIFY(doc.awaitDecodeForTest()); // ADR 0008: await the off-thread decode swap
    auto *scroll = qobject_cast<QScrollArea *>(view.get());
    QVERIFY(scroll != nullptr);
    scroll->resize(200, 200);
    scroll->viewport()->resize(200, 200);

    doc.zoomIn();
    QCOMPARE(doc.zoomMode(), ZoomMode::Custom);
    const double scaleBefore = doc.scaleFactor();

    // Custom mode should NOT track viewport changes — the user's
    // explicit zoom factor stays put.
    scroll->viewport()->resize(400, 400);
    doc.reapplyFitMode();
    QCOMPARE(doc.scaleFactor(), scaleBefore);
}

void TestAdapters::imageDocumentResizeEventTriggersRefit() {
    // Exercises the resize-watcher wiring end-to-end: deliver an
    // actual QResizeEvent to the viewport and check the scale tracks
    // it without manually calling reapplyFitMode(). This is the
    // regression guard for "the eventFilter is actually installed
    // and routes to reapplyFitMode."
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("e2e.png"), 80, 40);

    ImageDocument doc(path);
    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    QVERIFY(doc.awaitDecodeForTest()); // ADR 0008: await the off-thread decode swap
    auto *scroll = qobject_cast<QScrollArea *>(view.get());
    QVERIFY(scroll != nullptr);
    scroll->resize(400, 200);
    scroll->viewport()->resize(400, 200);

    doc.zoomFitPage();
    const double scaleBefore = doc.scaleFactor();
    QVERIFY(qFuzzyCompare(scaleBefore, 5.0));

    // Resize via an actual event delivery — what Qt does on a real
    // window resize.
    QResizeEvent resizeEv(QSize(800, 400), QSize(400, 200));
    QCoreApplication::sendEvent(scroll->viewport(), &resizeEv);
    // The event handler reads the viewport's current size, so set
    // it explicitly to match the new size as Qt would before the
    // event fires.
    scroll->viewport()->resize(800, 400);
    QResizeEvent resizeEv2(QSize(800, 400), QSize(400, 200));
    QCoreApplication::sendEvent(scroll->viewport(), &resizeEv2);
    const double scaleAfter = doc.scaleFactor();
    QVERIFY2(qFuzzyCompare(scaleAfter, 10.0),
             qPrintable(QStringLiteral("expected 10.0 after resize event, got %1")
                            .arg(scaleAfter)));
}

// Regression guard for the unified chronological undo log (roadmap
// Now #4). Interleaving a qpdf page op, an annotation edit, then another
// page op must undo in strict reverse-chronological order — the old
// most-recently-touched-stack heuristic undid both page ops first and
// the annotation last, so after two undos the document was in a state
// the user never passed through.
void TestAdapters::pdfDocumentInterleavedUndoIsChronological() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("interleaved.pdf");
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.drawText(QRect(100, 100, 400, 100), Qt::AlignCenter, "Page 1");
        writer.newPage();
        painter.drawText(QRect(100, 100, 400, 100), Qt::AlignCenter, "Page 2");
        writer.newPage();
        painter.drawText(QRect(100, 100, 400, 100), Qt::AlignCenter, "Page 3");
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    QCOMPARE(doc.pageCount(), 3);
    AnnotationStore *store = doc.annotations();
    QVERIFY(store != nullptr);
    QCOMPARE(store->count(), 0);

    // #1 delete a page (qpdf)  →  #2 add an annotation  →  #3 delete a page
    doc.deletePages({2});
    QCOMPARE(doc.pageCount(), 2);

    Annotation note;
    note.page = 0;
    note.type = AnnotationType::Rectangle;
    note.bounds = QRectF(10, 10, 40, 30);
    store->add(note);
    QCOMPARE(store->count(), 1);

    doc.deletePages({1});
    QCOMPARE(doc.pageCount(), 1);

    QVERIFY(doc.canUndo());
    QVERIFY(!doc.canRedo());

    // Reverse-chronological: #3, then #2, then #1.
    doc.undo(); // reverse #3 (page delete)
    QCOMPARE(doc.pageCount(), 2);
    QCOMPARE(store->count(), 1);

    doc.undo(); // reverse #2 (annotation) — NOT another page op
    QCOMPARE(doc.pageCount(), 2);
    QCOMPARE(store->count(), 0);

    doc.undo(); // reverse #1 (page delete)
    QCOMPARE(doc.pageCount(), 3);
    QCOMPARE(store->count(), 0);

    QVERIFY(!doc.canUndo());
    QVERIFY(doc.canRedo());

    // Redo replays forward: #1, #2, #3.
    doc.redo();
    QCOMPARE(doc.pageCount(), 2);
    QCOMPARE(store->count(), 0);

    doc.redo();
    QCOMPARE(doc.pageCount(), 2);
    QCOMPARE(store->count(), 1);

    doc.redo();
    QCOMPARE(doc.pageCount(), 1);
    QCOMPARE(store->count(), 1);

    QVERIFY(doc.canUndo());
    QVERIFY(!doc.canRedo());
}

// Regression guard for the cap-desync bug: AnnotationStore bounds its
// history while the unified chronological log used to grow without
// bound, so after more annotation edits than the (old, 64-frame) cap
// an undo-all silently no-opped the excess and pushed phantom redo
// entries. 70 edits + 1 rotate exercises the old >64 regime; every
// offered undo/redo must be real and the counts must balance exactly.
void TestAdapters::pdfDocumentUndoAllPastOldCapRegimeIsExact() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("overcap.pdf");
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.drawText(QRect(100, 100, 400, 100), Qt::AlignCenter, "over-cap");
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    AnnotationStore *store = doc.annotations();
    QVERIFY(store != nullptr);
    const QSize portrait = doc.contentSizeHint();
    QVERIFY2(portrait.height() > portrait.width(), "A4 fixture should start portrait");

    // 70 annotation edits, then one page rotate (the most recent op).
    for (int i = 0; i < 70; ++i) {
        Annotation a;
        a.page = 0;
        a.type = AnnotationType::Rectangle;
        a.bounds = QRectF(i, i, 20, 10);
        store->add(a);
    }
    QCOMPARE(store->count(), 70);
    doc.rotatePage(0, 90);
    const QSize landscape = doc.contentSizeHint();
    QVERIFY2(landscape.width() > landscape.height(),
             "rotate 90 should present the page landscape");

    // Undo-all: while canUndo() reports true every press must actually
    // revert something — a false return here is the silent-no-op bug.
    int undos = 0;
    while (doc.canUndo()) {
        QVERIFY2(doc.undo(), "undo() returned false while canUndo() was true");
        ++undos;
        QVERIFY2(undos <= 71, "undo log offered more entries than operations performed");
    }
    QCOMPARE(undos, 71);
    QCOMPARE(store->count(), 0);
    QCOMPARE(doc.contentSizeHint(), portrait);
    QVERIFY(!doc.canUndo());
    QVERIFY2(!doc.undo(), "undo() must refuse (return false) once canUndo() is false");
    QCOMPARE(store->count(), 0);

    // Redo-all: exactly as many redos as undos performed — any extra
    // claimed entry is a phantom.
    int redos = 0;
    while (doc.canRedo()) {
        QVERIFY2(doc.redo(), "redo() returned false while canRedo() was true");
        ++redos;
        QVERIFY2(redos <= undos, "phantom redo entries beyond the number of undos");
    }
    QCOMPARE(redos, 71);
    QCOMPARE(store->count(), 70);
    QCOMPARE(doc.contentSizeHint(), landscape);
    QVERIFY(!doc.canRedo());
    QVERIFY2(!doc.redo(), "redo() must refuse (return false) once canRedo() is false");
}

// Proves the eviction-sync mechanism at an arbitrary cap: with the
// store's depth cap shrunk to 5 (test seam), edits past the cap must
// evict from BOTH the store and the document's chronological log, so
// canUndo() never over-promises. The oldest page op must stay
// reachable — eviction removes the oldest entry of the matching
// domain, not the oldest entry overall.
void TestAdapters::pdfDocumentSmallCapEvictionKeepsLogAndStoreInLockstep() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("smallcap.pdf");
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.drawText(QRect(100, 100, 400, 100), Qt::AlignCenter, "small-cap");
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    AnnotationStore *store = doc.annotations();
    QVERIFY(store != nullptr);
    store->setMaxUndoDepth(5);
    const QSize portrait = doc.contentSizeHint();

    // Chronologically first: a rotate. Then 10 annotation edits — 5
    // more than the store can retain, forcing 5 evictions.
    doc.rotatePage(0, 90);
    for (int i = 0; i < 10; ++i) {
        Annotation a;
        a.page = 0;
        a.type = AnnotationType::Rectangle;
        a.bounds = QRectF(i, i, 20, 10);
        store->add(a);
    }
    QCOMPARE(store->count(), 10);

    // Undoable: 5 retained annotation frames + the rotate = 6.
    int undos = 0;
    while (doc.canUndo()) {
        QVERIFY2(doc.undo(), "undo() returned false while canUndo() was true");
        ++undos;
        QVERIFY2(undos <= 6, "log offered more undos than the store retains");
    }
    QCOMPARE(undos, 6);
    // The 5 oldest annotations fell off the capped history — they are
    // deliberately unreachable, not silently skipped.
    QCOMPARE(store->count(), 5);
    QCOMPARE(doc.contentSizeHint(), portrait);
    QVERIFY(!doc.canUndo());
    QVERIFY2(!doc.undo(), "undo() must refuse once canUndo() is false");

    int redos = 0;
    while (doc.canRedo()) {
        QVERIFY2(doc.redo(), "redo() returned false while canRedo() was true");
        ++redos;
        QVERIFY2(redos <= undos, "phantom redo entries beyond the number of undos");
    }
    QCOMPARE(redos, 6);
    QCOMPARE(store->count(), 10);
    QVERIFY(!doc.canRedo());
}

// The undo dispatch used to guard the "log names a PdfCommand but the
// command stack is empty" desync with Q_ASSERT only — compiled out
// under NDEBUG (RelWithDebInfo and Release both define it), leaving
// .back() on an empty vector: UB. The guards are now runtime checks:
// warn, drop the orphaned log entry, return false, never crash. There
// is no production path to this state, so it is forced through test
// seams: corruptPdfCommandStacksForTesting() for the command branch
// and AnnotationStore::clearHistory() for the annotation branch.
void TestAdapters::pdfDocumentUndoRedoSurviveForcedLogDesync() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("desync.pdf");
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.drawText(QRect(100, 100, 400, 100), Qt::AlignCenter, "desync");
        painter.end();
    }

    // Undo branch: log holds a PdfCommand entry, command stack empty.
    {
        PdfDocument doc(path);
        QVERIFY(doc.isValid());
        doc.rotatePage(0, 90);
        QVERIFY(doc.canUndo());
        doc.corruptPdfCommandStacksForTesting();
        QTest::ignoreMessage(QtWarningMsg,
                             "PdfDocument::undo: log expects a PdfCommand but the command "
                             "stack is empty; dropping the orphaned entry");
        QVERIFY2(!doc.undo(), "undo() must refuse on a log/stack desync");
        // The orphaned entry was dropped — the log no longer over-promises.
        QVERIFY(!doc.canUndo());
        QVERIFY(!doc.canRedo());
    }

    // Redo branch: undo a real rotate first so the redo log is
    // populated, then drop the command stacks underneath it.
    {
        PdfDocument doc(path);
        QVERIFY(doc.isValid());
        doc.rotatePage(0, 90);
        QVERIFY(doc.undo());
        QVERIFY(doc.canRedo());
        doc.corruptPdfCommandStacksForTesting();
        QTest::ignoreMessage(QtWarningMsg,
                             "PdfDocument::redo: log expects a PdfCommand but the command "
                             "stack is empty; dropping the orphaned entry");
        QVERIFY2(!doc.redo(), "redo() must refuse on a log/stack desync");
        QVERIFY(!doc.canRedo());
    }

    // Annotation branch: clearHistory() empties the store's stacks but
    // deliberately not the document's log — a corrupted sequence the
    // guard must absorb the same way.
    {
        PdfDocument doc(path);
        QVERIFY(doc.isValid());
        Annotation a;
        a.page = 0;
        a.type = AnnotationType::Rectangle;
        a.bounds = QRectF(5, 5, 20, 10);
        doc.annotations()->add(a);
        QVERIFY(doc.canUndo());
        doc.annotations()->clearHistory();
        QTest::ignoreMessage(QtWarningMsg,
                             "PdfDocument::undo: log expects an annotation frame but the "
                             "AnnotationStore history is empty; dropping the orphaned entry");
        QVERIFY2(!doc.undo(), "undo() must refuse when the store cannot deliver the frame");
        QVERIFY(!doc.canUndo());
        // The annotation itself is untouched — the guard is a no-op,
        // not a partial mutation.
        QCOMPARE(doc.annotations()->count(), 1);
    }
}

// Pins the one hand-maintained sync point of the 1:1 log/stack
// invariant: PdfDocument::saveCommitOnUi() reloads annotations from
// the saved file under m_suppressUndoLog (the re-read must not mint
// user undo steps), clears the store's history, and rebuilds the
// unified log from the RETAINED qpdf command stacks
// (m_undoLog.assign(m_pdfUndoStack.size(), PdfCommand)). After
// save + reload, undo must offer exactly the surviving page commands
// — every press real, no orphaned annotation entries — and redo must
// replay them.
void TestAdapters::pdfDocumentSaveReloadRebuildsUndoLogFromRetainedStacks() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("savereload.pdf");
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.drawText(QRect(100, 100, 400, 100), Qt::AlignCenter, "save-reload");
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    AnnotationStore *store = doc.annotations();
    QVERIFY(store != nullptr);
    const QSize portrait = doc.contentSizeHint();
    QVERIFY2(portrait.height() > portrait.width(), "A4 fixture should start portrait");

    // One qpdf command + one annotation edit before saving.
    doc.rotatePage(0, 90);
    const QSize landscape = doc.contentSizeHint();
    QVERIFY(landscape.width() > landscape.height());
    Annotation a;
    a.page = 0;
    a.type = AnnotationType::Rectangle;
    a.bounds = QRectF(10, 10, 40, 30);
    store->add(a);
    QCOMPARE(store->count(), 1);

    QVERIFY(doc.save());
    QVERIFY(!doc.isDirty());
    // The reload re-read the annotation from the saved file...
    QCOMPARE(store->count(), 1);
    // ...but under suppression: the store's history is cleared, the
    // qpdf command stack is retained, so the rebuilt log offers
    // exactly ONE undo (the rotate) — no orphaned annotation entry
    // that undo() would have to warn about and refuse.
    int undos = 0;
    while (doc.canUndo()) {
        QVERIFY2(doc.undo(), "undo() returned false while canUndo() was true — the "
                             "rebuilt log does not match the rebuilt stacks");
        ++undos;
        QVERIFY2(undos <= 1, "rebuilt log offered more undos than retained pdf commands");
    }
    QCOMPARE(undos, 1);
    QCOMPARE(doc.contentSizeHint(), portrait);
    // The reloaded annotation is not an undo step; it survives.
    QCOMPARE(store->count(), 1);
    QVERIFY2(!doc.undo(), "undo() must refuse once canUndo() is false");

    // Redo replays the undone page command through the same rebuilt
    // bookkeeping.
    int redos = 0;
    while (doc.canRedo()) {
        QVERIFY2(doc.redo(), "redo() returned false while canRedo() was true");
        ++redos;
        QVERIFY2(redos <= undos, "phantom redo entries beyond the number of undos");
    }
    QCOMPARE(redos, 1);
    QCOMPARE(doc.contentSizeHint(), landscape);
    QCOMPARE(store->count(), 1);
    QVERIFY(!doc.canRedo());
}

// BLOCKER B1 regression guard: data loss on undo after an edit made during
// the asynchronous annotation-load window.
//
// The sweep now loads off-thread; commitAnnotations() populates the store via
// addBatch() (no undo frame). AnnotationStore undo is SNAPSHOT-based: an add()
// snapshots the WHOLE vector before appending. The interleaving that loses
// data:
//   1. annotations() kicks the async load; the store is still empty.
//   2. During the load window the user draws — add() snapshots the EMPTY
//      pre-draw state, then appends the user annotation.
//   3. The load completes — addBatch APPENDS the file's annotations, no frame.
//   4. Undo restores the empty snapshot → every ORIGINAL file annotation is
//      wiped (and lost on the next save).
//
// This test opens a genuinely annotated PDF, edits DURING the load window, lets
// the load commit, then undoes — and asserts the original file annotations
// survive while only the user's draw is reverted. Against 7a53ad9 (no pre-edit
// baseline commit) step 4 wipes them and this FAILS; the fix (AnnotationStore
// pre-edit hook forcing the loaded baseline to commit before the first user
// edit snapshots) makes it pass.
void TestAdapters::pdfAnnotationUndoAfterInWindowEditPreservesLoadedAnnotations() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Build a base PDF, then stamp two real /Annots into it via PdfEditor so
    // the off-thread sweep has something to load.
    const QString base = dir.filePath("base.pdf");
    {
        QPdfWriter writer(base);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.drawText(QRect(100, 100, 400, 100), Qt::AlignCenter, "annotated");
        painter.end();
    }
    const QString annotated = dir.filePath("annotated.pdf");
    {
        PdfEditor editor;
        QVERIFY(editor.load(base));
        std::vector<Annotation> anns;
        Annotation rect;
        rect.page = 0;
        rect.type = AnnotationType::Rectangle;
        rect.bounds = QRectF(50, 60, 120, 80);
        anns.push_back(rect);
        Annotation ell;
        ell.page = 0;
        ell.type = AnnotationType::Ellipse;
        ell.bounds = QRectF(100, 100, 80, 40);
        anns.push_back(ell);
        QVERIFY(editor.writeAnnotations(anns));
        QVERIFY(editor.save(annotated));
    }

    PdfDocument doc(annotated);
    QVERIFY(doc.isValid());

    AnnotationStore *store = doc.annotations(); // kicks the async load
    QVERIFY(store != nullptr);
    // The off-thread sweep has not committed yet — store is empty and the
    // history hooks are already wired, so the edit below is tracked.
    QVERIFY(store->isEmpty());

    // The user draws BEFORE the async commit lands.
    Annotation user;
    user.page = 0;
    user.type = AnnotationType::Arrow;
    user.bounds = QRectF(5, 5, 10, 10);
    user.points = {QPointF(5, 5), QPointF(15, 15)};
    const int userId = store->add(user);
    QVERIFY(userId > 0);

    // Let the off-thread load commit (with the fix the pre-edit hook already
    // forced this synchronously inside add(); QTRY covers both timings).
    QTRY_COMPARE(store->count(), 3); // 2 loaded + 1 user

    // Undo the user's draw. The two ORIGINAL file annotations must survive —
    // undoing back to the loaded baseline, not to an empty pre-load snapshot.
    QVERIFY(doc.canUndo());
    QVERIFY(doc.undo());
    QCOMPARE(store->count(), 2);
    QVERIFY2(store->find(userId) == nullptr, "the user's drawn annotation must be reverted");

    int rects = 0, ells = 0;
    for (const Annotation &a : store->annotations()) {
        if (a.type == AnnotationType::Rectangle)
            ++rects;
        else if (a.type == AnnotationType::Ellipse)
            ++ells;
    }
    QCOMPARE(rects, 1);
    QCOMPARE(ells, 1);
}

// Item A — an ImageDocument advertises search once the OCR-store bridge
// exists, so MainWindow's Find enablement (gated on supportsSearch())
// lights up for images.
void TestAdapters::imageDocumentSupportsSearch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto doc = openBlankImage(dir, QStringLiteral("supportssearch.png"));
    QVERIFY(doc != nullptr);
    QVERIFY2(doc->supportsSearch(),
             "ImageDocument must advertise search so Find is enabled for images");
}

// Item A — case-insensitive substring search over the OCR store's blocks
// yields a match count and cycles current-match via findNext/findPrevious.
void TestAdapters::imageDocumentSearchFindsOcrText() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto doc = openBlankImage(dir, QStringLiteral("findocr.png"));
    QVERIFY(doc != nullptr);
    auto *store = doc->selectableText();
    QVERIFY(store);
    seedOcrBlocks(store);

    // A query hitting a single block.
    doc->setSearchQuery(QStringLiteral("world"));
    QCOMPARE(doc->searchMatchCount(), 1);
    QCOMPARE(doc->currentSearchMatchIndex(), 1);
    QCOMPARE(doc->pagesWithSearchMatches(), (std::vector<int>{0}));

    // A query hitting both blocks — findNext/findPrevious cycle through them.
    doc->setSearchQuery(QStringLiteral("o"));
    QCOMPARE(doc->searchMatchCount(), 2);
    QCOMPARE(doc->currentSearchMatchIndex(), 1);
    doc->findNext();
    QCOMPARE(doc->currentSearchMatchIndex(), 2);
    doc->findNext(); // wraps
    QCOMPARE(doc->currentSearchMatchIndex(), 1);
    doc->findPrevious(); // wraps back
    QCOMPARE(doc->currentSearchMatchIndex(), 2);

    // Clearing the search drops all matches.
    doc->clearSearch();
    QCOMPARE(doc->searchMatchCount(), 0);
    QCOMPARE(doc->currentSearchMatchIndex(), -1);
    QVERIFY(doc->pagesWithSearchMatches().empty());
}

// Item A — search matching ignores case in both directions.
void TestAdapters::imageDocumentSearchIsCaseInsensitive() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto doc = openBlankImage(dir, QStringLiteral("caseins.png"));
    QVERIFY(doc != nullptr);
    auto *store = doc->selectableText();
    QVERIFY(store);
    seedOcrBlocks(store);

    doc->setSearchQuery(QStringLiteral("HELLO"));
    QCOMPARE(doc->searchMatchCount(), 1);
    doc->setSearchQuery(QStringLiteral("foObAr"));
    QCOMPARE(doc->searchMatchCount(), 1);
}

// Item A — with no OCR results a query yields nothing and the navigation
// calls are safe no-ops.
void TestAdapters::imageDocumentEmptyStoreSearchNoMatches() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto doc = openBlankImage(dir, QStringLiteral("emptystore.png"));
    QVERIFY(doc != nullptr);
    QVERIFY(doc->selectableText());
    QVERIFY2(!doc->selectableText()->hasResults(0), "store must start empty");

    doc->setSearchQuery(QStringLiteral("anything"));
    QCOMPARE(doc->searchMatchCount(), 0);
    QCOMPARE(doc->currentSearchMatchIndex(), -1);
    doc->findNext();     // must not crash
    doc->findPrevious(); // must not crash
    QVERIFY(doc->pagesWithSearchMatches().empty());
}

// Item B — the Recognize Text page resolution skips the dialog for a
// single-page document (there is nothing to choose) and defers to the
// dialog for multi-page documents.
void TestAdapters::recognizeTextSkipsDialogForSinglePage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Single-page image → resolves to {currentPage} without a dialog.
    auto img = openBlankImage(dir, QStringLiteral("singlepage.png"));
    QVERIFY(img != nullptr);
    QCOMPARE(img->pageCount(), 1);
    const auto resolved = resolveRecognizePages(*img);
    QVERIFY2(resolved.has_value(), "single-page doc must resolve without a dialog");
    QCOMPARE(*resolved, (std::vector<int>{img->currentPage()}));

    // Multi-page PDF → nullopt, i.e. defer to the dialog.
    const QString pdfPath = dir.filePath(QStringLiteral("multipage.pdf"));
    {
        QPdfWriter writer(pdfPath);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter p(&writer);
        p.drawText(QRect(100, 100, 400, 100), Qt::AlignLeft, "one");
        writer.newPage();
        p.drawText(QRect(100, 100, 400, 100), Qt::AlignLeft, "two");
        p.end();
    }
    PdfDocument pdf(pdfPath);
    QVERIFY(pdf.isValid());
    QCOMPARE(pdf.pageCount(), 2);
    QVERIFY2(!resolveRecognizePages(pdf).has_value(),
             "multi-page doc must defer to the dialog");
}

// Item C — the Recognize terminal message is honest: a zero-block run
// reports "No text found", never a false "complete". Cancelled stays the
// no-changes-saved message.
void TestAdapters::ocrBatchWithZeroBlocksReportsNoTextFound() {
    QCOMPARE(MainWindow::recognizeCompletionMessage(/*cancelled=*/false, /*blockCount=*/0),
             QStringLiteral("No text found"));
    QCOMPARE(MainWindow::recognizeCompletionMessage(/*cancelled=*/false, /*blockCount=*/3),
             QStringLiteral("Text recognition complete"));
    QCOMPARE(MainWindow::recognizeCompletionMessage(/*cancelled=*/true, /*blockCount=*/0),
             QStringLiteral("Text recognition cancelled — no changes saved"));
}

// Backlog 2026-07-12-page-changed-signal-no-poll (1): PdfDocument fires a real
// page-changed signal on every current-page change. The former 150 ms polls in
// MainWindow and the Sidebar are replaced by connecting to this notifier, so it
// must fire (with the new page index) when the page moves — here via goToPage(),
// which routes through the same navigator jump the keyboard/thumbnail paths use.
void TestAdapters::pdfDocumentPageChangeNotifierFiresOnPageChange() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("multipage.pdf");
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        for (int i = 0; i < 3; ++i) {
            if (i > 0)
                writer.newPage();
            painter.drawText(QRect(100, 100, 400, 100), Qt::AlignCenter,
                             QStringLiteral("Page %1").arg(i + 1));
        }
        painter.end();
    }

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    QCOMPARE(doc.pageCount(), 3);

    PageChangeNotifier *notifier = doc.pageChangeNotifier();
    QVERIFY2(notifier, "PdfDocument must expose a PageChangeNotifier");

    // A view is required: the notifier is driven by the QPdfView navigator.
    std::unique_ptr<QWidget> view(doc.createView(nullptr));
    QVERIFY(view);

    QSignalSpy spy(notifier, &PageChangeNotifier::currentPageChanged);
    QVERIFY(spy.isValid());
    QCOMPARE(doc.currentPage(), 0);

    doc.goToPage(2);
    // The navigator emits synchronously on jump(); no timer / event-loop spin.
    QVERIFY2(spy.count() >= 1, "page change must emit currentPageChanged");
    QCOMPARE(spy.last().at(0).toInt(), 2);
    QCOMPARE(doc.currentPage(), 2);

    // A jump back fires again with the new index.
    doc.goToPage(0);
    QVERIFY(spy.count() >= 2);
    QCOMPARE(spy.last().at(0).toInt(), 0);
    QCOMPARE(doc.currentPage(), 0);
}

// A single-frame image document has no notion of a moving current page, so it
// exposes no PageChangeNotifier (the base-class nullptr default). The Sidebar /
// MainWindow wiring is null-guarded on this, so such documents simply never
// receive page-change events.
void TestAdapters::imageDocumentHasNoPageChangeNotifier() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("solid.png");
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QVERIFY(img.save(path));

    ImageAdapter adapter;
    auto doc = adapter.open(path);
    QVERIFY(doc);
    QVERIFY(doc->pageChangeNotifier() == nullptr);
}

QTEST_MAIN(TestAdapters)
#include "test_adapters.moc"
