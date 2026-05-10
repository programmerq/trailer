#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "document/DocumentRegistry.h"
#include "document/ImageAdapter.h"
#include "document/PdfAdapter.h"

#include <QImage>
#include <QLabel>
#include <QObject>
#include <QPainter>
#include <QPdfWriter>
#include <QScrollArea>
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

QTEST_MAIN(TestAdapters)
#include "test_adapters.moc"
