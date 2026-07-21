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
#include "document/ImageAdapter.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPageSize>
#include <QPixmap>
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

// Any action located by its (mnemonic-stripped) menu text.
QAction *findActionByText(MainWindow *mw, const QString &text) {
    for (QAction *a : mw->findChildren<QAction *>()) {
        QString t = a->text();
        t.remove(QLatin1Char('&'));
        if (t.compare(text, Qt::CaseInsensitive) == 0)
            return a;
    }
    return nullptr;
}

// A PPM whose ASCII header fully specifies the size (QImageReader::size()
// succeeds, so the staged open kicks a decode and provisionally enables the
// controls) but whose binary pixel data is truncated to far fewer than
// 400*300*3 bytes, so the async full decode fails.
QString writeCorruptImage(const QString &path) {
    QByteArray ppm = QByteArrayLiteral("P6\n400 300\n255\n");
    ppm.append(100, '\0');
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(ppm);
    f.close();
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
    void corruptImageDisablesEditAndZoomActions();
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

// G3 regression: a staged image open (ADR 0008) whose async decode FAILS
// (valid header, corrupt/truncated body) must not leave the edit/zoom actions
// enabled-but-inert. They are enabled at open while the decode is pending;
// once it fails, the capability notifier fires and MainWindow must DISABLE
// them (with a why tooltip) rather than let the user click a control that
// no-ops against a null image.
void TestDirtyMarkerZoom::corruptImageDisablesEditAndZoomActions() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = writeCorruptImage(dir.filePath(QStringLiteral("corrupt.ppm")));

    app->openFiles({png});
    QApplication::processEvents();

    MainWindow *mw = newestWindow(app);
    QVERIFY(mw);
    IDocument *doc = currentDoc(mw);
    QVERIFY(doc);
    auto *img = dynamic_cast<ImageDocument *>(doc);
    QVERIFY2(img, "a corrupt image file must still open as an ImageDocument");

    // Wait for the off-thread decode to finish (it fails to a null image), then
    // let the capabilitiesChanged refresh run.
    QVERIFY(img->awaitDecodeForTest());
    QApplication::processEvents();

    // Capabilities are now honest — a null decoded image is not zoomable/editable.
    QVERIFY2(!doc->supportsZoom(), "a failed-decode image must not report zoom support");
    QVERIFY2(!doc->supportsEditing(), "a failed-decode image must not report edit support");
    QCOMPARE(doc->pageCount(), 0);

    // And the controls MainWindow enabled at open (while pending) are now
    // disabled with an explanatory tooltip — not enabled-but-inert (G3).
    QAction *zin = zoomInAction(mw);
    QVERIFY2(zin, "Zoom In action not found");
    QVERIFY2(!zin->isEnabled(),
             "Zoom In must be DISABLED for an undecodable image, not enabled-but-inert");
    QVERIFY2(!zin->toolTip().isEmpty(),
             "disabled Zoom In should carry a 'why' tooltip (G3)");

    QAction *rot = findActionByText(mw, QStringLiteral("Rotate Left"));
    QVERIFY2(rot, "Rotate Left action not found");
    QVERIFY2(!rot->isEnabled(), "Rotate Left must be disabled for an undecodable image");

    // G3 evidence: when TRAILER_STAGED_OPEN_EVIDENCE_DIR is set, grab the
    // window showing the decode-failed state — the main toolbar's zoom/rotate
    // buttons rendered DISABLED (greyed) above the "Could not decode image:"
    // view. Offscreen static grabs can't render a hover tooltip, so the
    // tooltip text ("This image could not be opened.", asserted above) is
    // noted in the PR caption instead; the disabled control state itself is in
    // frame. A no-op in normal CI runs (env var unset).
    const QString evDir = QString::fromLocal8Bit(qgetenv("TRAILER_STAGED_OPEN_EVIDENCE_DIR"));
    if (!evDir.isEmpty()) {
        QDir().mkpath(evDir);
        mw->resize(900, 600);
        QApplication::processEvents();
        const QPixmap pm = mw->grab();
        QVERIFY2(!pm.isNull(), "grab returned null for the decode-failed evidence shot");
        QVERIFY2(pm.save(QDir(evDir).filePath(
                     QStringLiteral("staged-open-03-decode-failed-disabled.png"))),
                 "failed to write the decode-failed evidence shot");
    }
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
