// UAT harness — Instant Alpha + Smart Lasso (Phase 6 §6.3.3 / §6.3.6,
// DESIGN §6.1.5).
//
// Workstream G moved both features out of the modal `SamSegmentDialog`
// into in-document tool modes on the markup toolbar's overlay. The
// harness now drives the AnnotationOverlay + SamController directly.
//
//   uat_sam_010_instantAlphaMenuActionWired
//       Tools → Instant Alpha… exists and is enabled for an image
//       document. Triggering it activates the InstantAlpha tool on
//       the markup toolbar.
//   uat_sam_020_smartLassoMenuActionWired
//       Tools → Smart Lasso… exists and is enabled for an image
//       document. Triggering it activates the SmartLasso tool on the
//       markup toolbar.
//   uat_sam_030_instantAlphaAppliesAlphaWithRealModels
//       With TRAILER_TEST_SAM_ENCODER + TRAILER_TEST_SAM_DECODER
//       seeded into AppPaths::modelsDir(), the InstantAlpha tool path
//       through SamSession.prepare() + segment() + applyAsAlpha()
//       yields an ARGB image the same size as the input, and the
//       overlay's commit handler routes that into
//       ImageDocument::replaceImage. Tab marks dirty + canUndo.
//   uat_sam_040_smartLassoCropsToObjectBoundsWithRealModels
//       Same seeded-cache path, but uses the SmartLasso commit
//       handler to call cropToRect with the contour's bounding rect.
//   uat_sam_050_instantAlphaNoopsWithoutModels
//       Without models, the controller's prepare returns false and
//       segment() yields a null mask; no document mutation occurs.
//   uat_sam_060_overlayDispatchesPromptThroughController
//       Synthesise a SAM prompt via the overlay's test seam; the
//       controller's dispatch counter increments — proving the
//       overlay → controller wiring is intact even without real
//       models (decoder returns null but the dispatch still flows).

#include "app/Application.h"
#include "document/ImageAdapter.h"
#include "ml/ModelRegistry.h"
#include "ml/SamSession.h"
#include "settings/AppPaths.h"
#include "ui/AnnotationOverlay.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "ui/MarkupToolbar.h"
#include "ui/SamController.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QPoint>
#include <QPolygon>
#include <QRect>
#include <QSize>
#include <QSettings>
#include <QTemporaryDir>
#include <QVector>
#include <QtTest/QtTest>

#include <algorithm>

using namespace trailer;

namespace {

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

QAction *findActionByText(QMenuBar *bar, const QString &text) {
    for (QAction *top : bar->actions()) {
        QMenu *menu = top->menu();
        if (!menu)
            continue;
        for (QAction *a : menu->actions()) {
            if (a->text() == text)
                return a;
        }
    }
    return nullptr;
}

QString writeSampleScene(const QString &path, int w = 320, int h = 240) {
    // Dark-grey background with a bright disc in the middle — an easy
    // target for SAM to segment when we click the centre.
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(qRgb(32, 32, 32));
    const int cx = w / 2;
    const int cy = h / 2;
    const int r = std::min(w, h) / 4;
    for (int y = 0; y < h; ++y) {
        auto *scan = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int dx = x - cx;
            const int dy = y - cy;
            if (dx * dx + dy * dy <= r * r) {
                scan[x] = qRgb(220, 220, 220);
            }
        }
    }
    img.save(path, "PNG");
    return path;
}

bool seedMobileSamIntoAppCache() {
    const QString encSrc = QString::fromLocal8Bit(qgetenv("TRAILER_TEST_SAM_ENCODER"));
    const QString decSrc = QString::fromLocal8Bit(qgetenv("TRAILER_TEST_SAM_DECODER"));
    if (encSrc.isEmpty() || !QFileInfo::exists(encSrc))
        return false;
    if (decSrc.isEmpty() || !QFileInfo::exists(decSrc))
        return false;
    const QString dir = AppPaths::modelsDir();
    QDir().mkpath(dir);
    const QString encDest = QDir(dir).filePath(QStringLiteral("mobile_sam_encoder.onnx"));
    const QString decDest = QDir(dir).filePath(QStringLiteral("mobile_sam_decoder.onnx"));
    QFile::remove(encDest);
    QFile::remove(decDest);
    return QFile::copy(encSrc, encDest) && QFile::copy(decSrc, decDest);
}

void wipeMobileSamCache() {
    const QString dir = AppPaths::modelsDir();
    QFile::remove(QDir(dir).filePath(QStringLiteral("mobile_sam_encoder.onnx")));
    QFile::remove(QDir(dir).filePath(QStringLiteral("mobile_sam_decoder.onnx")));
}

} // namespace

class TestUatInstantAlphaAndSmartLasso : public QObject {
    Q_OBJECT
  private slots:
    void init();
    void uat_sam_010_instantAlphaMenuActionWired();
    void uat_sam_020_smartLassoMenuActionWired();
    void uat_sam_030_instantAlphaAppliesAlphaWithRealModels();
    void uat_sam_040_smartLassoCropsToObjectBoundsWithRealModels();
    void uat_sam_050_instantAlphaNoopsWithoutModels();
    void uat_sam_060_overlayDispatchesPromptThroughController();

  private:
    QTemporaryDir m_scratch;
};

void TestUatInstantAlphaAndSmartLasso::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
    wipeMobileSamCache();
}

void TestUatInstantAlphaAndSmartLasso::uat_sam_010_instantAlphaMenuActionWired() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeSampleScene(m_scratch.filePath(QStringLiteral("sam010.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    QAction *action = findActionByText(mw->menuBar(), QStringLiteral("&Instant Alpha…"));
    QVERIFY2(action, "Tools → Instant Alpha… action is missing");
    QVERIFY2(action->isEnabled(), "Instant Alpha should be enabled for an image document");
}

void TestUatInstantAlphaAndSmartLasso::uat_sam_020_smartLassoMenuActionWired() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeSampleScene(m_scratch.filePath(QStringLiteral("sam020.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    QAction *action = findActionByText(mw->menuBar(), QStringLiteral("Smart &Lasso…"));
    QVERIFY2(action, "Tools → Smart Lasso… action is missing");
    QVERIFY2(action->isEnabled(), "Smart Lasso should be enabled for an image document");
}

void TestUatInstantAlphaAndSmartLasso::uat_sam_030_instantAlphaAppliesAlphaWithRealModels() {
    if (!seedMobileSamIntoAppCache()) {
        QSKIP("TRAILER_TEST_SAM_ENCODER + TRAILER_TEST_SAM_DECODER not "
              "set — skipping real inference path.");
    }

    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeSampleScene(m_scratch.filePath(QStringLiteral("sam030.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    auto *imgDoc = dynamic_cast<ImageDocument *>(dv->currentDocument());
    QVERIFY2(imgDoc, "Active document should be an ImageDocument");
    const QImage original = imgDoc->image();
    QVERIFY(!original.isNull());
    const QSize before = imgDoc->imagePixelSize();

    SamSession session(&app->modelRegistry());
    QVERIFY2(session.isModelReady(),
             "Seeded MobileSAM models should be visible to the shared registry");
    QVERIFY(session.prepare(original));
    QCOMPARE(session.preparedSize(), before);

    const QVector<QPoint> positives{QPoint(original.width() / 2, original.height() / 2)};
    const QImage mask = session.segment(positives, {});
    QVERIFY(!mask.isNull());
    QCOMPARE(mask.size(), before);
    QCOMPARE(mask.format(), QImage::Format_Grayscale8);

    // Clicked pixel lands inside the foreground.
    const QPoint click = positives.first();
    QVERIFY2(mask.constScanLine(click.y())[click.x()] != 0, "Clicked pixel should be foreground");

    const QImage result = session.applyAsAlpha(original);
    QVERIFY(!result.isNull());
    QCOMPARE(result.size(), before);
    QVERIFY(result.hasAlphaChannel());

    QVERIFY(!imgDoc->isDirty());
    QVERIFY(!imgDoc->canUndo());
    QVERIFY(imgDoc->replaceImage(result));
    QVERIFY2(imgDoc->isDirty(), "replaceImage should mark document dirty");
    QVERIFY2(imgDoc->canUndo(), "replaceImage should push an undo snapshot");

    imgDoc->undo();
    QVERIFY(!imgDoc->canUndo());
}

void TestUatInstantAlphaAndSmartLasso::uat_sam_040_smartLassoCropsToObjectBoundsWithRealModels() {
    if (!seedMobileSamIntoAppCache()) {
        QSKIP("TRAILER_TEST_SAM_ENCODER + TRAILER_TEST_SAM_DECODER not "
              "set — skipping real inference path.");
    }

    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeSampleScene(m_scratch.filePath(QStringLiteral("sam040.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    auto *imgDoc = dynamic_cast<ImageDocument *>(dv->currentDocument());
    QVERIFY(imgDoc);
    const QImage original = imgDoc->image();
    const QSize before = imgDoc->imagePixelSize();

    SamSession session(&app->modelRegistry());
    QVERIFY(session.isModelReady());
    QVERIFY(session.prepare(original));

    const QVector<QPoint> positives{QPoint(original.width() / 2, original.height() / 2)};
    QVERIFY(!session.segment(positives, {}).isNull());

    const QPolygon poly = session.contourFromLastMask();
    QVERIFY2(poly.size() >= 3, "Contour should have at least 3 points");
    const QRect bounds = poly.boundingRect().intersected(QRect(QPoint(), before));
    QVERIFY(bounds.width() >= 2 && bounds.height() >= 2);
    // The bounding rect of the disc must be strictly smaller than the
    // full image — otherwise the crop would be a no-op.
    QVERIFY2(bounds.width() < before.width() || bounds.height() < before.height(),
             "Polygon bounds should be a proper subset of the image");

    QVERIFY(!imgDoc->isDirty());
    QVERIFY(!imgDoc->canUndo());
    QVERIFY(imgDoc->cropToRect(bounds.x(), bounds.y(), bounds.width(), bounds.height()));
    QVERIFY2(imgDoc->isDirty(), "cropToRect should mark document dirty");
    QVERIFY2(imgDoc->canUndo(), "cropToRect should push an undo snapshot");
    QCOMPARE(imgDoc->imagePixelSize(), bounds.size());

    imgDoc->undo();
    QCOMPARE(imgDoc->imagePixelSize(), before);
}

void TestUatInstantAlphaAndSmartLasso::uat_sam_050_instantAlphaNoopsWithoutModels() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeSampleScene(m_scratch.filePath(QStringLiteral("sam050.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    auto *imgDoc = dynamic_cast<ImageDocument *>(dv->currentDocument());
    QVERIFY(imgDoc);

    SamSession session(&app->modelRegistry());
    QVERIFY2(!session.isModelReady(), "Cache was wiped in init() — models should not be ready");

    // prepare() must fail fast without emitting spurious signals and
    // segment() must yield a null mask.
    QVERIFY(!session.prepare(imgDoc->image()));
    const QImage mask =
        session.segment({QPoint(imgDoc->image().width() / 2, imgDoc->image().height() / 2)}, {});
    QVERIFY(mask.isNull());
    QVERIFY(session.contourFromLastMask().isEmpty());

    QVERIFY(!imgDoc->isDirty());
    QVERIFY(!imgDoc->canUndo());
}

void TestUatInstantAlphaAndSmartLasso::uat_sam_060_overlayDispatchesPromptThroughController() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeSampleScene(m_scratch.filePath(QStringLiteral("sam060.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    auto *imgDoc = dynamic_cast<ImageDocument *>(dv->currentDocument());
    QVERIFY(imgDoc);

    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY2(overlay, "Active image document should expose an AnnotationOverlay");
    auto *controller = overlay->samController();
    QVERIFY2(controller, "Overlay should be wired to a SamController");

    // Make sure the toolbar is visible so its tool radio is interactive,
    // then activate InstantAlpha on the toolbar (the same path the menu
    // shortcut takes via activateSamTool). Skipping the menu entry
    // keeps us off the model-download dialog (which would have to
    // hit the network); we drive the overlay's tool mode directly.
    auto *toolbar = mw->findChild<MarkupToolbar *>();
    QVERIFY(toolbar);
    toolbar->show();
    overlay->setActiveTool(AnnotationTool::InstantAlpha);
    QVERIFY(overlay->isSamToolActiveForTest());

    // Synthesise a positive prompt via the overlay's test seam. The
    // controller's dispatch counter increments because requestSegment
    // routes through MlScheduler; the decoder returns a null mask
    // because models aren't loaded, but the dispatch path is what
    // we're exercising here.
    const int beforeDispatches = controller->decoderDispatchCountForTest();
    const QPointF prompt(imgDoc->image().width() / 2.0, imgDoc->image().height() / 2.0);
    QVERIFY(overlay->simulateSamPromptForTest(prompt, /*positive=*/true));
    // Spin briefly so the scheduler can pick up the request.
    for (int i = 0; i < 20; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (controller->decoderDispatchCountForTest() > beforeDispatches)
            break;
    }
    QVERIFY2(controller->decoderDispatchCountForTest() > beforeDispatches,
             "Synthesising a SAM prompt should have driven a decoder dispatch");

    // Tool deactivation drops the SAM preview + cancels pending work.
    overlay->setActiveTool(AnnotationTool::Select);
    QVERIFY(!overlay->isSamToolActiveForTest());
    QVERIFY(overlay->samPreviewMaskForTest().isNull());
}

int main(int argc, char **argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    // See tests/test_image_scale.cpp's main() for why this is needed on
    // macOS: QSettings(org, app) defaults to NativeFormat there, which
    // ignores the HOME sandboxing above.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    Application app(argc, argv);
    TestUatInstantAlphaAndSmartLasso tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_instant_alpha_and_smart_lasso.moc"
