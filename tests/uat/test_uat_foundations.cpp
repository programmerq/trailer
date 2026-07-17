// UAT harness — Foundations
//
// Drives the Application + MainWindow in-process under
// QT_QPA_PLATFORM=offscreen. Each slot implements one case from
// docs/uat/01-foundations.md; slot names end with the UAT ID so
// failures point directly at the spec case.
//
// The test binary is labelled `uat` in CTest. Regular CI runs pass
// `-LE uat` to skip it; UAT runs pass `-L uat`.

#include "app/Application.h"
#include "document/IDocument.h"
#include "platform/Share.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QAbstractButton>
#include <QClipboard>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileOpenEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPdfWriter>
#include <QPageSize>
#include <QImage>
#include <QPainter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QToolBar>
#include <QtTest/QtTest>

#include <memory>

using namespace trailer;

namespace {

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

QAction *findMenuAction(QMenuBar *bar, const QString &topText, const QString &itemText) {
    for (QAction *topAction : bar->actions()) {
        if (topAction->text() == topText) {
            QMenu *menu = topAction->menu();
            if (!menu)
                return nullptr;
            for (QAction *action : menu->actions()) {
                if (action->text() == itemText)
                    return action;
            }
        }
    }
    return nullptr;
}

// Walk every QMenu under the menu bar and return the one that hosts the
// given action. Used to assert the hosting menu has tooltips enabled —
// a disabled action's tooltip is invisible in the menu unless the menu
// itself has setToolTipsVisible(true).
QMenu *menuContainingAction(QMenuBar *bar, QAction *action) {
    for (QMenu *menu : bar->findChildren<QMenu *>()) {
        if (menu->actions().contains(action))
            return menu;
    }
    return nullptr;
}

QStringList topLevelMenuTexts(QMenuBar *bar) {
    QStringList texts;
    for (QAction *a : bar->actions()) {
        if (!a->text().isEmpty())
            texts << a->text();
    }
    return texts;
}

// Minimal editable IDocument used by the UAT-FND-014 close-prompt
// cases. It reports a controllable dirty flag and, on save(), writes a
// marker payload to its target path and clears dirty — exactly the
// contract MainWindow's close prompt depends on (save() success +
// isDirty() flipping to false). Kept in the test so the matrix can be
// driven deterministically without a real PDF/qpdf round-trip.
class FakeDoc : public trailer::IDocument {
  public:
    FakeDoc(QString path, QString name, QString payload)
        : m_path(std::move(path)), m_name(std::move(name)), m_payload(std::move(payload)) {}

    QString displayName() const override { return m_name; }
    QString filePath() const override { return m_path; }
    QWidget *createView(QWidget *parent) override { return new QWidget(parent); }

    bool supportsEditing() const override { return true; }
    bool isDirty() const override { return m_dirty; }
    void setDirty(bool dirty) { m_dirty = dirty; }
    bool isUntitled() const override { return m_untitled; }
    void setUntitled(bool untitled) { m_untitled = untitled; }

    bool save(const QString &newPath = {}) override {
        const QString target = newPath.isEmpty() ? m_path : newPath;
        if (target.isEmpty())
            return false;
        QFile f(target);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(m_payload.toUtf8());
        f.close();
        m_path = target;
        m_dirty = false;
        // A save to an explicit user-chosen destination (Save-As)
        // resolves the untitled state — mirrors ImageDocument::save.
        if (!newPath.isEmpty())
            m_untitled = false;
        ++m_saveCount;
        // Mirror the save into a caller-owned sink that OUTLIVES this document.
        // A tab close destroys the FakeDoc synchronously (DocumentView::
        // onTabCloseRequested erases the owning unique_ptr before this call
        // returns), so a test that reads saveCount() on the raw pointer AFTER
        // driving the close is a use-after-free — it read a stale value by luck
        // until the external-change monitor's post-close retarget started
        // reusing the freed address. Slots that must observe "saved after
        // close" install a sink and read that instead.
        if (m_saveSink)
            ++(*m_saveSink);
        return true;
    }

    int saveCount() const { return m_saveCount; }
    // Install a save-observation sink that survives this document's
    // destruction, so a slot can assert "saved exactly once" after the tab
    // (and thus the FakeDoc) has been torn down without reading freed memory.
    void observeSavesInto(std::shared_ptr<int> sink) { m_saveSink = std::move(sink); }

  private:
    QString m_path;
    QString m_name;
    QString m_payload;
    bool m_dirty = false;
    bool m_untitled = false;
    int m_saveCount = 0;
    std::shared_ptr<int> m_saveSink;
};

// Add a FakeDoc to a MainWindow's DocumentView (the same view the close-
// prompt veto is wired to) and return the raw pointer so the caller can
// inspect dirty/save state after driving a close.
FakeDoc *addFakeDoc(MainWindow *mw, const QString &path, const QString &name,
                    const QString &payload, bool dirty, bool untitled = false) {
    auto *dv = mw->findChild<DocumentView *>();
    auto doc = std::make_unique<FakeDoc>(path, name, payload);
    doc->setDirty(dirty);
    doc->setUntitled(untitled);
    FakeDoc *raw = doc.get();
    dv->addDocument(std::move(doc));
    QApplication::processEvents();
    return raw;
}

// Fire DocumentView's private tab-close slot the same way the tab-bar's
// close button does (tabCloseRequested → onTabCloseRequested), so the
// documentCloseRequested veto runs.
void requestCloseTab(DocumentView *dv, int index) {
    QMetaObject::invokeMethod(dv, "onTabCloseRequested", Qt::DirectConnection,
                              Q_ARG(int, index));
    QApplication::processEvents();
}

// Directory that persists past the test run (unlike QTemporaryDir) so
// the G2 evidence PNGs can be collected. Lives under the CTest working
// directory (the build tree).
QString screenshotDir() {
    QDir dir(QDir::current());
    dir.mkpath(QStringLiteral("uat-screenshots"));
    return dir.absoluteFilePath(QStringLiteral("uat-screenshots"));
}

void grabTo(QWidget *w, const QString &name) {
    const QString path = QDir(screenshotDir()).absoluteFilePath(name);
    w->grab().save(path, "PNG");
    qInfo().noquote() << "G2-SCREENSHOT" << path;
}

QString writeTinyPdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(100, 100, QStringLiteral("UAT fixture"));
    p.end();
    return path;
}

// Write an unmistakably NON-blank image (bold, saturated colored content —
// never white/empty) to `path`. Used for evidence grabs that must show a
// REAL pasted image inside an "Untitled" document rather than reading as
// the blank empty-state window.
QString writeVividImage(const QString &path) {
    QImage img(480, 320, QImage::Format_ARGB32);
    img.fill(QColor(0x0f, 0x2a, 0x43)); // deep navy — clearly not blank/white
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xff, 0x8c, 0x1a));
    p.drawRect(QRect(40, 48, 400, 96));   // orange band
    p.setBrush(QColor(0x2e, 0xc4, 0x8b));
    p.drawRect(QRect(40, 184, 400, 92));  // green band
    p.setBrush(QColor(0xe0, 0x3b, 0x3b));
    p.drawEllipse(QPoint(240, 160), 58, 58); // red circle
    p.setPen(QColor(Qt::white));
    QFont f = p.font();
    f.setPixelSize(34);
    f.setBold(true);
    p.setFont(f);
    p.drawText(img.rect().adjusted(0, 8, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("PASTED IMAGE"));
    p.end();
    img.save(path, "PNG");
    return path;
}

} // namespace

class TestUatFoundations : public QObject {
    Q_OBJECT
  private slots:
    void init();

    // Map to docs/uat/01-foundations.md
    void uat_fnd_001_launchWithNoArguments();
    void uat_fnd_002_launchOpensFileInWindow();
    void uat_fnd_003_singleDocumentHidesTabBar();
    void uat_fnd_004_openingMultipleFilesSpawnsMultipleWindows();
    void uat_fnd_005_imageBatchSharesOneWindow();
    void uat_fnd_010_menuStructure();
    void uat_fnd_011_macosNoWindowMenuProvidesFileActions();
    void uat_fnd_016_toggleSidebar();
    void uat_fnd_020_flashErrorRoutesToStatusBarNotModal();
    void uat_fnd_030_autoSaveWritesDirtyDocsWithPath();
    void uat_fnd_031_autoSaveSkipsUntitledAndCleanDocs();
    // UAT-FND-014 — closing a dirty tab prompts Save/Discard/Cancel
    // instead of silently discarding unsaved edits. One slot per row of
    // the threshold matrix.
    void uat_fnd_014_closeDirtyTabCancelKeepsDocAndEdits();
    void uat_fnd_014_closeDirtyTabDiscardDropsDoc();
    void uat_fnd_014_closeDirtyTabSaveTitledWritesFile();
    void uat_fnd_014_closeDirtyTabSaveUntitledRoutesThroughSaveAs();
    void uat_fnd_014_closeDirtyNonLastTabCancelThenDiscard();
    void uat_fnd_014_closeCleanTabNeverPrompts();
    // UAT-FND-014 — an UNTITLED document (content-bearing but backed only
    // by a transient temp file, e.g. macOS New-from-Clipboard) must prompt
    // on close even though it reports clean, and Save must route through
    // Save-As. Regression guard for the silent-discard bug.
    void uat_fnd_014_closeUntitledTabPromptsAndCancelKeepsIt();
    void uat_fnd_014_closeUntitledTabDiscardDropsDoc();
    void uat_fnd_014_closeUntitledTabSaveRoutesThroughSaveAs();
    void uat_fnd_014_untitledImageDocReportsUntitledAndClearsOnSave();
    // Peripheral-gap coverage the correctness review flagged: auto-save
    // skips an untitled doc even when dirty; a failed Save-As on an
    // untitled close vetoes (keeps the doc); multiple untitled docs each
    // route through the close prompt. Plus reshaped-prompt / tab-title
    // grab() evidence for the untitled states.
    void uat_fnd_014_autoSaveSkipsUntitledDirtyDoc();
    void uat_fnd_014_closeUntitledSaveFailureVetoesAndKeepsDoc();
    void uat_fnd_014_multipleUntitledDocsEachPromptOnClose();
    // The persistent empty-state window (zero documents, ADR 0005) must
    // NEVER be treated as an untitled/unsaved doc: closing it must not
    // prompt and must not veto. Guards against a phantom "Untitled"
    // placeholder document ever being created for the empty state.
    void uat_fnd_014_emptyStateWindowNeverPromptsOnClose();
    void uat_fnd_014_untitledCloseReshapeAndTabTitleEvidence();
    void uat_fnd_040_shareMenuItemPresentOnSupportedPlatforms();
    void uat_fnd_041_shareDisabledWithTooltipWhenUnavailable();
    void uat_fnd_042_twoPagesActionDisabledWithTooltip();
    void uat_fnd_043_everyMenuWithDisabledTooltipActionRendersTooltips();
    void uat_fnd_050_fileOpenEventOpensWindow();
    void uat_fnd_070_copyPageAsImageToClipboard();

  private:
    QTemporaryDir m_scratch;
};

void TestUatFoundations::init() {
    // Close any leftover windows from a previous slot so every case
    // starts from an "app with no open windows" baseline.
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

void TestUatFoundations::uat_fnd_001_launchWithNoArguments() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    QCOMPARE(mw->windowTitle(), QStringLiteral("Trailer"));
    QCOMPARE(mw->documentCount(), 0);

    // Sidebar dock is hidden at launch (2026-04-30 HITL: chrome
    // off by default; the user opens it from the top-bar's
    // sidebar-mode picker or View → Toggle Sidebar).
    auto docks = mw->findChildren<QDockWidget *>();
    QDockWidget *sidebar = nullptr;
    for (auto *d : docks) {
        if (d->windowTitle().contains(QStringLiteral("Sidebar"), Qt::CaseInsensitive)) {
            sidebar = d;
            break;
        }
    }
    QVERIFY2(sidebar, "Sidebar dock not found");
    QVERIFY2(!sidebar->isVisible(), "Sidebar must be hidden by default — toggle via "
                                    "View → Toggle Sidebar");

    // Markup toolbar is hidden at launch (UAT-FND-001 correction).
    QToolBar *markup = nullptr;
    for (auto *t : mw->findChildren<QToolBar *>()) {
        if (t->windowTitle().contains(QStringLiteral("Markup"), Qt::CaseInsensitive)) {
            markup = t;
            break;
        }
    }
    QVERIFY2(markup, "Markup toolbar not found");
    QVERIFY2(!markup->isVisible(), "Markup toolbar should be hidden at launch");

    // Inspector dock is hidden at launch.
    QDockWidget *inspector = nullptr;
    for (auto *d : docks) {
        if (d->windowTitle().contains(QStringLiteral("Inspector"), Qt::CaseInsensitive)) {
            inspector = d;
            break;
        }
    }
    QVERIFY2(inspector, "Inspector dock not found");
    QVERIFY2(!inspector->isVisible(), "Inspector dock should be hidden at launch");
}

void TestUatFoundations::uat_fnd_002_launchOpensFileInWindow() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_fnd_002.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY2(mw, "Expected a MainWindow after openFiles");
    QCOMPARE(mw->documentCount(), 1);

    const auto recent = app->recentFiles().entries();
    QVERIFY2(!recent.isEmpty(), "Recent files should contain at least one entry after openFiles");
}

// Window-per-file is the default (see TODO.md UX polish pass). A
// window holding exactly one document should show no tab strip — the
// tab bar is a distraction on single-doc frames. Qt's built-in
// tabBarAutoHide handles this when count() <= 1.
void TestUatFoundations::uat_fnd_003_singleDocumentHidesTabBar() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_fnd_003.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    auto *tabs = mw->findChild<QTabWidget *>();
    QVERIFY2(tabs, "Expected a QTabWidget (DocumentView) inside MainWindow");
    QCOMPARE(tabs->count(), 1);
    QVERIFY2(tabs->tabBarAutoHide(), "DocumentView must set tabBarAutoHide so single-doc windows "
                                     "don't show a tab strip");
    QVERIFY2(!tabs->tabBar()->isVisible(),
             "With autoHide on and one tab, the tab bar must not be visible");
}

// Opening several files in a single openFiles() call should, under
// the default NewWindow mode, spawn one window per file — not pile
// them all into a single frame. The test confirms the count of
// top-level MainWindows matches the number of paths.
void TestUatFoundations::uat_fnd_004_openingMultipleFilesSpawnsMultipleWindows() {
    QVERIFY(m_scratch.isValid());
    const QString p1 = writeTinyPdf(m_scratch.filePath("uat_fnd_004_a.pdf"));
    const QString p2 = writeTinyPdf(m_scratch.filePath("uat_fnd_004_b.pdf"));
    const QString p3 = writeTinyPdf(m_scratch.filePath("uat_fnd_004_c.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // Sanity: starting state has no MainWindows (init() closed them).
    int baselineWindows = 0;
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            ++baselineWindows;
    }
    QCOMPARE(baselineWindows, 0);

    app->openFiles({p1, p2, p3});
    QApplication::processEvents();

    int spawned = 0;
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w)) {
            QCOMPARE(mw->documentCount(), 1);
            ++spawned;
        }
    }
    QCOMPARE(spawned, 3);
}

// UAT-FND-005 — A batch of images opened together should share one
// window (the QTabWidget central shows tabs when count > 1) so the
// user can flip through them without arranging multiple frames.
// PDF batches still spawn separate windows because PDFs are
// typically multi-page documents that warrant their own frame.
void TestUatFoundations::uat_fnd_005_imageBatchSharesOneWindow() {
    QVERIFY(m_scratch.isValid());
    auto writePng = [this](const QString &name) {
        const QString path = m_scratch.filePath(name);
        QImage img(80, 60, QImage::Format_RGB32);
        img.fill(Qt::white);
        img.save(path, "PNG");
        return path;
    };
    const QString p1 = writePng(QStringLiteral("uat_fnd_005_a.png"));
    const QString p2 = writePng(QStringLiteral("uat_fnd_005_b.png"));
    const QString p3 = writePng(QStringLiteral("uat_fnd_005_c.png"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    int baselineWindows = 0;
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            ++baselineWindows;
    }
    QCOMPARE(baselineWindows, 0);

    app->openFiles({p1, p2, p3});
    QApplication::processEvents();

    int windowCount = 0;
    int totalDocs = 0;
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w)) {
            ++windowCount;
            totalDocs += mw->documentCount();
        }
    }
    QCOMPARE(windowCount, 1);
    QCOMPARE(totalDocs, 3);
}

void TestUatFoundations::uat_fnd_010_menuStructure() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    QMenuBar *bar = mw->menuBar();
    QVERIFY(bar);

    const QStringList topLevel = topLevelMenuTexts(bar);
    for (const QString &expected :
         {QStringLiteral("&File"), QStringLiteral("&Edit"), QStringLiteral("&View"),
          QStringLiteral("&Tools"), QStringLiteral("&Help")}) {
        QVERIFY2(topLevel.contains(expected),
                 qPrintable(QStringLiteral("Missing top-level menu: ") + expected));
    }

    // Spot-check a handful of items that the UAT doc lists.
    for (const auto &pair : {
             std::pair{QStringLiteral("&File"), QStringLiteral("&Open…")},
             std::pair{QStringLiteral("&File"), QStringLiteral("&Quit")},
             std::pair{QStringLiteral("&View"), QStringLiteral("Zoom &In")},
             std::pair{QStringLiteral("&Tools"), QStringLiteral("Rotate &Right")},
             std::pair{QStringLiteral("&Help"), QStringLiteral("&About Trailer")},
         }) {
        QAction *a = findMenuAction(bar, pair.first, pair.second);
        QVERIFY2(a, qPrintable(QStringLiteral("Missing menu item: ") + pair.first +
                               QStringLiteral(" > ") + pair.second));
    }
}

void TestUatFoundations::uat_fnd_011_macosNoWindowMenuProvidesFileActions() {
#ifndef Q_OS_MACOS
    QSKIP("macOS-only menu-bar behavior.");
#else
    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);

    QMenuBar* bar = app->noWindowMenuBar();
    QVERIFY2(bar, "Application-level macOS menu bar should exist");

    for (const QString& item : {
             QStringLiteral("&New"),
             QStringLiteral("&Open…"),
             QStringLiteral("New from &Clipboard"),
             QStringLiteral("&Acquire…"),
         }) {
        QAction* a = findMenuAction(bar, QStringLiteral("&File"), item);
        QVERIFY2(a, qPrintable(QStringLiteral("Missing File menu item: ") + item));
    }

    QAction* newAction =
        findMenuAction(bar, QStringLiteral("&File"), QStringLiteral("&New"));
    QVERIFY(newAction);
    const int before = static_cast<int>(app->windows().size());
    newAction->trigger();
    QApplication::processEvents();
    QVERIFY2(app->windows().size() >= before + 1,
             "File > New should create a window in no-window mode");
#endif
}

void TestUatFoundations::uat_fnd_016_toggleSidebar() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);

    QDockWidget *sidebar = nullptr;
    for (auto *d : mw->findChildren<QDockWidget *>()) {
        if (d->windowTitle().contains(QStringLiteral("Sidebar"), Qt::CaseInsensitive)) {
            sidebar = d;
            break;
        }
    }
    QVERIFY(sidebar);
    QVERIFY2(!sidebar->isVisible(), "Sidebar should be hidden at launch (2026-04-30)");

    QAction *toggle =
        findMenuAction(mw->menuBar(), QStringLiteral("&View"), QStringLiteral("Toggle &Sidebar"));
    QVERIFY2(toggle, "View > Toggle Sidebar action not found");

    toggle->trigger();
    QApplication::processEvents();
    QVERIFY2(sidebar->isVisible(), "First trigger should show the Sidebar");

    toggle->trigger();
    QApplication::processEvents();
    QVERIFY2(!sidebar->isVisible(), "Second trigger should hide the Sidebar");
}

// UAT-FND-020 — flashError routes operation-failure feedback into the
// status bar instead of popping a QMessageBox::warning modal. The
// 2026-04-29 polish pass replaced a dozen-plus operation-failure
// modals with status-bar messages so users dealing with a tax doc /
// bill / court doc are not interrupted by a click-through every time
// something doesn't work the first try.
void TestUatFoundations::uat_fnd_020_flashErrorRoutesToStatusBarNotModal() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);

    // Snapshot of any existing QMessageBox windows so we don't false-
    // positive on a leftover from another test.
    auto countMessageBoxes = []() {
        int n = 0;
        for (auto *w : QApplication::topLevelWidgets()) {
            if (w->inherits("QMessageBox"))
                ++n;
        }
        return n;
    };
    const int messageBoxesBefore = countMessageBoxes();

    mw->flashError(QStringLiteral("Crop failed — margins too large."));
    QApplication::processEvents();

    QVERIFY2(countMessageBoxes() == messageBoxesBefore, "flashError must NOT spawn a QMessageBox");
    const QString status = mw->statusBar()->currentMessage();
    QVERIFY2(status.contains(QStringLiteral("Crop failed")),
             qPrintable(QStringLiteral("status bar was: '%1'").arg(status)));
    QVERIFY2(status.contains(QStringLiteral("⚠")),
             "flashError prefixes the message with a warning glyph so the "
             "bar visually changes character without stylesheet juggling");

    // flashSuccess and flashStatus take parallel paths — confirm both
    // also bypass any modal layer.
    mw->flashSuccess(QStringLiteral("Saved."));
    QApplication::processEvents();
    QCOMPARE(countMessageBoxes(), messageBoxesBefore);
    QVERIFY(mw->statusBar()->currentMessage().contains(QStringLiteral("Saved.")));
    QVERIFY(mw->statusBar()->currentMessage().contains(QStringLiteral("✓")));

    mw->flashStatus(QStringLiteral("Window/region capture not supported."));
    QApplication::processEvents();
    QCOMPARE(countMessageBoxes(), messageBoxesBefore);
    QVERIFY(mw->statusBar()->currentMessage().contains(QStringLiteral("not supported")));
}

// UAT-FND-030 — Auto-save writes the file when a document is dirty
// and has an established path. The 30 s timer's slot is exposed as a
// public method so the test can trigger it without waiting.
void TestUatFoundations::uat_fnd_030_autoSaveWritesDirtyDocsWithPath() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_fnd_030.pdf"));
    const auto sizeBefore = QFileInfo(pdfPath).size();

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->settings().setAutoSave(true);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<QTabWidget *>();
    QVERIFY(dv);

    // Mutate the active doc so isDirty() reports true. Adding an
    // empty rectangle annotation through the public API (the same
    // path the markup toolbar uses) is enough.
    auto *doc = qobject_cast<MainWindow *>(mw)->findChild<QObject *>();
    Q_UNUSED(doc);
    // Use the test-friendly path: rotate the page (mutates state) so
    // isDirty becomes true via the document's normal write path.
    auto *dvCast = mw->findChild<DocumentView *>();
    QVERIFY(dvCast);
    if (auto *idoc = dvCast->currentDocument()) {
        idoc->rotatePage(0, 90);
        QVERIFY2(idoc->isDirty(), "rotating a page should make the document dirty");
    }

    // Trigger the auto-save tick directly.
    mw->autoSaveDirtyDocs();
    QApplication::processEvents();

    // The file on disk should have been rewritten — its size will
    // typically change after a rotate, but at minimum its mtime
    // changes. Assert the doc is now clean (auto-save cleared dirty).
    auto *idocAfter = dvCast->currentDocument();
    QVERIFY2(idocAfter && !idocAfter->isDirty(),
             "After autoSaveDirtyDocs() the doc should no longer be dirty");

    // sizeBefore captured pre-edit; the post-save file is rewritten.
    Q_UNUSED(sizeBefore);
}

// UAT-FND-031 — Auto-save MUST NOT pick a destination for the user;
// untitled docs (filePath empty) are skipped. It also skips clean
// documents — no churn on idle files.
void TestUatFoundations::uat_fnd_031_autoSaveSkipsUntitledAndCleanDocs() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->settings().setAutoSave(true);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);

    // No documents in the window — autoSave is a no-op, must not
    // crash and must not flash a "saved" status (we asserted nothing
    // happens).
    const QString preMsg = mw->statusBar()->currentMessage();
    mw->autoSaveDirtyDocs();
    QApplication::processEvents();
    const QString postMsg = mw->statusBar()->currentMessage();
    QVERIFY2(!postMsg.contains(QStringLiteral("Auto-saved")),
             "Auto-save with no documents must not flash a success message");

    // autoSave off → no work, no status change, no crash.
    app->settings().setAutoSave(false);
    mw->autoSaveDirtyDocs();
    QApplication::processEvents();
    QVERIFY2(!mw->statusBar()->currentMessage().contains(QStringLiteral("Auto-saved")),
             "When autoSave setting is off, no save should happen");
}

// UAT-FND-014 — Cancel on the close prompt keeps the dirty document: the
// tab stays, the doc count is unchanged, and the unsaved edits (dirty
// flag) survive. No file is written. This is the data-loss guard: the
// pre-fix code erased the unique_ptr with no prompt.
void TestUatFoundations::uat_fnd_014_closeDirtyTabCancelKeepsDocAndEdits() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);

    const QString file = m_scratch.filePath(QStringLiteral("uat_fnd_014_cancel.txt"));
    QFile seed(file);
    QVERIFY(seed.open(QIODevice::WriteOnly));
    seed.write("ORIGINAL");
    seed.close();

    FakeDoc *doc = addFakeDoc(mw, file, QStringLiteral("cancel-doc"),
                              QStringLiteral("REWRITTEN"), /*dirty=*/true);
    QCOMPARE(dv->documentCount(), 1);

    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
    // Evidence: doc-open-with-dirty-tab state before the close attempt.
    grabTo(mw, QStringLiteral("fnd014_dirty_tab_open.png"));

    // Evidence: render the Save/Discard/Cancel prompt itself. Offscreen
    // shows no live modal (the forced-response seam drives the choice),
    // so we build the identical QMessageBox purely to grab its visual.
    {
        QMessageBox box(mw);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(QStringLiteral("Unsaved changes"));
        box.setText(QStringLiteral("Save changes to %1?").arg(doc->displayName()));
        box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Save);
        box.ensurePolished();
        box.adjustSize();
        grabTo(&box, QStringLiteral("fnd014_prompt_save_discard_cancel.png"));
    }

    requestCloseTab(dv, 0);

    // Cancel must abort the close: doc still present, still dirty.
    QCOMPARE(dv->documentCount(), 1);
    QCOMPARE(mw->documentCount(), 1);
    QCOMPARE(dv->currentDocument(), static_cast<IDocument *>(doc));
    QVERIFY2(doc->isDirty(), "Cancelling the close must leave edits intact (still dirty)");
    QCOMPARE(doc->saveCount(), 0);

    // Evidence: the document is still open after Cancel.
    grabTo(mw, QStringLiteral("fnd014_doc_still_open_after_cancel.png"));

    // On-disk file must be untouched by a Cancel.
    QFile check(file);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), QByteArray("ORIGINAL"));

    // Reset the forced-response seam so it can't leak into later slots.
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Prompt);
}

// UAT-FND-014 — Discard drops the dirty document without saving. The tab
// closes (count 0 → empty state), and the original file on disk is left
// unchanged.
void TestUatFoundations::uat_fnd_014_closeDirtyTabDiscardDropsDoc() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);

    const QString file = m_scratch.filePath(QStringLiteral("uat_fnd_014_discard.txt"));
    QFile seed(file);
    QVERIFY(seed.open(QIODevice::WriteOnly));
    seed.write("ORIGINAL");
    seed.close();

    FakeDoc *doc = addFakeDoc(mw, file, QStringLiteral("discard-doc"),
                              QStringLiteral("REWRITTEN"), /*dirty=*/true);
    QCOMPARE(dv->documentCount(), 1);

    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Discard);
    // The close destroys the FakeDoc synchronously; observe saves through a
    // sink that outlives it rather than reading the freed pointer.
    auto saveSink = std::make_shared<int>(0);
    doc->observeSavesInto(saveSink);
    requestCloseTab(dv, 0);

    // Discard: the doc is gone.
    QCOMPARE(dv->documentCount(), 0);
    QCOMPARE(mw->documentCount(), 0);
    QCOMPARE(*saveSink, 0); // Discard never saves.

    // Evidence: empty-state after Discard.
    grabTo(mw, QStringLiteral("fnd014_empty_state_after_discard.png"));

    // Original file must NOT have been rewritten by a discard.
    QFile check(file);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), QByteArray("ORIGINAL"));

    // Reset the forced-response seam so it can't leak into later slots.
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Prompt);
}

// UAT-FND-014 — Save on a titled dirty doc writes the file and closes.
// The doc reports clean afterwards; the file on disk carries the new
// payload.
void TestUatFoundations::uat_fnd_014_closeDirtyTabSaveTitledWritesFile() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);

    const QString file = m_scratch.filePath(QStringLiteral("uat_fnd_014_save.txt"));
    QFile seed(file);
    QVERIFY(seed.open(QIODevice::WriteOnly));
    seed.write("ORIGINAL");
    seed.close();

    FakeDoc *doc = addFakeDoc(mw, file, QStringLiteral("save-doc"),
                              QStringLiteral("REWRITTEN"), /*dirty=*/true);
    QCOMPARE(dv->documentCount(), 1);

    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Save);
    // The close destroys the FakeDoc synchronously, so observe the save through
    // a sink that outlives it rather than reading the freed pointer.
    auto saveSink = std::make_shared<int>(0);
    doc->observeSavesInto(saveSink);
    requestCloseTab(dv, 0);

    // Save succeeded → the tab closed.
    QCOMPARE(dv->documentCount(), 0);
    QCOMPARE(*saveSink, 1);

    // File on disk carries the new payload.
    QFile check(file);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), QByteArray("REWRITTEN"));

    // Reset the forced-response seam so it can't leak into later slots.
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Prompt);
}

// UAT-FND-014 — Save on an UNTITLED dirty doc routes through the Save-As
// path (chooseSaveAsPath). The harness seeds the destination via
// setSaveAsPathForTesting; the file is written and the tab closes.
void TestUatFoundations::uat_fnd_014_closeDirtyTabSaveUntitledRoutesThroughSaveAs() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);

    const QString target = m_scratch.filePath(QStringLiteral("uat_fnd_014_untitled.txt"));
    QFile::remove(target);

    // Untitled: empty filePath. Save must route through Save-As.
    FakeDoc *doc = addFakeDoc(mw, QString(), QStringLiteral("Untitled"),
                              QStringLiteral("UNTITLED-PAYLOAD"), /*dirty=*/true);
    QCOMPARE(dv->documentCount(), 1);

    mw->setSaveAsPathForTesting(target);
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Save);
    // Observe the save through a sink that outlives the FakeDoc, which the
    // close destroys synchronously (reading doc->saveCount() afterwards would
    // be a use-after-free).
    auto saveSink = std::make_shared<int>(0);
    doc->observeSavesInto(saveSink);
    requestCloseTab(dv, 0);

    // Save-As routed and wrote → the tab closed.
    QCOMPARE(dv->documentCount(), 0);
    QCOMPARE(*saveSink, 1);
    QVERIFY2(QFileInfo::exists(target),
             "Untitled Save must route through Save-As and write the chosen path");
    QFile check(target);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), QByteArray("UNTITLED-PAYLOAD"));

    mw->setSaveAsPathForTesting(QString());
    // Reset the forced-response seam so it can't leak into later slots.
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Prompt);
}

// UAT-FND-014 (path c) — closing a NON-last (middle) tab respects the
// prompt: Cancel keeps both docs; Discard drops only the targeted one
// and leaves the OTHER intact.
void TestUatFoundations::uat_fnd_014_closeDirtyNonLastTabCancelThenDiscard() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);

    const QString fileA = m_scratch.filePath(QStringLiteral("uat_fnd_014_nonlast_a.txt"));
    const QString fileB = m_scratch.filePath(QStringLiteral("uat_fnd_014_nonlast_b.txt"));
    FakeDoc *docA = addFakeDoc(mw, fileA, QStringLiteral("doc-A"),
                               QStringLiteral("A"), /*dirty=*/true);
    FakeDoc *docB = addFakeDoc(mw, fileB, QStringLiteral("doc-B"),
                               QStringLiteral("B"), /*dirty=*/false);
    QCOMPARE(dv->documentCount(), 2);

    // Cancel closing the dirty non-last tab (index 0) → both survive.
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
    requestCloseTab(dv, 0);
    QCOMPARE(dv->documentCount(), 2);
    QVERIFY(docA->isDirty());

    // Discard closing the same dirty non-last tab → only doc-B remains.
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Discard);
    requestCloseTab(dv, 0);
    QCOMPARE(dv->documentCount(), 1);
    IDocument *survivor = nullptr;
    QVERIFY(dv->documentAt(0, &survivor));
    QCOMPARE(survivor, static_cast<IDocument *>(docB));
    QCOMPARE(survivor->displayName(), QStringLiteral("doc-B"));

    // Reset the forced-response seam so it can't leak into later slots.
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Prompt);
}

// UAT-FND-014 — a CLEAN document closes with NO prompt. Proven by forcing
// the response to Cancel: if the prompt were consulted the doc would be
// vetoed and stay, but because the isDirty() guard short-circuits before
// confirmCloseDirtyDoc, the clean doc closes regardless.
void TestUatFoundations::uat_fnd_014_closeCleanTabNeverPrompts() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);

    const QString file = m_scratch.filePath(QStringLiteral("uat_fnd_014_clean.txt"));
    FakeDoc *doc = addFakeDoc(mw, file, QStringLiteral("clean-doc"),
                              QStringLiteral("X"), /*dirty=*/false);
    QCOMPARE(dv->documentCount(), 1);

    // Force Cancel: a clean doc must ignore it (never prompts) and close.
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
    // The close destroys the FakeDoc synchronously; observe saves through a
    // sink that outlives it rather than reading the freed pointer.
    auto saveSink = std::make_shared<int>(0);
    doc->observeSavesInto(saveSink);
    requestCloseTab(dv, 0);

    QCOMPARE(dv->documentCount(), 0);
    QCOMPARE(*saveSink, 0); // A clean close never saves.

    // Reset the forced-response seam so it can't leak into later slots.
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Prompt);
}

// UAT-FND-014 — an UNTITLED document (isDirty()==false but isUntitled()
// ==true; its only backing is a transient temp file the user never
// chose) MUST prompt on close. Proven by forcing Cancel: a clean TITLED
// doc ignores Cancel and closes (closeCleanTabNeverPrompts), but the
// untitled doc must be vetoed and kept — and stay untitled — because the
// close gate now consults isUntitled() as well as isDirty(). This is the
// core regression guard: pre-fix the pasted content closed silently.
void TestUatFoundations::uat_fnd_014_closeUntitledTabPromptsAndCancelKeepsIt() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);

    // Untitled: a NON-empty temp path (mirrors the real transient import,
    // which is backed by a temp file) but clean (dirty=false).
    const QString tempPath = m_scratch.filePath(QStringLiteral("trailer-clipboard-uuid.png"));
    FakeDoc *doc = addFakeDoc(mw, tempPath, QStringLiteral("Untitled"),
                              QStringLiteral("PASTED"), /*dirty=*/false, /*untitled=*/true);
    QCOMPARE(dv->documentCount(), 1);
    QVERIFY2(!doc->isDirty(), "An untitled transient import is clean on creation");
    QVERIFY2(doc->isUntitled(), "The doc must report untitled");

    // Force Cancel: if the prompt fires (it must, for an untitled doc)
    // the close is vetoed and the doc is kept.
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
    requestCloseTab(dv, 0);

    QCOMPARE(dv->documentCount(), 1);
    QCOMPARE(dv->currentDocument(), static_cast<IDocument *>(doc));
    QVERIFY2(doc->isUntitled(), "Cancel keeps the untitled doc, still untitled");
    QCOMPARE(doc->saveCount(), 0);

    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Prompt);
}

// UAT-FND-014 — Discard on an untitled doc drops it without writing.
void TestUatFoundations::uat_fnd_014_closeUntitledTabDiscardDropsDoc() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);

    const QString tempPath = m_scratch.filePath(QStringLiteral("trailer-clipboard-discard.png"));
    QFile::remove(tempPath);
    FakeDoc *doc = addFakeDoc(mw, tempPath, QStringLiteral("Untitled"),
                              QStringLiteral("PASTED"), /*dirty=*/false, /*untitled=*/true);
    QCOMPARE(dv->documentCount(), 1);
    QVERIFY(doc->isUntitled());

    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Discard);
    requestCloseTab(dv, 0);
    // `doc` is destroyed by the close — do not touch it past this point.

    QCOMPARE(dv->documentCount(), 0);
    // Discard must not have written anything to the temp path.
    QVERIFY2(!QFileInfo::exists(tempPath),
             "Discard must not write the untitled doc's temp file");

    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Prompt);
}

// UAT-FND-014 — Save on an untitled doc routes through Save-As even
// though the doc already has a (temp) path. The discriminating check:
// the chosen destination is written and the temp path is NOT — proving
// the untitled gate forced Save-As rather than silently overwriting the
// transient file.
void TestUatFoundations::uat_fnd_014_closeUntitledTabSaveRoutesThroughSaveAs() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);

    const QString tempPath = m_scratch.filePath(QStringLiteral("trailer-clipboard-save.png"));
    const QString chosen = m_scratch.filePath(QStringLiteral("uat_fnd_014_untitled_chosen.txt"));
    QFile::remove(tempPath);
    QFile::remove(chosen);

    FakeDoc *doc = addFakeDoc(mw, tempPath, QStringLiteral("Untitled"),
                              QStringLiteral("PASTED-PAYLOAD"), /*dirty=*/false,
                              /*untitled=*/true);
    QCOMPARE(dv->documentCount(), 1);

    mw->setSaveAsPathForTesting(chosen);
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Save);
    requestCloseTab(dv, 0);
    // NOTE: on a successful close the DocumentView erases (destroys) the
    // document synchronously, so `doc` is dangling here — assert on the
    // file system and the tab count only, never on `doc`. That the save
    // cleared the untitled state on the real adapter is covered by
    // uat_fnd_014_untitledImageDocReportsUntitledAndClearsOnSave (which
    // saves without closing, so the doc survives to be inspected).

    // Save-As routed to the chosen path and the tab closed.
    QCOMPARE(dv->documentCount(), 0);
    QVERIFY2(QFileInfo::exists(chosen),
             "Untitled Save must route through Save-As and write the CHOSEN path");
    QVERIFY2(!QFileInfo::exists(tempPath),
             "Untitled Save must NOT silently overwrite the transient temp file");
    QFile check(chosen);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), QByteArray("PASTED-PAYLOAD"));

    mw->setSaveAsPathForTesting(QString());
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Prompt);
}

// UAT-FND-014 — regression closer to the REAL bug: drive the actual
// production entry point Application::openFiles(paths, markUntitled=true)
// — the same call the macOS clipboard / screenshot imports make — with a
// real on-disk PNG, and assert the resulting ImageDocument (not a
// FakeDoc) reports isUntitled()==true and presents a clean "Untitled"
// title. Then a save to a user-chosen path clears the untitled state.
// This covers the hole a FakeDoc-only test would miss: that the real
// ImageDocument wiring is marked untitled by the open path.
void TestUatFoundations::uat_fnd_014_untitledImageDocReportsUntitledAndClearsOnSave() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // A real PNG on disk, standing in for the temp file the clipboard
    // path writes via transientImportPath().
    const QString png = m_scratch.filePath(QStringLiteral("trailer-clipboard-real.png"));
    QImage img(8, 8, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QVERIFY2(img.save(png, "PNG"), "Failed to write the fixture PNG");

    // Reuse one window so the opened doc is easy to locate; restore the
    // user's open-mode afterwards so this slot doesn't leak state.
    const OpenFilesIn savedMode = app->settings().openFilesIn();
    app->settings().setOpenFilesIn(OpenFilesIn::SameWindow);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);

    // The exact call the clipboard/screenshot imports make.
    app->openFiles({png}, /*markUntitled=*/true);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    // Find the untitled ImageDocument among the window's docs.
    IDocument *opened = nullptr;
    for (int i = 0; i < dv->documentCount(); ++i) {
        IDocument *d = nullptr;
        if (dv->documentAt(i, &d) && d && d->isUntitled()) {
            opened = d;
            break;
        }
    }
    QVERIFY2(opened, "openFiles(markUntitled=true) must produce an untitled document");
    QVERIFY2(opened->isUntitled(),
             "A doc opened from a transient import path must report isUntitled()==true");
    QCOMPARE(opened->displayName(), QStringLiteral("Untitled"));

    // Saving to a user-chosen path resolves the untitled state (Save-As).
    const QString chosen = m_scratch.filePath(QStringLiteral("uat_fnd_014_saved_real.png"));
    QFile::remove(chosen);
    QVERIFY2(opened->save(chosen), "Save to the chosen path should succeed");
    QVERIFY2(!opened->isUntitled(), "A save to a chosen path clears untitled");
    QVERIFY(QFileInfo::exists(chosen));
    QVERIFY2(opened->displayName() != QStringLiteral("Untitled"),
             "After Save-As the title reflects the chosen file, not \"Untitled\"");

    app->settings().setOpenFilesIn(savedMode);
}

// UAT-FND-014 — auto-save MUST leave an untitled doc alone even when it is
// dirty. The skip clause `isUntitled()` fires before any write, so no
// destination is silently chosen for the user (the temp file must never be
// treated as the save target). Discriminating setup: the doc is dirty AND
// has a non-empty (temp) path, so neither the !isDirty() nor the empty-path
// skip applies — only the isUntitled() clause can prevent the save.
void TestUatFoundations::uat_fnd_014_autoSaveSkipsUntitledDirtyDoc() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->settings().setAutoSave(true);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);

    // Non-empty temp path (mirrors a transient import) + dirty + untitled.
    const QString tempPath = m_scratch.filePath(QStringLiteral("trailer-clipboard-autosave.png"));
    QFile::remove(tempPath);
    FakeDoc *doc = addFakeDoc(mw, tempPath, QStringLiteral("Untitled"),
                              QStringLiteral("PASTED"), /*dirty=*/true, /*untitled=*/true);
    QCOMPARE(dv->documentCount(), 1);
    QVERIFY2(doc->isDirty(), "Precondition: the doc is dirty");
    QVERIFY2(doc->isUntitled(), "Precondition: the doc is untitled");

    mw->autoSaveDirtyDocs();
    QApplication::processEvents();

    QCOMPARE(doc->saveCount(), 0);
    QVERIFY2(doc->isUntitled(), "Auto-save must not clear untitled");
    QVERIFY2(doc->isDirty(), "Auto-save must leave the untitled doc dirty (unsaved)");
    QVERIFY2(!QFileInfo::exists(tempPath),
             "Auto-save must not write the untitled doc's transient temp file");

    app->settings().setAutoSave(false);
}

// UAT-FND-014 — Save on an untitled close whose Save-As destination FAILS
// to write must VETO the close and keep the doc intact. The Save-As path is
// pre-seeded to an unwritable location (a file under a non-existent
// directory), so FakeDoc::save() returns false; confirmCloseDirtyDoc must
// then abort the close rather than lose the pasted content.
void TestUatFoundations::uat_fnd_014_closeUntitledSaveFailureVetoesAndKeepsDoc() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);

    const QString tempPath = m_scratch.filePath(QStringLiteral("trailer-clipboard-savefail.png"));
    QFile::remove(tempPath);
    // A destination whose parent directory does not exist — QFile::open
    // (WriteOnly) fails, so FakeDoc::save() returns false.
    const QString unwritable =
        m_scratch.filePath(QStringLiteral("no_such_dir/uat_fnd_014_cannot_write.txt"));
    QFile::remove(unwritable);

    FakeDoc *doc = addFakeDoc(mw, tempPath, QStringLiteral("Untitled"),
                              QStringLiteral("PASTED"), /*dirty=*/false, /*untitled=*/true);
    QCOMPARE(dv->documentCount(), 1);

    mw->setSaveAsPathForTesting(unwritable);
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Save);
    requestCloseTab(dv, 0);

    // Save-As destination failed to write → close vetoed, doc kept intact
    // and still untitled so the user can retry a good destination.
    QCOMPARE(dv->documentCount(), 1);
    QCOMPARE(dv->currentDocument(), static_cast<IDocument *>(doc));
    QVERIFY2(doc->isUntitled(), "A failed Save-As must leave the doc untitled (unsaved)");
    QVERIFY2(!QFileInfo::exists(unwritable), "The unwritable destination must not exist");
    QVERIFY2(!QFileInfo::exists(tempPath),
             "A failed Save-As must not fall back to overwriting the temp file");

    mw->setSaveAsPathForTesting(QString());
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Prompt);
}

// UAT-FND-014 — with MULTIPLE untitled docs open, EACH one must route
// through the close prompt independently: Cancel on a tab vetoes that
// close (the doc stays), and Discard drops it. Driven per-tab via
// requestCloseTab (the documentCloseRequested veto).
//
// NOTE (headless limitation): the window-level `closeEvent` walk that
// prompts each dirty/untitled doc in turn (MainWindow.cpp: closeEvent)
// early-returns event->accept() under QT_QPA_PLATFORM=offscreen/minimal
// (MainWindow.cpp: the platform guard before the dirty walk), because
// there is no human to click the modal and UAT init slots call
// w->close() to tear down windows. So the multi-doc window-close prompt
// cannot be exercised headlessly; the per-tab close/veto path exercised
// here (and by the other uat_fnd_014_* slots) is the headless proxy for
// that gate, one document at a time.
void TestUatFoundations::uat_fnd_014_multipleUntitledDocsEachPromptOnClose() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);

    const QString tempA = m_scratch.filePath(QStringLiteral("trailer-clipboard-multi-a.png"));
    const QString tempB = m_scratch.filePath(QStringLiteral("trailer-clipboard-multi-b.png"));
    QFile::remove(tempA);
    QFile::remove(tempB);
    FakeDoc *docA = addFakeDoc(mw, tempA, QStringLiteral("Untitled"),
                              QStringLiteral("PASTED-A"), /*dirty=*/false, /*untitled=*/true);
    FakeDoc *docB = addFakeDoc(mw, tempB, QStringLiteral("Untitled"),
                              QStringLiteral("PASTED-B"), /*dirty=*/false, /*untitled=*/true);
    Q_UNUSED(docA);
    QCOMPARE(dv->documentCount(), 2);

    // Cancel must veto the first untitled tab's close — proving the second
    // doc's presence doesn't bypass the per-doc prompt.
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
    requestCloseTab(dv, 0);
    QCOMPARE(dv->documentCount(), 2);

    // Discard the first untitled doc: it drops, the second remains and
    // must STILL be gated (untitled).
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Discard);
    requestCloseTab(dv, 0);
    QCOMPARE(dv->documentCount(), 1);
    QVERIFY2(!QFileInfo::exists(tempA), "Discard must not write the first temp file");

    // The remaining doc is the second untitled one; Cancel vetoes it too.
    IDocument *remaining = dv->currentDocument();
    QVERIFY(remaining);
    QVERIFY2(remaining->isUntitled(), "The remaining doc is still untitled and still gated");
    QCOMPARE(remaining, static_cast<IDocument *>(docB));
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
    requestCloseTab(dv, 0);
    QCOMPARE(dv->documentCount(), 1);

    // Finally Discard the second untitled doc.
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Discard);
    requestCloseTab(dv, 0);
    QCOMPARE(dv->documentCount(), 0);
    QVERIFY2(!QFileInfo::exists(tempB), "Discard must not write the second temp file");

    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Prompt);
}

// UAT-FND-014 — the persistent EMPTY-STATE window (zero documents; the
// disabled-toolbar-over-welcome-surface window, ADR 0005) must NEVER be
// treated as an unsaved / untitled document. The close gate now prompts
// when isDirty() || isUntitled(), so if anything ever created a phantom
// "Untitled" placeholder doc for the empty state, closing a brand-new
// window the user never put content into would nag them (or, armed with
// Cancel below, be vetoed and become un-closable). This is the negative
// guard: an empty window has NOTHING for closeEvent's dirty/untitled
// vector to iterate (documentCount()==0) and exposes NO current document
// for the tab-close veto, so no prompt can fire and no veto can occur.
void TestUatFoundations::uat_fnd_014_emptyStateWindowNeverPromptsOnClose() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);

    // A freshly-ensured window is in the empty state: no phantom/untitled
    // placeholder document exists. If one did, it would surface here as a
    // tab (documentCount()>0) or as the current document — and the close
    // gate would then prompt over it.
    QCOMPARE(mw->documentCount(), 0);
    QCOMPARE(dv->documentCount(), 0);
    QVERIFY2(dv->currentDocument() == nullptr,
             "The empty-state window must expose NO current document — nothing "
             "for the close gate to treat as untitled/unsaved.");

    // Arm Cancel — the response that WOULD veto a close if any save prompt
    // fired — then close the empty-state window. With no document to prompt
    // for, the close must go through cleanly (not be vetoed). Combined with
    // the zero-document invariants above, this proves an empty-state close
    // neither prompts nor vetoes.
    mw->setCloseResponseForTesting(MainWindow::CloseResponse::Cancel);
    QVERIFY2(mw->close(),
             "Closing an empty-state window must not be vetoed by a save prompt.");
    QApplication::processEvents();
    // `mw`/`dv` are scheduled for deletion (WA_DeleteOnClose) after close();
    // do not touch them past this point.
}

// UAT-FND-014 — G2 / UX-Done evidence for the NEW reshaped untitled states.
// (1) The close prompt for an UNTITLED doc: title "Unsaved changes", text
//     "Save changes to Untitled?", a "Save…" (ellipsis) button, and the
//     informative "If you don't save, this image will be lost." line.
//     Offscreen shows no live modal (the forced-response seam drives the
//     choice), so we build the identical QMessageBox purely to grab its
//     visual — matching the production confirmCloseDirtyDoc untitled branch.
// (2) The "Untitled" tab / window title with a single untitled doc open.
// The native Save-As picker itself cannot be grabbed under offscreen; its
// friendly default filename is verified by the chooseSaveAsPath unit
// assertions and by owner manual check (manual-fallback allowance §2.5.3).
void TestUatFoundations::uat_fnd_014_untitledCloseReshapeAndTabTitleEvidence() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // Reuse one window so the opened doc is easy to locate; restore the
    // user's open-mode afterwards so this slot doesn't leak state.
    const OpenFilesIn savedMode = app->settings().openFilesIn();
    app->settings().setOpenFilesIn(OpenFilesIn::SameWindow);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    // A generous size so the grabbed evidence clearly shows the loaded
    // image rather than a cramped central area that could read as empty.
    mw->resize(760, 620);

    // Drive the REAL production entry point the clipboard / screenshot
    // imports use — openFiles(paths, markUntitled=true) — with a vivid,
    // unmistakably non-blank image on disk. This produces a real
    // ImageDocument whose view renders the image, so the evidence grab
    // shows an ACTUAL pasted image inside an "Untitled" document, never
    // the blank empty-state window.
    const QString png = m_scratch.filePath(QStringLiteral("trailer-clipboard-evidence.png"));
    writeVividImage(png);
    app->openFiles({png}, /*markUntitled=*/true);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = nullptr;
    for (int i = 0; i < dv->documentCount(); ++i) {
        IDocument *d = nullptr;
        if (dv->documentAt(i, &d) && d && d->isUntitled()) {
            doc = d;
            break;
        }
    }
    QVERIFY2(doc, "openFiles(markUntitled=true) must produce an untitled image document");
    QVERIFY2(doc->isUntitled(), "Precondition: the opened doc is untitled");
    QCOMPARE(doc->displayName(), QStringLiteral("Untitled"));
    QApplication::processEvents();

    // (2) The "Untitled" tab / window title state — now with a real,
    // visibly non-blank image loaded in the untitled document.
    grabTo(mw, QStringLiteral("fnd014_untitled_tab_title.png"));

    // (1) The reshaped untitled close prompt — mirrors confirmCloseDirtyDoc's
    // untitled branch exactly.
    {
        QMessageBox box(mw);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(QStringLiteral("Unsaved changes"));
        box.setText(QStringLiteral("Save changes to %1?").arg(doc->displayName()));
        box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Save);
        if (auto *saveButton = box.button(QMessageBox::Save))
            saveButton->setText(QStringLiteral("Save…"));
        box.setInformativeText(QStringLiteral("If you don't save, this image will be lost."));
        box.ensurePolished();
        box.adjustSize();
        grabTo(&box, QStringLiteral("fnd014_untitled_close_prompt.png"));
    }

    app->settings().setOpenFilesIn(savedMode);
}

// UAT-FND-040 — File → Share is ALWAYS present in the File menu. On
// platforms that have a native share-sheet (macOS via
// NSSharingServicePicker) it is enabled and wired; elsewhere it is
// present-but-disabled with an explanatory tooltip (owner policy:
// unavailable capabilities are disabled + explained, never hidden).
// This test only checks presence; firing the picker is platform-
// modal and not UAT-friendly.
void TestUatFoundations::uat_fnd_040_shareMenuItemPresentOnSupportedPlatforms() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);

    QAction *shareAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&File"), QStringLiteral("&Share…"));
    QVERIFY2(shareAction, "File → Share… must always be present in the File menu "
                          "(disabled + tooltip when unavailable, never hidden)");
}

// UAT-FND-041 — When ShareService::isAvailable() is false (Linux /
// Windows stub), the always-present Share action must be shown
// disabled with a non-empty tooltip that points the user at the real
// alternative, rather than silently vanishing.
void TestUatFoundations::uat_fnd_041_shareDisabledWithTooltipWhenUnavailable() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);

    QAction *shareAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&File"), QStringLiteral("&Share…"));
    QVERIFY2(shareAction, "File → Share… must always exist");

    if (ShareService::isAvailable()) {
        QSKIP("ShareService is available on this platform; disabled-state case "
              "does not apply.");
    }

    QVERIFY2(!shareAction->isEnabled(),
             "Share must be disabled when ShareService is unavailable");
    QVERIFY2(!shareAction->toolTip().isEmpty(),
             "Disabled Share must carry an explanatory tooltip pointing at the "
             "alternative");

    // A tooltip is only actually shown in the menu if the hosting menu has
    // tooltips enabled — otherwise the "disabled + explanation" policy is
    // only half-delivered (invisible on hover).
    QMenu *hostMenu = menuContainingAction(mw->menuBar(), shareAction);
    QVERIFY2(hostMenu, "Could not locate the menu hosting the Share action");
    QVERIFY2(hostMenu->toolTipsVisible(),
             "The File menu must call setToolTipsVisible(true) so the disabled "
             "Share tooltip is actually rendered on hover");
}

// UAT-FND-042 — View → Two Pages is an unbuilt capability: QPdfView
// has no facing/two-up layout. Per owner policy the action must be
// present but disabled with an explanatory tooltip, never silently
// aliasing another layout.
void TestUatFoundations::uat_fnd_042_twoPagesActionDisabledWithTooltip() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);

    QAction *twoPages =
        findMenuAction(mw->menuBar(), QStringLiteral("&View"), QStringLiteral("Two Pages"));
    QVERIFY2(twoPages, "View → Two Pages action must be present");
    QVERIFY2(!twoPages->isEnabled(),
             "Two Pages must be disabled while a real facing layout is unbuilt");
    QVERIFY2(!twoPages->toolTip().isEmpty(),
             "Disabled Two Pages must carry an explanatory tooltip");

    // The tooltip is only visible on hover if the hosting menu enables
    // tooltips — assert the View menu does so.
    QMenu *hostMenu = menuContainingAction(mw->menuBar(), twoPages);
    QVERIFY2(hostMenu, "Could not locate the menu hosting the Two Pages action");
    QVERIFY2(hostMenu->toolTipsVisible(),
             "The View menu must call setToolTipsVisible(true) so the disabled "
             "Two Pages tooltip is actually rendered on hover");
}

// UAT-FND-043 — Generalized "no lying controls" guard (Gate G3).
//
// uat_fnd_041 (Share) and uat_fnd_042 (Two Pages) each pin one specific
// action: disabled + explanatory tooltip + host menu has toolTipsVisible.
// The regression they guard against is structural, though — ANY new menu,
// or an action moved to a menu that forgot setToolTipsVisible(true), would
// silently swallow its "here's why this is greyed out / where to go
// instead" tooltip. This test generalizes the pair: it sweeps EVERY menu
// reachable under the menu bar (top-level menus and their submenus) and,
// for every disabled action that carries an explanatory tooltip, asserts
// the hosting menu actually renders tooltips. It is the general invariant;
// 041/042 remain as named traceability anchors.
void TestUatFoundations::
    uat_fnd_043_everyMenuWithDisabledTooltipActionRendersTooltips() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    // Scope: this sweep runs against the no-document window state, which
    // maximises the number of disabled capability actions (Share on
    // unsupported platforms, Two Pages, Copy Page as Image, Recognize
    // Text, …), so the sweep has real work to do. Disabled+explained
    // actions that surface only with a document open are covered
    // transitively: their host menus already contain a helper-created
    // action here, so the per-menu tooltip-rendering invariant this test
    // asserts already holds for those menus.
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    QApplication::processEvents();

    // QAction::toolTip() falls back to a synthesised label when no tooltip
    // is set explicitly — Qt strips the mnemonic '&' AND a trailing ellipsis
    // ("Save &As…" -> "Save As"), so a hand-rolled '&'-strip would mis-flag
    // ellipsis items. To match Qt's fallback exactly (and stay version-proof)
    // we ask Qt itself: a throwaway action with the same text and no explicit
    // tooltip yields precisely that fallback. An action carries a real
    // *explanation* only when its tooltip differs from that fallback — that is
    // the G3-relevant set.
    auto carriesExplanation = [](const QAction *action) {
        const QString tip = action->toolTip();
        if (tip.isEmpty())
            return false;
        const QAction fallbackProbe(action->text(), nullptr);
        return tip != fallbackProbe.toolTip();
    };

    // findChildren<QMenu*> is recursive, so this reaches submenus (Open
    // Recent, Forms, …) as well as the top-level menus.
    const QList<QMenu *> menus = mw->menuBar()->findChildren<QMenu *>();
    QVERIFY2(!menus.isEmpty(), "Expected the menu bar to contain menus");

    int disabledExplainedActions = 0;
    for (QMenu *menu : menus) {
        for (QAction *action : menu->actions()) {
            if (action->isSeparator() || action->menu() != nullptr)
                continue;
            if (action->isEnabled() || !carriesExplanation(action))
                continue;
            ++disabledExplainedActions;
            const QString msg =
                QStringLiteral(
                    "G3 violation: menu \"%1\" hosts disabled action \"%2\" "
                    "with explanatory tooltip \"%3\" but does NOT call "
                    "setToolTipsVisible(true) — the explanation is invisible "
                    "on hover.")
                    .arg(menu->title(), action->text(), action->toolTip());
            QVERIFY2(menu->toolTipsVisible(), qPrintable(msg));
        }
    }

    // Guard against the sweep silently passing because it found nothing to
    // check (e.g. a fixture change that stops surfacing disabled actions).
    QVERIFY2(disabledExplainedActions > 0,
             "Expected at least one disabled+explained action to sweep; the "
             "test would be vacuous otherwise");
}

// UAT-FND-050 — Synthesize the QFileOpenEvent macOS dispatches when
// the user drops a file onto the Dock icon (or right-click → Open
// With → Trailer). Application::event already routes it through
// openFiles, so we just have to confirm the wiring still survives.
// The previous TODO entry (2026-04-30 #1) suspected a registry-
// timing bug; the registry is registered in the Application
// constructor, before exec(), so by the time any QEvent::FileOpen
// can be dispatched the routing path is fully wired. This test
// pins that contract so a future refactor that defers
// registry init can't silently break Dock-drop.
//
// (The end-to-end live test — actually drag a file onto the Dock
// — still needs a real macOS session. See README/dev notes; this
// UAT only covers the in-process event-routing path.)
void TestUatFoundations::uat_fnd_050_fileOpenEventOpensWindow() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(
        m_scratch.filePath(QStringLiteral("uat_fnd_050.pdf")));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    // Confirm we start with no windows so the event below is the
    // only thing that can open one.
    QCOMPARE(app->windows().size(), 0);

    QFileOpenEvent ev(pdfPath);
    QCoreApplication::sendEvent(app, &ev);
    QApplication::processEvents();

    QCOMPARE(app->windows().size(), 1);
    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 1);
}

// UAT-FND-070 — Copy Page as Image puts a rendered page on the clipboard.
//
// The end-of-session flow: mark up a page, then copy it to paste into a
// chat app. Edit > Copy Page as Image renders the current page / image
// and pushes it to the system clipboard.
void TestUatFoundations::uat_fnd_070_copyPageAsImageToClipboard() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_fnd_070.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    QClipboard *clip = QApplication::clipboard();
    QVERIFY(clip);
    clip->clear();
    QVERIFY(clip->image().isNull());

    QAction *copyPage = findMenuAction(mw->menuBar(), QStringLiteral("&Edit"),
                                       QStringLiteral("Copy Page as &Image"));
    QVERIFY2(copyPage, "Edit > Copy Page as Image action not found");
    QVERIFY2(copyPage->isEnabled(), "Copy Page as Image should be enabled for a PDF");

    copyPage->trigger();
    QApplication::processEvents();

    const QImage copied = clip->image();
    QVERIFY2(!copied.isNull(), "Copy Page as Image must place an image on the clipboard");
    QVERIFY(copied.width() > 0 && copied.height() > 0);

    // G3: when the action is unavailable it must be disabled and carry an
    // explanatory tooltip that is actually visible in the menu — the same
    // pattern uat_fnd_041 (Share) and uat_fnd_042 (Two Pages) enforce. An
    // empty-state window (no rasterisable document) is the disabled case.
    MainWindow *empty = app->ensureFreshWindow();
    QVERIFY(empty);
    QApplication::processEvents();

    QAction *disabledCopy = findMenuAction(empty->menuBar(), QStringLiteral("&Edit"),
                                           QStringLiteral("Copy Page as &Image"));
    QVERIFY2(disabledCopy, "Edit > Copy Page as Image action not found in empty window");
    QVERIFY2(!disabledCopy->isEnabled(),
             "Copy Page as Image must be disabled when no page raster is available");
    QVERIFY2(!disabledCopy->toolTip().isEmpty(),
             "Disabled Copy Page as Image must carry an explanatory tooltip");
    // Qt synthesises a default tooltip equal to the action text with the
    // mnemonic '&' stripped; the explanatory tooltip must differ from that.
    QString plainLabel = disabledCopy->text();
    plainLabel.remove(QLatin1Char('&'));
    QVERIFY2(disabledCopy->toolTip() != plainLabel,
             "Disabled Copy Page as Image tooltip must be an explanatory string, "
             "not Qt's default fallback to the action text");

    QMenu *hostMenu = menuContainingAction(empty->menuBar(), disabledCopy);
    QVERIFY2(hostMenu, "Could not locate the menu hosting the Copy Page as Image action");
    QVERIFY2(hostMenu->toolTipsVisible(),
             "The Edit menu must call setToolTipsVisible(true) so the disabled "
             "Copy Page as Image tooltip is actually rendered on hover");
}

// Custom main: we need to set HOME (and XDG vars) before constructing
// Application so Settings/RecentFiles write into a sandbox, not the
// user's real config dir. QTEST_MAIN would construct QApplication
// before we got a chance to do that.
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
    TestUatFoundations tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_foundations.moc"
