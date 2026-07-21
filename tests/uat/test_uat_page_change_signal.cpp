// UAT harness — page-changed signal replaces the former 150 ms polls.
//
// Backlog 2026-07-12-page-changed-signal-no-poll. Two consumers used to poll
// IDocument::currentPage() on a timer because IDocument is not a QObject and
// exposed no page-changed signal:
//   (a) MainWindow's auto-OCR / large-doc "Recognize text" hint re-derivation
//       (former m_ocrPagePoll, 150 ms), and
//   (b) the Sidebar thumbnail page-sync (former m_pageSyncTimer, 120 ms).
//
// Both now react to PdfDocument's PageChangeNotifier::currentPageChanged. These
// tests drive a real page change (doc->goToPage(), which routes through the same
// QPdfView navigator jump the keyboard / thumbnail-click paths use) and assert
// each consumer updates *synchronously* — within a single processEvents(), with
// NO QTest::qWait(). A surviving 120/150 ms poll could not have fired in zero
// wall-clock time, so a synchronous update is the behavioral proof the timers
// are retired and the signal path is live.
//
//   uat_pagesig_010_sidebarSelectionFollowsPageChangeWithoutTimer
//   uat_pagesig_020_largeDocOcrHintRederivesOnPageChangeWithoutTimer

#include "app/Application.h"
#include "document/IDocument.h"
#include "document/PageChangeNotifier.h"
#include "settings/Settings.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "ui/OcrController.h"
#include "ui/Sidebar.h"
#include "ui/ThumbnailModel.h"

#include <QDir>
#include <QLabel>
#include <QListView>
#include <QPageSize>
#include <QPainter>
#include <QPdfView>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QTemporaryDir>
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

void saveEvidence(QWidget *w, const QString &fileName) {
    const QString dir = qEnvironmentVariable("TRAILER_UAT_EVIDENCE_DIR");
    if (dir.isEmpty() || !w)
        return;
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(fileName);
    if (!w->grab().save(path, "PNG"))
        qWarning("page_change_signal: failed to save evidence %s", qPrintable(path));
}

// Every page carries a page-numbered line of real text, so all pages are
// born-digital (pageHasText() == true everywhere).
QString writeTextPdf(const QString &path, int pages) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    for (int i = 0; i < pages; ++i) {
        if (i > 0)
            writer.newPage();
        p.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter,
                   QStringLiteral("Page %1").arg(i + 1));
    }
    p.end();
    return path;
}

// A LARGE (> OcrController::kLargeDocPageThreshold) PDF whose EVEN pages carry
// real text and whose ODD pages are blank (no text, no image). pageHasText()
// therefore alternates true/false, so navigating between an even and an odd
// page must toggle the large-doc "Recognize text" hint — the exact per-page
// re-derivation the former poll performed on every tick.
QString writeLargeMixedPdf(const QString &path, int pages) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    for (int i = 0; i < pages; ++i) {
        if (i > 0)
            writer.newPage();
        if (i % 2 == 0) {
            p.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter,
                       QStringLiteral("Page %1 has text").arg(i + 1));
        }
        // Odd pages: draw nothing -> blank, text-less "scanned-like" page.
    }
    p.end();
    return path;
}

QListView *thumbnailListView(Sidebar *sidebar) {
    for (auto *lv : sidebar->findChildren<QListView *>()) {
        if (qobject_cast<ThumbnailModel *>(lv->model()))
            return lv;
    }
    return nullptr;
}

} // namespace

class TestUatPageChangeSignal : public QObject {
    Q_OBJECT
  private slots:
    void init();
    void uat_pagesig_010_sidebarSelectionFollowsPageChangeWithoutTimer();
    void uat_pagesig_020_largeDocOcrHintRederivesOnPageChangeWithoutTimer();

  private:
    QTemporaryDir m_scratch;
};

void TestUatPageChangeSignal::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// (b) Sidebar page-sync: the thumbnail selection follows the document's current
// page off the page-changed signal, synchronously — no poll timer.
void TestUatPageChangeSignal::uat_pagesig_010_sidebarSelectionFollowsPageChangeWithoutTimer() {
    QVERIFY(m_scratch.isValid());
    const QString pdf =
        writeTextPdf(m_scratch.filePath(QStringLiteral("sidebar_sync.pdf")), 5);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdf});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1000, 800);
    mw->show();
    (void)QTest::qWaitForWindowExposed(mw);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    QCOMPARE(doc->pageCount(), 5);
    // The signal source under test: a real PdfDocument page-changed notifier.
    QVERIFY2(doc->pageChangeNotifier(),
             "PdfDocument must expose a PageChangeNotifier for the Sidebar to follow");

    auto *sidebar = mw->findChild<Sidebar *>();
    QVERIFY(sidebar);
    sidebar->setMode(Sidebar::Mode::Pages);
    QApplication::processEvents();

    QListView *view = thumbnailListView(sidebar);
    QVERIFY2(view, "Sidebar should host a ThumbnailModel-backed QListView");
    QCOMPARE(view->model()->rowCount(), 5);

    // Baseline: page 0 selected on open.
    QCOMPARE(doc->currentPage(), 0);
    QCOMPARE(view->currentIndex().row(), 0);

    // Spy the notifier so the failure diagnostic distinguishes "signal never
    // fired" from "Sidebar ignored it".
    QSignalSpy spy(doc->pageChangeNotifier(), &PageChangeNotifier::currentPageChanged);
    QVERIFY(spy.isValid());

    // Drive a page change. Deliberately NO QTest::qWait — only a single
    // processEvents pass to let the synchronous signal/slot settle any queued
    // paint. A 120 ms poll could not have re-synced the selection here.
    doc->goToPage(3);
    QApplication::processEvents();

    QVERIFY2(spy.count() >= 1, "goToPage must emit currentPageChanged");
    QCOMPARE(doc->currentPage(), 3);
    QCOMPARE(view->currentIndex().row(), 3);
    saveEvidence(sidebar, QStringLiteral("pagesig_sidebar_synced_page3.png"));

    // And back, to prove it tracks both directions.
    doc->goToPage(1);
    QApplication::processEvents();
    QCOMPARE(doc->currentPage(), 1);
    QCOMPARE(view->currentIndex().row(), 1);
}

// (a) MainWindow auto-OCR / large-doc hint re-derivation: the per-page
// "Recognize text" hint re-derives off the page-changed signal, synchronously
// — no poll timer. A large mixed doc (even pages text, odd pages blank) makes
// the hint toggle as the current page crosses a text/no-text boundary.
void TestUatPageChangeSignal::uat_pagesig_020_largeDocOcrHintRederivesOnPageChangeWithoutTimer() {
    QVERIFY(m_scratch.isValid());
    const int pages = OcrController::kLargeDocPageThreshold + 10; // > large-doc gate
    const QString pdf =
        writeLargeMixedPdf(m_scratch.filePath(QStringLiteral("large_mixed.pdf")), pages);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdf});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1000, 800);
    mw->show();
    (void)QTest::qWaitForWindowExposed(mw);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    QCOMPARE(doc->pageCount(), pages);
    QVERIFY2(doc->supportsSelectableText(),
             "a PDF must host a SelectableTextStore for the large-doc hint to apply");
    QVERIFY(doc->pageChangeNotifier());

    auto *controller = mw->findChild<OcrController *>();
    QVERIFY(controller);
    QVERIFY2(controller->isLargeDoc(),
             "fixture must exceed the large-doc auto-OCR threshold");

    auto *hint = mw->findChild<QWidget *>(QStringLiteral("largeDocOcrHint"));
    QVERIFY2(hint, "the large-doc 'Recognize text' hint widget must exist");

    // Page 0 has text -> the hint is hidden on open.
    QCOMPARE(doc->currentPage(), 0);
    QVERIFY2(!hint->isVisible(), "hint must be hidden on a text page");

    // Navigate to a BLANK (text-less) odd page. NO QTest::qWait: the hint must
    // re-derive synchronously off the page-changed signal. A surviving 150 ms
    // poll could not have re-derived within a single processEvents pass.
    doc->goToPage(1);
    QApplication::processEvents();
    QCOMPARE(doc->currentPage(), 1);
    QVERIFY2(hint->isVisible(),
             "hint must appear immediately on landing on a text-less page (signal-driven, "
             "not polled)");
    saveEvidence(mw, QStringLiteral("pagesig_largedoc_hint_shown_blankpage.png"));

    // Navigate to a text page -> the hint self-clears, again synchronously.
    doc->goToPage(2);
    QApplication::processEvents();
    QCOMPARE(doc->currentPage(), 2);
    QVERIFY2(!hint->isVisible(), "hint must self-clear on returning to a text page");
    saveEvidence(mw, QStringLiteral("pagesig_largedoc_hint_hidden_textpage.png"));
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
    TestUatPageChangeSignal tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_page_change_signal.moc"
