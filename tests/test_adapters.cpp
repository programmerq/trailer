#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "document/DocumentRegistry.h"
#include "document/ImageAdapter.h"
#include "document/PdfAdapter.h"
#include "document/SelectableTextStore.h"
#include "ui/SelectableTextLayer.h"

#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QObject>
#include <QPainter>
#include <QPdfView>
#include <QPdfWriter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

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
    void pdfDocumentAdvertisesCapabilities();
    void pdfDocumentRendersThumbnailsForValidFile();
    void pdfDocumentAcceptsSearchQueryWithoutView();
    void pdfDocumentPageHasTextDistinguishesTextFromBlankPage();
    void pdfDocumentNativeTextLayerFeedsSelectableStore();
    void pdfDocumentNativeTextDragSelectsRealString();
    void printSupportReflectsValidity();
    void pdfDocumentRotationMarksDirtyAndSaveClears();
    void pdfDocumentDeletePagesRemovesAndMarksDirty();
    void pdfDocumentMovePageReorders();
    void pdfDocumentInterleavedUndoIsChronological();
    void pdfDocumentUndoAllPastOldCapRegimeIsExact();
    void pdfDocumentSmallCapEvictionKeepsLogAndStoreInLockstep();
    void pdfDocumentUndoRedoSurviveForcedLogDesync();
    void pdfDocumentSaveReloadRebuildsUndoLogFromRetainedStacks();
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
    void imageDocumentReapplyFitModeRefitsOnResize();
    void imageDocumentResizeDoesNothingInCustomMode();
    void pdfViewReflowsOnResizeInFitInView();
    void pdfDownArrowStepsPageImmediatelyInFitMode();
    void imageDocumentResizeEventTriggersRefit();
};

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

void TestAdapters::imageDocumentReapplyFitModeRefitsOnResize() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("rf.png"), 50, 50);

    ImageDocument doc(path);
    std::unique_ptr<QWidget> view(doc.createView(nullptr));
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
    auto *pdfView = qobject_cast<QPdfView *>(view.get());
    QVERIFY2(pdfView != nullptr, "expected createView to return a QPdfView");

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
    pdfView->show();
    pdfView->resize(800, 600);
    pdfView->setZoomMode(QPdfView::ZoomMode::FitInView);
    QCoreApplication::processEvents();
    QTest::qWait(50);
    const int largeRange =
        pdfView->verticalScrollBar()->maximum() - pdfView->verticalScrollBar()->minimum();

    pdfView->resize(200, 150);
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
    auto *pdfView = qobject_cast<QPdfView *>(view.get());
    QVERIFY(pdfView != nullptr);
    pdfView->show();
    pdfView->resize(800, 600);
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

QTEST_MAIN(TestAdapters)
#include "test_adapters.moc"
