// Regression guard — audit finding CF-4: the title-bar unsaved-changes
// "•" dirty marker must NOT clear when the user zooms a document that is
// dirty only via annotations.
//
// The marker is rebuilt by MainWindow::updateTitleForDocument, which reads
// IDocument::isDirty() (annotations OR page-commands OR pixel edits). A
// zoom must never leave the title reporting a stale, cleaner state than
// isDirty() — the close-guard already uses isDirty(), so a title that
// drops the dot after a zoom would under-report unsaved annotation work
// even though the data is still at risk.
//
// Reproduced at the MainWindow level so the real annotate -> title and
// zoom-action code paths run: open an image, annotate it (dirty via
// annotations only), assert the title shows "•", trigger the Zoom In
// action, and assert the "•" survives. The clean-doc guard proves a zoom
// on an unedited document does not spuriously add a marker.

#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "app/Application.h"
#include "document/IDocument.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QString>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

const QString kDot = QString::fromUtf8("\xE2\x80\xA2"); // "•"

MainWindow *newestWindow(Application *app) {
    // Mirror test_image_scale.cpp's newestImageDoc: iterate and keep the
    // LAST non-null window, so a lingering just-closed window (destroyed
    // via deleteLater on the next loop turn) can't latch a stale document.
    MainWindow *found = nullptr;
    for (MainWindow *w : app->windows())
        if (w)
            found = w;
    return found;
}

IDocument *currentDoc(MainWindow *mw) {
    for (int i = 0; i < mw->documentCount(); ++i) {
        IDocument *d = nullptr;
        if (mw->documentAt(i, &d) == 1 && d)
            return d;
    }
    return nullptr;
}

// The Zoom In action, located the way a user reaches it: by its menu text
// (ampersand mnemonic stripped). Faithful to the real command path — its
// triggered() lambda runs doc->zoomIn() + updateZoomIndicator().
QAction *zoomInAction(MainWindow *mw) {
    for (QAction *a : mw->findChildren<QAction *>()) {
        QString t = a->text();
        t.remove(QLatin1Char('&'));
        if (t.compare(QStringLiteral("Zoom In"), Qt::CaseInsensitive) == 0)
            return a;
    }
    return nullptr;
}

QString writeImage(const QString &path) {
    QImage img(320, 240, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    img.save(path, "PNG");
    return path;
}

QString writePdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(100, 100, QStringLiteral("CF-4 fixture"));
    p.end();
    return path;
}

} // namespace

class TestDirtyMarkerZoom : public QObject {
    Q_OBJECT
  private slots:
    void init();
    void annotationDirtyMarkerSurvivesZoom();
    void pdfAnnotationDirtyMarkerSurvivesZoom();
    void cleanDocumentZoomAddsNoMarker();
};

void TestDirtyMarkerZoom::init() {
    // Start each slot from an app with no open windows.
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

void TestDirtyMarkerZoom::annotationDirtyMarkerSurvivesZoom() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = writeImage(dir.filePath(QStringLiteral("cf4.png")));

    app->openFiles({png});
    QApplication::processEvents();

    MainWindow *mw = newestWindow(app);
    QVERIFY(mw);
    IDocument *doc = currentDoc(mw);
    QVERIFY(doc);

    // Baseline: freshly opened, not yet edited — no dirty dot.
    QVERIFY2(
        !mw->windowTitle().contains(kDot),
        qPrintable(
            QStringLiteral("unexpected dirty dot before any edit: '%1'").arg(mw->windowTitle())));

    // Make the document dirty via annotations ONLY (no rotate / pixel edit).
    AnnotationStore *store = doc->annotations();
    QVERIFY(store);
    Annotation a;
    a.type = AnnotationType::Rectangle;
    a.page = 0;
    a.bounds = QRectF(20, 20, 80, 40);
    store->add(a);

    QVERIFY2(doc->isDirty(), "adding an annotation must make the document dirty");
    // Settle the annotation-changed -> title-refresh (synchronous today, so
    // this passes at once; QTRY keeps it robust if a slower CI ever defers it).
    QTRY_VERIFY2(mw->windowTitle().contains(kDot),
                 qPrintable(QStringLiteral("annotate should show the dirty dot; title was '%1'")
                                .arg(mw->windowTitle())));

    // Zoom in via the real command path, and PROVE the zoom actually happened
    // — a disabled action or a factor already clamped at max would make "dot
    // survives zoom" hold vacuously.
    QAction *zin = zoomInAction(mw);
    QVERIFY2(zin, "Zoom In action not found");
    QVERIFY2(zin->isEnabled(), "Zoom In action is disabled — zoom would be a no-op");
    const double zBefore = doc->zoomFactor();
    zin->trigger();
    QApplication::processEvents();
    QVERIFY2(doc->zoomFactor() > zBefore,
             qPrintable(QStringLiteral("zoom-in did not raise the zoom factor (%1 -> %2) — "
                                       "trigger was a no-op")
                            .arg(zBefore)
                            .arg(doc->zoomFactor())));

    // The marker must survive — the document is still unsaved.
    QVERIFY2(doc->isDirty(), "zoom must not clear the document's dirty state");
    QVERIFY2(mw->windowTitle().contains(kDot),
             qPrintable(QStringLiteral("CF-4: dirty dot cleared after zoom; title was '%1'")
                            .arg(mw->windowTitle())));
}

void TestDirtyMarkerZoom::pdfAnnotationDirtyMarkerSurvivesZoom() {
    // PDF variant: unlike images (whose isDirty() reads the annotation
    // vector directly), a PdfDocument tracks annotation dirtiness in a
    // SEPARATE m_annotationsModified flag, OR-ed with the page-command
    // m_dirty inside isDirty(). This is exactly the "separate source" the
    // audit worried the zoom-path title refresh might read partially, so
    // the PDF path gets its own guard.
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pdf = writePdf(dir.filePath(QStringLiteral("cf4.pdf")));

    app->openFiles({pdf});
    QApplication::processEvents();

    MainWindow *mw = newestWindow(app);
    QVERIFY(mw);
    IDocument *doc = currentDoc(mw);
    QVERIFY(doc);

    AnnotationStore *store = doc->annotations();
    QVERIFY(store);
    // Let any deferred annotation load settle so the user edit below is a
    // genuine post-baseline change rather than part of the initial populate.
    QApplication::processEvents();

    // Baseline: a freshly opened PDF fixture must start clean, so the
    // "annotate shows the dot" assertion below can't pass for the wrong
    // reason (e.g. a fixture that somehow opens dirty).
    QVERIFY2(!doc->isDirty(), "freshly opened PDF fixture should be clean");
    QVERIFY2(
        !mw->windowTitle().contains(kDot),
        qPrintable(
            QStringLiteral("unexpected dirty dot before any edit: '%1'").arg(mw->windowTitle())));

    Annotation a;
    a.type = AnnotationType::Rectangle;
    a.page = 0;
    a.bounds = QRectF(20, 20, 80, 40);
    store->add(a);

    QVERIFY2(doc->isDirty(), "annotating a PDF must make it dirty");
    QTRY_VERIFY2(mw->windowTitle().contains(kDot),
                 qPrintable(QStringLiteral("PDF annotate should show the dirty dot; title '%1'")
                                .arg(mw->windowTitle())));

    // Prove the zoom actually happened (see the image slot's rationale).
    QAction *zin = zoomInAction(mw);
    QVERIFY2(zin, "Zoom In action not found");
    QVERIFY2(zin->isEnabled(), "Zoom In action is disabled — zoom would be a no-op");
    const double zBefore = doc->zoomFactor();
    zin->trigger();
    QApplication::processEvents();
    QVERIFY2(doc->zoomFactor() > zBefore,
             qPrintable(QStringLiteral("zoom-in did not raise the zoom factor (%1 -> %2) — "
                                       "trigger was a no-op")
                            .arg(zBefore)
                            .arg(doc->zoomFactor())));

    QVERIFY2(doc->isDirty(), "zoom must not clear the PDF's dirty state");
    QVERIFY2(mw->windowTitle().contains(kDot),
             qPrintable(QStringLiteral("CF-4 (PDF): dirty dot cleared after zoom; title '%1'")
                            .arg(mw->windowTitle())));
}

void TestDirtyMarkerZoom::cleanDocumentZoomAddsNoMarker() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = writeImage(dir.filePath(QStringLiteral("clean.png")));

    app->openFiles({png});
    QApplication::processEvents();

    MainWindow *mw = newestWindow(app);
    QVERIFY(mw);
    IDocument *doc = currentDoc(mw);
    QVERIFY(doc);
    QVERIFY2(!doc->isDirty(), "freshly opened image should be clean");

    // Prove the zoom actually happened, so "no marker after zoom" is not a
    // vacuous pass on a no-op trigger.
    QAction *zin = zoomInAction(mw);
    QVERIFY2(zin, "Zoom In action not found");
    QVERIFY2(zin->isEnabled(), "Zoom In action is disabled — zoom would be a no-op");
    const double zBefore = doc->zoomFactor();
    zin->trigger();
    QApplication::processEvents();
    QVERIFY2(doc->zoomFactor() > zBefore,
             qPrintable(QStringLiteral("zoom-in did not raise the zoom factor (%1 -> %2) — "
                                       "trigger was a no-op")
                            .arg(zBefore)
                            .arg(doc->zoomFactor())));

    QVERIFY2(!mw->windowTitle().contains(kDot),
             qPrintable(QStringLiteral("clean doc must not gain a dirty dot on zoom; title '%1'")
                            .arg(mw->windowTitle())));
}

// Custom main mirrors tests/test_image_scale.cpp: construct the real
// Application (a QApplication subclass) so openFiles() drives the true
// window/document wiring, and sandbox HOME so Settings / RecentFiles never
// touch the real config dir.
int main(int argc, char **argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    trailer::Application app(argc, argv);
    TestDirtyMarkerZoom tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_dirty_marker_zoom.moc"
