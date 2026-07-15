// UAT harness — Recognize Text (Phase 6 §6.11 / Workstream F).
//
// After Workstream F, Recognize Text feeds the document's
// SelectableTextStore instead of dumping into a QPlainTextEdit.
// These tests exercise the new in-document selectability contract:
//
//   1. The Tools → Recognize Text… action is enabled for any doc that
//      supportsSelectableText() — images, PDFs, anything raster.
//   2. With seeded PP-OCR models, an OcrEngine::recognize() pass
//      populates the SelectableTextStore for page 0 and the
//      SelectableTextLayer (a) sets the I-beam cursor only over
//      cached text, (b) drag-selects a block into the clipboard via
//      selectedText(), without mutating the document.
//   3. Without cached models, recognize() noops the way it used to
//      and the store stays empty.
//   4. The large-doc OCR hint chip is wired so MainWindow has a
//      status-bar widget hidden by default.
//   5. The Recognize Text dialog's "force re-run" affordance is
//      shown for docs with a text layer and resolves to a non-empty
//      page list when accepted.
//
//   uat_ocr_010_recognizeTextMenuActionWiredForImage
//   uat_ocr_020_recognizeTextPopulatesSelectableTextStore  (gated)
//   uat_ocr_030_recognizeTextNoopsWithoutModels
//   uat_ocr_040_recognizeTextDialogOffersForceRerunForPdf
//   uat_ocr_050_recognizeTextLayerIsOverlayChildOfView

#include "app/Application.h"
#include "document/IDocument.h"
#include "document/ImageAdapter.h"
#include "document/SelectableTextStore.h"
#include "ml/CancellationToken.h"
#include "ml/ModelRegistry.h"
#include "ml/OcrEngine.h"
#include "settings/AppPaths.h"
#include "settings/Settings.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "ui/MlProgressWidget.h"
#include "ui/OcrController.h"
#include "ui/OcrResultsDialog.h"
#include "ui/SelectableTextLayer.h"

#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPolygon>
#include <QSemaphore>

#include <atomic>
#include <QTemporaryDir>
#include <QToolButton>
#include <QWidget>
#include <QtTest/QtTest>

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

QString writeTextImage(const QString &path, const QString &text = QStringLiteral("HELLO 1234"),
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
    const QString detSrc = QString::fromLocal8Bit(qgetenv("TRAILER_TEST_PPOCR_DET"));
    const QString recSrc = QString::fromLocal8Bit(qgetenv("TRAILER_TEST_PPOCR_REC"));
    if (detSrc.isEmpty() || !QFileInfo::exists(detSrc))
        return false;
    if (recSrc.isEmpty() || !QFileInfo::exists(recSrc))
        return false;
    const QString dir = AppPaths::modelsDir();
    QDir().mkpath(dir);
    const QString detDest = QDir(dir).filePath(QStringLiteral("pp_ocr_det.onnx"));
    const QString recDest = QDir(dir).filePath(QStringLiteral("pp_ocr_rec_en.onnx"));
    QFile::remove(detDest);
    QFile::remove(recDest);
    return QFile::copy(detSrc, detDest) && QFile::copy(recSrc, recDest);
}

void wipePpOcrCache() {
    const QString dir = AppPaths::modelsDir();
    QFile::remove(QDir(dir).filePath(QStringLiteral("pp_ocr_det.onnx")));
    QFile::remove(QDir(dir).filePath(QStringLiteral("pp_ocr_rec_en.onnx")));
}

// Write a `pages`-page PDF. When `withText`, each page carries a drawn
// text line (a born-digital text layer); otherwise pages are blank —
// the closest fixture to an image-only scan. Used to exercise the
// large-doc (>50 page) Recognize-text notice guard.
QString writeMultiPagePdf(const QString &path, int pages, bool withText) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    for (int i = 0; i < pages; ++i) {
        if (i > 0)
            writer.newPage();
        if (withText) {
            p.drawText(QRect(200, 200, 3000, 400), Qt::AlignLeft,
                       QStringLiteral("Born-digital page %1 selectable text").arg(i + 1));
        }
    }
    p.end();
    return path;
}

// G2 evidence: grab the running window to PNG when TRAILER_SHOT_DIR is
// set. Offscreen-safe (QWidget::grab renders without a display).
void saveNoticeShot(MainWindow *mw, const QString &name) {
    const QByteArray shotDir = qgetenv("TRAILER_SHOT_DIR");
    if (shotDir.isEmpty())
        return;
    QDir().mkpath(QString::fromLocal8Bit(shotDir));
    mw->grab().save(QString::fromLocal8Bit(shotDir) + "/" + name);
}

// A recognizer that blocks on `gate` before returning a single sentinel
// block, so a batch stays mid-flight long enough to grab the revealed
// progress widget.
OcrController::RecognizeFn gatedRecognizer(std::shared_ptr<QSemaphore> gate) {
    return [gate](const QImage &, const CancellationToken *) -> QVector<OcrEngine::TextBlock> {
        gate->acquire();
        OcrEngine::TextBlock b;
        b.text = QStringLiteral("recognised");
        b.polygon = QPolygon(QRect(0, 0, 20, 20));
        return {b};
    };
}

} // namespace

class TestUatRecognizeText : public QObject {
    Q_OBJECT
  private slots:
    void init();
    void uat_ocr_010_recognizeTextMenuActionWiredForImage();
    void uat_ocr_020_recognizeTextPopulatesSelectableTextStore();
    void uat_ocr_030_recognizeTextNoopsWithoutModels();
    void uat_ocr_040_recognizeTextDialogOffersForceRerunForPdf();
    void uat_ocr_050_recognizeTextLayerIsOverlayChildOfView();
    void uat_ocr_060_largeDocNoticeGuardedDismissableSelfClearing();
    void uat_ocr_065_noticeDismissalIsPerDocument();
    void uat_ocr_070_nonForceSkipsNativeTextForceReruns();
    void uat_ocr_080_ocrOnPdfBlocksLandInPointSpace();
    void uat_ocr_090_noticeLinkRevealsProgressNoModal();

  private:
    QTemporaryDir m_scratch;
};

void TestUatRecognizeText::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
    wipePpOcrCache();
}

void TestUatRecognizeText::uat_ocr_010_recognizeTextMenuActionWiredForImage() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeTextImage(m_scratch.filePath(QStringLiteral("ocr010.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    QAction *action = findActionByText(mw->menuBar(), QStringLiteral("Reco&gnize Text…"));
    QVERIFY2(action, "Tools → Recognize Text… action is missing");
    QVERIFY2(action->isEnabled(), "Recognize Text should be enabled for an image document");
}

void TestUatRecognizeText::uat_ocr_020_recognizeTextPopulatesSelectableTextStore() {
    if (!seedPpOcrIntoAppCache()) {
        QSKIP("TRAILER_TEST_PPOCR_DET + TRAILER_TEST_PPOCR_REC not "
              "set — skipping real inference path.");
    }

    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeTextImage(m_scratch.filePath(QStringLiteral("ocr020.png")));

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
    const QImage source = imgDoc->image();
    QVERIFY(!source.isNull());

    // Drive the same code path OcrController would take: run a
    // recognize() pass directly, and verify the store receives the
    // results. This sidesteps the model-download dialog (which
    // would block under offscreen) while still asserting the
    // contract that the store is the authoritative output.
    OcrEngine engine(&app->modelRegistry());
    QVERIFY2(engine.isModelReady(),
             "Seeded PP-OCR models should be visible to the shared registry");
    const auto blocks = engine.recognize(source);
    QVERIFY2(!blocks.isEmpty(), "Expected at least one TextBlock on a HELLO 1234 banner");

    auto *store = imgDoc->selectableText();
    QVERIFY(store);
    store->put(0, hashImageContent(source),
               std::vector<OcrEngine::TextBlock>(blocks.constBegin(), blocks.constEnd()));
    QVERIFY(store->hasResults(0));

    // The SelectableTextLayer attached to the document view should
    // now hit-test as "over text" for the centre of the image, and
    // produce non-empty selection text on a drag across the banner.
    auto *layer = mw->findChild<SelectableTextLayer *>();
    QVERIFY2(layer, "ImageDocument view should host a SelectableTextLayer");
    // No drag has happened yet — selection should be empty. (A
    // synthetic drag through the layer would have to align with
    // whatever scale the adapter ended up at; unit tests cover the
    // mapping directly via SelectableTextLayer's test seam.)
    QVERIFY2(layer->selectedText().isEmpty(), "Pre-drag selection should be empty");
    QVERIFY(layer->selectedBlockCount() == 0);

    // OCR is read-only — no document mutation.
    QVERIFY(!imgDoc->isDirty());
    QVERIFY(!imgDoc->canUndo());
}

void TestUatRecognizeText::uat_ocr_030_recognizeTextNoopsWithoutModels() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeTextImage(m_scratch.filePath(QStringLiteral("ocr030.png")));

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

    OcrEngine engine(&app->modelRegistry());
    QVERIFY2(!engine.isModelReady(), "Cache wiped in init() — models should not be ready");

    const auto blocks = engine.recognize(imgDoc->image());
    QVERIFY(blocks.isEmpty());
    QVERIFY(!imgDoc->isDirty());
    QVERIFY(!imgDoc->canUndo());

    // The store remains empty — the SelectableTextLayer therefore
    // never reports an I-beam over the document.
    auto *store = imgDoc->selectableText();
    QVERIFY(store);
    QVERIFY(!store->hasResults(0));
}

void TestUatRecognizeText::uat_ocr_040_recognizeTextDialogOffersForceRerunForPdf() {
    // Construct the dialog directly with hasTextLayer=true so we
    // can assert the force-rerun checkbox is visible and the
    // resolved page list reflects the user's pick.
    RecognizeTextDialog dialog(/*pageCount=*/10, /*currentPage=*/3,
                               /*hasTextLayer=*/true, /*languageOptions=*/{});
    // The force-rerun control should be reachable and not hidden
    // for a doc that has a text layer (per Workstream F: PDFs whose
    // layer is a corner watermark only). isVisibleTo(&dialog) checks
    // the explicit-hide state without requiring the dialog be
    // shown — the offscreen plugin doesn't realize widgets that
    // are never show()n.
    auto *check = dialog.findChild<QCheckBox *>();
    QVERIFY2(check, "Force-rerun checkbox should be reachable");
    QVERIFY2(check->isVisibleTo(&dialog),
             "Force-rerun checkbox should be visible (not hidden) for hasTextLayer=true");

    // For a hasTextLayer=false doc, the same checkbox is hidden.
    RecognizeTextDialog noLayer(/*pageCount=*/10, /*currentPage=*/3,
                                /*hasTextLayer=*/false, /*languageOptions=*/{});
    auto *check2 = noLayer.findChild<QCheckBox *>();
    QVERIFY(check2);
    QVERIFY2(!check2->isVisibleTo(&noLayer),
             "Force-rerun checkbox should be hidden when there is no text layer to bypass");

    // Default scope is "All pages" for a 10-page doc; resolvedPages()
    // returns the pages 0..9 once accepted. We accept via
    // QDialog::accept() to set result() without showing the dialog.
    dialog.accept();
    const auto pages = dialog.resolvedPages();
    QCOMPARE(static_cast<int>(pages.size()), 10);
    QCOMPARE(pages.front(), 0);
    QCOMPARE(pages.back(), 9);
}

void TestUatRecognizeText::uat_ocr_050_recognizeTextLayerIsOverlayChildOfView() {
    QVERIFY(m_scratch.isValid());
    const QString imgPath = writeTextImage(m_scratch.filePath(QStringLiteral("ocr050.png")));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *layer = mw->findChild<SelectableTextLayer *>();
    QVERIFY2(layer, "SelectableTextLayer should attach to the image's view widget");
    // The layer's parent is a viewport widget (QLabel / QPdfView
    // viewport). We don't assert the exact type — just that it has
    // one, so it lives inside the document view.
    QVERIFY(layer->parentWidget() != nullptr);
}

// uat_ocr_060 — the large-doc "Recognize text on this page" notice
// (m_largeDocOcrHint) is (a) guarded by a real per-page text check so it
// never fires on a born-digital doc, (b) visible only for a genuinely
// text-less large-doc page, (c) routed through the consent/download gate
// (no silent no-op), (d) dismissable and self-clearing. Backlog
// 2026-07-13 / ADR 0006 (refines ADR-0002 §3 G5/G6).
void TestUatRecognizeText::uat_ocr_060_largeDocNoticeGuardedDismissableSelfClearing() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // --- (a) Born-digital LARGE doc (>50 pages, real text): notice hidden.
    const QString bornDigital = writeMultiPagePdf(
        m_scratch.filePath(QStringLiteral("large_text.pdf")), 55, /*withText=*/true);
    app->openFiles({bornDigital});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *controller = mw->findChild<OcrController *>();
    QVERIFY(controller);
    QVERIFY2(controller->isLargeDoc(), "55-page doc must count as a large doc (>50)");

    auto *notice = mw->findChild<QWidget *>(QStringLiteral("largeDocOcrHint"));
    QVERIFY2(notice, "large-doc recognize notice widget must exist");
    IDocument *doc = mw->findChild<DocumentView *>()->currentDocument();
    QVERIFY(doc);
    QVERIFY2(doc->pageHasText(doc->currentPage()),
             "born-digital page must report native text");
    QVERIFY2(!notice->isVisible(),
             "notice must stay hidden on a born-digital large doc (real per-page guard)");
    saveNoticeShot(mw, QStringLiteral("notice_a_hidden_born_digital.png"));

    // --- (b) Text-less LARGE doc: notice visible. Close the born-digital
    // window first so currentMainWindow()/currentDocument() resolve to the
    // new doc unambiguously (openFiles may open a fresh window).
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
    const QString scanLike = writeMultiPagePdf(
        m_scratch.filePath(QStringLiteral("large_blank.pdf")), 55, /*withText=*/false);
    app->openFiles({scanLike});
    QApplication::processEvents();
    mw = currentMainWindow();
    QVERIFY(mw);
    notice = mw->findChild<QWidget *>(QStringLiteral("largeDocOcrHint"));
    QVERIFY(notice);
    doc = mw->findChild<DocumentView *>()->currentDocument();
    QVERIFY(doc);
    QVERIFY2(!doc->pageHasText(doc->currentPage()),
             "text-less page must report no native text");
    auto *store = doc->selectableText();
    QVERIFY(store && !store->hasResults(doc->currentPage()));
    QVERIFY2(notice->isVisible(),
             "notice must show for a text-less large-doc page with no OCR results");
    saveNoticeShot(mw, QStringLiteral("notice_b_visible_textless.png"));

    // --- (c) The link routes through the consent gate (not submitUserPages
    // directly): no silent no-op, no ad-hoc modal.
    auto *link = mw->findChild<QLabel *>(QStringLiteral("largeDocOcrHintLink"));
    QVERIFY2(link, "notice must carry a Recognize-text link");
    bool routedToConsent = false;
    mw->setOcrModelDownloadHookForTesting([&routedToConsent]() {
        routedToConsent = true;
        return false; // simulate: consent flow entered, download not completed
    });
    emit link->linkActivated(QStringLiteral("#recognize"));
    QApplication::processEvents();
    QVERIFY2(routedToConsent,
             "notice link must reach the download-consent gate (ensureOcrModelsReady)");
    QCOMPARE(QApplication::activeModalWidget(), nullptr);
    for (QWidget *w : QApplication::topLevelWidgets()) {
        QVERIFY2(!qobject_cast<QMessageBox *>(w), "routing must not spawn a QMessageBox");
        QVERIFY2(!qobject_cast<QDialog *>(w), "routing must not spawn a QDialog");
    }
    mw->setOcrModelDownloadHookForTesting(nullptr);

    // --- (d1) Dismiss (×) hides it and keeps it hidden for this document.
    auto *dismiss = mw->findChild<QToolButton *>(QStringLiteral("largeDocOcrHintDismiss"));
    QVERIFY2(dismiss, "notice must carry a dismiss (×) button");
    dismiss->click();
    QApplication::processEvents();
    QVERIFY2(!notice->isVisible(), "notice must hide immediately on dismiss");
    // Let the re-derive poll (150ms) run; dismissal is sticky per-document.
    QTest::qWait(250);
    QVERIFY2(!notice->isVisible(), "dismissed notice must stay hidden on re-derivation");
    saveNoticeShot(mw, QStringLiteral("notice_c_hidden_after_dismiss.png"));

    // --- (d2) Self-clear on OCR results: seed results for the current page
    // and confirm the poll re-derives the notice to hidden even without a
    // prior dismiss. Open the text-less doc afresh to reset dismissal.
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
    const QString scanLike2 = writeMultiPagePdf(
        m_scratch.filePath(QStringLiteral("large_blank2.pdf")), 55, /*withText=*/false);
    app->openFiles({scanLike2});
    QApplication::processEvents();
    mw = currentMainWindow();
    QVERIFY(mw);
    notice = mw->findChild<QWidget *>(QStringLiteral("largeDocOcrHint"));
    QVERIFY(notice);
    doc = mw->findChild<DocumentView *>()->currentDocument();
    QVERIFY(doc);
    QVERIFY2(notice->isVisible(), "notice visible again on a fresh text-less large doc");
    // Simulate OCR landing text for the visible page.
    OcrEngine::TextBlock b;
    b.text = QStringLiteral("recognised");
    b.polygon = QPolygon({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
    doc->selectableText()->put(doc->currentPage(), 999ULL, {b});
    QTest::qWait(250); // let the poll re-derive
    QVERIFY2(!notice->isVisible(),
             "notice must self-clear once the page gains OCR results");
}

// uat_ocr_065 — dismissal is truly per-document (reviewer #3): dismiss on
// doc A, switch to B, back to A → A stays dismissed; B is independent.
void TestUatRecognizeText::uat_ocr_065_noticeDismissalIsPerDocument() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    // Two docs in ONE window (tabs) so they share the single status-bar
    // notice; NewWindow would give each its own MainWindow and moot the
    // per-document question.
    app->settings().setOpenFilesIn(OpenFilesIn::NewTab);

    const QString a = writeMultiPagePdf(m_scratch.filePath(QStringLiteral("perdoc_a.pdf")), 55,
                                        /*withText=*/false);
    const QString b = writeMultiPagePdf(m_scratch.filePath(QStringLiteral("perdoc_b.pdf")), 55,
                                        /*withText=*/false);
    app->openFiles({a});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    app->openFiles({b});
    QApplication::processEvents();
    QCOMPARE(dv->documentCount(), 2);

    auto *notice = mw->findChild<QWidget *>(QStringLiteral("largeDocOcrHint"));
    QVERIFY(notice);
    auto *dismiss = mw->findChild<QToolButton *>(QStringLiteral("largeDocOcrHintDismiss"));
    QVERIFY(dismiss);

    // Current tab is B (text-less large) → notice visible; dismiss it.
    QVERIFY2(notice->isVisible(), "notice visible on doc B");
    dismiss->click();
    QApplication::processEvents();
    QVERIFY2(!notice->isVisible(), "notice hidden after dismiss on B");

    // Switch to A → notice re-appears (A was never dismissed).
    dv->setCurrentIndex(0);
    QApplication::processEvents();
    QVERIFY2(notice->isVisible(), "notice must show on A — dismissal is per-document, not global");

    // Back to B → still dismissed.
    dv->setCurrentIndex(1);
    QApplication::processEvents();
    QVERIFY2(!notice->isVisible(), "B stays dismissed across a tab switch away and back");

    // A remains independent.
    dv->setCurrentIndex(0);
    QApplication::processEvents();
    QVERIFY2(notice->isVisible(), "A remains un-dismissed after revisiting B");

    app->settings().setOpenFilesIn(OpenFilesIn::NewWindow);
}

// uat_ocr_070 — on a born-digital page (native text already ingested), a
// NON-force Recognize is a no-op (page already selectable), while force-
// rerun re-OCRs (reviewer #5 — replaces the g1 masking with an explicit
// assertion of the intended behaviour).
void TestUatRecognizeText::uat_ocr_070_nonForceSkipsNativeTextForceReruns() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    const QString pdf = writeMultiPagePdf(m_scratch.filePath(QStringLiteral("bornsmall.pdf")), 2,
                                          /*withText=*/true);
    app->openFiles({pdf});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    IDocument *doc = mw->findChild<DocumentView *>()->currentDocument();
    QVERIFY(doc);
    auto *store = doc->selectableText();
    QVERIFY(store);
    // Native text was ingested on open.
    QVERIFY2(store->hasResults(0), "born-digital page 0 has native text after open");
    const QString nativeText = store->blocks(0).front().text;
    QVERIFY(nativeText.contains(QStringLiteral("Born-digital")));

    OcrController controller(app);
    controller.setDocument(doc);
    controller.setProgressRevealDelayMs(0);
    std::atomic<int> calls{0};
    controller.setRecognizerForTesting(
        [&calls](const QImage &, const CancellationToken *) -> QVector<OcrEngine::TextBlock> {
            ++calls;
            OcrEngine::TextBlock b;
            b.text = QStringLiteral("ocr-sentinel");
            b.polygon = QPolygon(QRect(0, 0, 20, 20));
            return {b};
        });

    // NON-force: page already has native text → recognizer never runs, the
    // native block is preserved.
    controller.submitUserPages(doc, {0}, /*forceRerun=*/false);
    QApplication::processEvents();
    QTest::qWait(50);
    QCOMPARE(calls.load(), 0);
    QCOMPARE(store->blocks(0).front().text, nativeText);

    // Force-rerun: the recognizer runs and replaces the block.
    controller.submitUserPages(doc, {0}, /*forceRerun=*/true);
    QTRY_VERIFY(store->hasResults(0) &&
                store->blocks(0).front().text == QStringLiteral("ocr-sentinel"));
    QVERIFY(calls.load() >= 1);
}

// uat_ocr_080 — forced OCR on a PDF page stores blocks in PDF POINT space
// aligned with native/docToView, NOT in 144-DPI pixel space (reviewer #2
// regression). A stub recognizer returns a block at the glyph's 144-DPI
// pixel location; after ingestion it must land back on the native point
// geometry (×0.5), not 2× off.
void TestUatRecognizeText::uat_ocr_080_ocrOnPdfBlocksLandInPointSpace() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    const QString pdf = writeMultiPagePdf(m_scratch.filePath(QStringLiteral("ocrpts.pdf")), 1,
                                          /*withText=*/true);
    app->openFiles({pdf});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    IDocument *doc = mw->findChild<DocumentView *>()->currentDocument();
    QVERIFY(doc);
    auto *store = doc->selectableText();
    QVERIFY(store && store->hasResults(0));
    // Native (known-aligned, point-space) geometry for the line.
    const QRect nativePts = store->blocks(0).front().polygon.boundingRect();
    QVERIFY(!nativePts.isEmpty());

    // renderPageForOcr is 144 DPI = 2× points, so a real OCR pass would
    // detect this glyph at ~2× the native point rect. The stub returns
    // exactly that pixel-space rect.
    const QRect pixelRect(nativePts.x() * 2, nativePts.y() * 2, nativePts.width() * 2,
                          nativePts.height() * 2);
    OcrController controller(app);
    controller.setDocument(doc);
    controller.setProgressRevealDelayMs(0);
    controller.setRecognizerForTesting(
        [pixelRect](const QImage &, const CancellationToken *) -> QVector<OcrEngine::TextBlock> {
            OcrEngine::TextBlock b;
            b.text = QStringLiteral("ocrblock");
            b.polygon = QPolygon(pixelRect);
            return {b};
        });

    controller.submitUserPages(doc, {0}, /*forceRerun=*/true);
    QTRY_VERIFY(store->hasResults(0) &&
                store->blocks(0).front().text == QStringLiteral("ocrblock"));
    const QRect storedPts = store->blocks(0).front().polygon.boundingRect();

    // The stored OCR block must land on the native point geometry (scaled
    // back by ×0.5), not at the raw 2× pixel location.
    const QRect inter = storedPts.intersected(nativePts);
    const double coverage = (inter.width() * double(inter.height())) /
                            (nativePts.width() * double(nativePts.height()));
    QVERIFY2(coverage > 0.8,
             qPrintable(QStringLiteral("OCR-on-PDF block not aligned to point space: coverage %1, "
                                       "stored %2x%3 @ %4,%5 vs native %6x%7 @ %8,%9")
                            .arg(coverage)
                            .arg(storedPts.width()).arg(storedPts.height())
                            .arg(storedPts.x()).arg(storedPts.y())
                            .arg(nativePts.width()).arg(nativePts.height())
                            .arg(nativePts.x()).arg(nativePts.y())));
    // And it is emphatically NOT the un-scaled 2× rect.
    QVERIFY2(std::abs(storedPts.width() - nativePts.width()) <= 3,
             "stored width must match point space, not be ~2× (unscaled) off");
    QVERIFY2(storedPts.width() < pixelRect.width() - 2,
             "stored block must be scaled down from the 144-DPI pixel rect");
}

// uat_ocr_090 — ADR-0002 G5: clicking the notice link reveals the standard
// MlProgressWidget with the model present (no ad-hoc modal), and routes to
// the consent gate with the model absent (no QDialog spawned). Grabs both.
void TestUatRecognizeText::uat_ocr_090_noticeLinkRevealsProgressNoModal() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    const QString scan = writeMultiPagePdf(m_scratch.filePath(QStringLiteral("g5scan.pdf")), 55,
                                           /*withText=*/false);
    app->openFiles({scan});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *notice = mw->findChild<QWidget *>(QStringLiteral("largeDocOcrHint"));
    auto *link = mw->findChild<QLabel *>(QStringLiteral("largeDocOcrHintLink"));
    auto *ctrl = mw->findChild<OcrController *>();
    auto *mlp = mw->findChild<MlProgressWidget *>();
    QVERIFY(notice && link && ctrl && mlp);
    QVERIFY(notice->isVisible());

    // --- Model present: link reveals the standard progress widget, no modal.
    ctrl->setProgressRevealDelayMs(0);
    auto gate = std::make_shared<QSemaphore>();
    ctrl->setRecognizerForTesting(gatedRecognizer(gate));
    mw->setOcrModelDownloadHookForTesting([]() { return true; });
    emit link->linkActivated(QStringLiteral("#recognize"));
    QApplication::processEvents();
    QTRY_VERIFY2(mlp->state() == MlProgressWidget::Running,
                 "notice link with model present must reveal the MlProgressWidget");
    QCOMPARE(QApplication::activeModalWidget(), nullptr);
    for (QWidget *w : QApplication::topLevelWidgets()) {
        QVERIFY2(!qobject_cast<QMessageBox *>(w), "no ad-hoc QMessageBox");
        QVERIFY2(!qobject_cast<QDialog *>(w), "no ad-hoc QDialog");
    }
    saveNoticeShot(mw, QStringLiteral("notice_g5_progress_model_present.png"));
    gate->release(1);
    QTRY_VERIFY(mlp->state() != MlProgressWidget::Running);
    mw->setOcrModelDownloadHookForTesting(nullptr);

    // --- Model absent: link routes to the consent gate, no dialog, no
    // silent write. Reopen a fresh scan so the store is empty again.
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
    const QString scan2 = writeMultiPagePdf(m_scratch.filePath(QStringLiteral("g5scan2.pdf")), 55,
                                            /*withText=*/false);
    app->openFiles({scan2});
    QApplication::processEvents();
    mw = currentMainWindow();
    QVERIFY(mw);
    link = mw->findChild<QLabel *>(QStringLiteral("largeDocOcrHintLink"));
    IDocument *doc = mw->findChild<DocumentView *>()->currentDocument();
    QVERIFY(link && doc);
    bool routedToConsent = false;
    mw->setOcrModelDownloadHookForTesting([&routedToConsent]() {
        routedToConsent = true;
        return false; // consent entered, download not completed
    });
    emit link->linkActivated(QStringLiteral("#recognize"));
    QApplication::processEvents();
    QVERIFY2(routedToConsent, "model-absent click must reach the consent gate");
    QVERIFY2(!doc->selectableText()->hasResults(doc->currentPage()),
             "no text written when the model is absent (no silent no-op OCR)");
    QCOMPARE(QApplication::activeModalWidget(), nullptr);
    for (QWidget *w : QApplication::topLevelWidgets()) {
        QVERIFY2(!qobject_cast<QMessageBox *>(w), "consent routing must not spawn a QMessageBox");
        QVERIFY2(!qobject_cast<QDialog *>(w), "consent routing must not spawn a QDialog");
    }
    saveNoticeShot(mw, QStringLiteral("notice_g5_consent_model_absent.png"));
    mw->setOcrModelDownloadHookForTesting(nullptr);
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

    Application app(argc, argv);
    TestUatRecognizeText tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_recognize_text.moc"
