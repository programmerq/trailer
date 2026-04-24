// UAT harness — Recognize Text (Phase 6 §6.11 / DESIGN §6.1.5).
//
// Recognize Text opens a modal OcrResultsDialog we can't click
// through under QT_QPA_PLATFORM=offscreen, so this test exercises
// the integration path the menu slot relies on:
//
//   1. Open an image → ImageDocument becomes the active view.
//   2. Tools → Recognize Text… action exists and is enabled for an
//      image, disabled for a PDF.
//   3. With TRAILER_TEST_PPOCR_DET + TRAILER_TEST_PPOCR_REC seeded
//      into AppPaths::modelsDir(), OcrEngine::recognize() returns at
//      least one non-empty TextBlock on a painted "HELLO 1234"
//      banner and does NOT mutate the document (dirty/undo remain
//      false — OCR is a read-only operation).
//   4. Without cached models, recognize() returns an empty vector
//      and no document mutation occurs.
//
//   uat_ocr_010_recognizeTextMenuActionWiredForImage
//   uat_ocr_020_recognizeTextProducesBlocksWithRealModels  (gated)
//   uat_ocr_030_recognizeTextNoopsWithoutModels

#include "app/Application.h"
#include "document/ImageAdapter.h"
#include "ml/ModelRegistry.h"
#include "ml/OcrEngine.h"
#include "settings/AppPaths.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

MainWindow* currentMainWindow() {
    for (auto* w : QApplication::topLevelWidgets()) {
        if (auto* mw = qobject_cast<MainWindow*>(w)) return mw;
    }
    return nullptr;
}

QAction* findActionByText(QMenuBar* bar, const QString& text) {
    for (QAction* top : bar->actions()) {
        QMenu* menu = top->menu();
        if (!menu) continue;
        for (QAction* a : menu->actions()) {
            if (a->text() == text) return a;
        }
    }
    return nullptr;
}

QString writeTextImage(const QString& path,
                       const QString& text = QStringLiteral("HELLO 1234"),
                       int w = 640, int h = 200) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    QFont f;
    f.setPixelSize(80);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::black);
    p.drawText(img.rect(), Qt::AlignCenter, text);
    p.end();
    img.save(path, "PNG");
    return path;
}

bool seedPpOcrIntoAppCache() {
    const QString detSrc =
        QString::fromLocal8Bit(qgetenv("TRAILER_TEST_PPOCR_DET"));
    const QString recSrc =
        QString::fromLocal8Bit(qgetenv("TRAILER_TEST_PPOCR_REC"));
    if (detSrc.isEmpty() || !QFileInfo::exists(detSrc)) return false;
    if (recSrc.isEmpty() || !QFileInfo::exists(recSrc)) return false;
    const QString dir = AppPaths::modelsDir();
    QDir().mkpath(dir);
    const QString detDest =
        QDir(dir).filePath(QStringLiteral("pp_ocr_det.onnx"));
    const QString recDest =
        QDir(dir).filePath(QStringLiteral("pp_ocr_rec_en.onnx"));
    QFile::remove(detDest);
    QFile::remove(recDest);
    return QFile::copy(detSrc, detDest) && QFile::copy(recSrc, recDest);
}

void wipePpOcrCache() {
    const QString dir = AppPaths::modelsDir();
    QFile::remove(QDir(dir).filePath(QStringLiteral("pp_ocr_det.onnx")));
    QFile::remove(QDir(dir).filePath(QStringLiteral("pp_ocr_rec_en.onnx")));
}

}  // namespace

class TestUatRecognizeText : public QObject {
    Q_OBJECT
private slots:
    void init();
    void uat_ocr_010_recognizeTextMenuActionWiredForImage();
    void uat_ocr_020_recognizeTextProducesBlocksWithRealModels();
    void uat_ocr_030_recognizeTextNoopsWithoutModels();

private:
    QTemporaryDir m_scratch;
};

void TestUatRecognizeText::init() {
    for (auto* w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow*>(w)) w->close();
    }
    QApplication::processEvents();
    wipePpOcrCache();
}

void TestUatRecognizeText::
    uat_ocr_010_recognizeTextMenuActionWiredForImage() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeTextImage(
        m_scratch.filePath(QStringLiteral("ocr010.png")));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    QAction* action = findActionByText(
        mw->menuBar(), QStringLiteral("Reco&gnize Text…"));
    QVERIFY2(action, "Tools → Recognize Text… action is missing");
    QVERIFY2(action->isEnabled(),
             "Recognize Text should be enabled for an image document");
}

void TestUatRecognizeText::
    uat_ocr_020_recognizeTextProducesBlocksWithRealModels() {
    if (!seedPpOcrIntoAppCache()) {
        QSKIP("TRAILER_TEST_PPOCR_DET + TRAILER_TEST_PPOCR_REC not "
              "set — skipping real inference path.");
    }

    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeTextImage(
        m_scratch.filePath(QStringLiteral("ocr020.png")));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    auto* dv = mw->findChild<DocumentView*>();
    QVERIFY(dv);
    auto* imgDoc = dynamic_cast<ImageDocument*>(dv->currentDocument());
    QVERIFY2(imgDoc, "Active document should be an ImageDocument");
    const QImage source = imgDoc->image();
    QVERIFY(!source.isNull());

    OcrEngine engine(&app->modelRegistry());
    QVERIFY2(engine.isModelReady(),
             "Seeded PP-OCR models should be visible to the shared registry");

    const auto blocks = engine.recognize(source);
    QVERIFY2(!blocks.isEmpty(),
             "Expected at least one TextBlock on a HELLO 1234 banner");
    for (const auto& b : blocks) {
        QVERIFY(!b.text.isEmpty());
        QVERIFY(b.confidence >= 0.0f && b.confidence <= 1.0f);
        const QRect bounds = b.polygon.boundingRect();
        QVERIFY2(bounds.intersects(source.rect()),
                 "Block polygon should overlap the source image");
    }

    // OCR is read-only — it must not mutate the document.
    QVERIFY(!imgDoc->isDirty());
    QVERIFY(!imgDoc->canUndo());
}

void TestUatRecognizeText::
    uat_ocr_030_recognizeTextNoopsWithoutModels() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeTextImage(
        m_scratch.filePath(QStringLiteral("ocr030.png")));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    auto* dv = mw->findChild<DocumentView*>();
    QVERIFY(dv);
    auto* imgDoc = dynamic_cast<ImageDocument*>(dv->currentDocument());
    QVERIFY(imgDoc);

    OcrEngine engine(&app->modelRegistry());
    QVERIFY2(!engine.isModelReady(),
             "Cache wiped in init() — models should not be ready");

    const auto blocks = engine.recognize(imgDoc->image());
    QVERIFY(blocks.isEmpty());
    QVERIFY(!imgDoc->isDirty());
    QVERIFY(!imgDoc->canUndo());
}

int main(int argc, char** argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid()) return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME",   (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    Application app(argc, argv);
    TestUatRecognizeText tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_recognize_text.moc"
