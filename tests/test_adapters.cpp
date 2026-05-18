#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "document/DocumentRegistry.h"
#include "document/ImageAdapter.h"
#include "document/PdfAdapter.h"

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
    void printSupportReflectsValidity();
    void pdfDocumentRotationMarksDirtyAndSaveClears();
    void pdfDocumentDeletePagesRemovesAndMarksDirty();
    void pdfDocumentMovePageReorders();
    void imageDocumentRotateSwapsDimensionsAndMarksDirty();
    void imageDocumentFlipHorizontalMarksDirty();
    void imageDocumentResizeChangesPixelSize();
    void imageDocumentCropReducesSize();
    void imageDocumentAdjustColourModifiesPixels();
    void imageDocumentSaveClearsDirty();
    void imageDocumentExportAsJpegWritesFile();
    void imageDocumentUndoRestoresPriorState();
    void imageDocumentSaveFlattensAnnotationsIntoPixels();
    void imageDocumentAnnotationUndoTakesPrecedenceOverImageUndo();
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

void TestAdapters::imageDocumentAnnotationUndoTakesPrecedenceOverImageUndo() {
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

void TestAdapters::imageDocumentFitModeStartsCustom() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTinyPng(dir.filePath("fm.png"), 32, 32);

    ImageDocument doc(path);
    QCOMPARE(doc.fitMode(), ImageDocument::FitMode::Custom);
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
    QCOMPARE(doc.fitMode(), ImageDocument::FitMode::FitInView);
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
    QCOMPARE(doc.fitMode(), ImageDocument::FitMode::FitToWidth);
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
    QCOMPARE(doc.fitMode(), ImageDocument::FitMode::FitInView);

    doc.zoomIn();
    QCOMPARE(doc.fitMode(), ImageDocument::FitMode::Custom);

    doc.zoomFitWidth();
    QCOMPARE(doc.fitMode(), ImageDocument::FitMode::FitToWidth);

    doc.zoomOut();
    QCOMPARE(doc.fitMode(), ImageDocument::FitMode::Custom);

    doc.zoomFitPage();
    doc.zoomActual();
    QCOMPARE(doc.fitMode(), ImageDocument::FitMode::Custom);
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
    QCOMPARE(doc.fitMode(), ImageDocument::FitMode::Custom);
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

QTEST_MAIN(TestAdapters)
#include "test_adapters.moc"
