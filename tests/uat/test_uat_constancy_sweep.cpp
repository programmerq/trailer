// UAT harness — Generalized spatial-constancy sweep (UAT-XCT-093,
// docs/uat/06-cross-cutting.md).
//
// G10 question 2 ("does any existing control change on-screen position
// when unrelated state changes?") run as a MATRIX instead of per-PR
// prose: every unrelated-state toggle MainWindow offers x the full
// visible chrome inventory (uat_chrome_walk.h — the same tree walk the
// chrome census uses, so move()-positioned floating overlays are
// covered). For each toggle:
//
//   A  snapshot every chrome element's mapTo(window) position
//   B  apply the toggle; assert every element visible in BOTH A and B
//      (minus the toggle's declared whitelist) holds its exact position
//   C  revert the toggle; assert positions returned to A (catches
//      hysteresis — chrome that migrates a pixel per toggle cycle)
//
// Elements visible in only one snapshot are out of scope by
// construction (appearing/disappearing is the census's business;
// MOVING is this sweep's). mapTo(window) — not pos() — so a shifted
// ancestor is charged to every control inside it, exactly as the user
// experiences it. This is uat_zoom_ind_070's geometry oracle
// generalized from one (readout, badge) pair to the whole inventory,
// in the matrix style of test_uat_sweep.cpp, per AGENTS.md G10's own
// evidence rule ("prefer a geometry assertion in a test over a
// screenshot").
//
// STANDING EXCLUSION (the one structural whitelist entry): the
// centralWidget() subtree. The document area is elastic by design —
// showing a toolbar row or a dock legitimately reflows the document
// viewport; the document is the subject, not furniture. Everything
// OUTSIDE the central widget is furniture and must not move.
//
// Toggle inventory (mined from src/ui/MainWindow.cpp):
//   markup-toolbar     View toggle / auto-show path (row 2 appears)
//   form-toolbar       auto-show on fillable PDFs (row 3 appears)
//   sidebar-mode       Hidden -> Pages -> Hidden (left dock)
//   inspector          right dock show/hide
//   theme              applyTheme(Dark) and back (palette + icon refresh)
//   zoom-readout       transient reveal->fade (DR 2026-07-31)
//   status-bar content mlIndicator / read-only badge / OCR hints /
//                      mlProgress forced visible inside their reserved
//                      slots (G10/SC-CRIT-1's regression class)
//   search-expand      Find reveals the toolbar search field
//   document lifecycle open a 2nd doc, switch tabs, close it
//
// Per-toggle whitelists are empty today — every legitimate mover is
// either absent from one of the two snapshots (so out of scope) or
// inside the central-widget exclusion. The mechanism stays so a future
// legitimately-affected control is declared HERE, with a why-comment,
// instead of silently loosening the oracle.
//
// KNOWN-DEFECT TOLERANCES are a separate mechanism from whitelists and
// deliberately not interchangeable: a whitelist entry says "this control
// legitimately moves under this toggle, forever"; a KnownDefect entry
// says "this movement is a PRE-EXISTING G10 violation, frozen so the
// gate can hold the line everywhere else while the fix is tracked to
// done". Every entry cites the docs/backlog item whose threshold is
// exactly the deletion of that entry — the ratchet only tightens. The
// first sweep run surfaced two such defects (both vertical — the
// horizontal SC-CRIT-1 protections hold everywhere):
//   1. status-bar slots reserve width but not HEIGHT; revealing taller
//      content re-centres every permanent widget vertically
//      (docs/backlog/2026-08-28-status-bar-slot-height-not-reserved.md)
//   2. opening Find grows the main-toolbar row (+3px on every row-1
//      control) and QToolBarLayout re-shows the "hidden" search icon
//      beside the open field
//      (docs/backlog/2026-08-28-search-open-reflows-toolbar-and-reshows-button.md)
// Tolerances stay sharp: dx must be 0 unless the entry says otherwise,
// so any NEW horizontal displacement under these toggles still fails.

#include "app/Application.h"
#include "document/IDocument.h"
#include "settings/Settings.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "ui/SearchBar.h"
#include "ui/Sidebar.h"

#include "uat_chrome_walk.h"

#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QScopeGuard>
#include <QSettings>
#include <QStatusBar>

#include <cstdlib>
#include <QTemporaryDir>
#include <QToolBar>
#include <QtTest/QtTest>

#include <functional>

using namespace trailer;

namespace {

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

QString writeTinyPdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(100, 100, QStringLiteral("Constancy fixture"));
    p.end();
    return path;
}

QString writeStaticImage(const QString &path) {
    QImage img(200, 150, QImage::Format_ARGB32);
    img.fill(qRgb(200, 210, 220));
    img.save(path, "PNG");
    return path;
}

template <typename Pred> bool pumpUntil(Pred pred, int budgetMs = 2000) {
    QElapsedTimer timer;
    timer.start();
    while (!pred() && timer.elapsed() < budgetMs)
        QTest::qWait(10);
    return pred();
}

// Chrome positions OUTSIDE the central-widget subtree (the standing
// exclusion — see file header).
// Keyed by the WIDGET POINTER, not the walk id: the walk's "#<n>"
// ordinals count only visible same-base siblings, so a toggle that
// shows/hides an unnamed sibling renames every later same-base id —
// and an id-keyed comparison would silently drop a genuinely displaced,
// still-visible control as "appeared/disappeared" (correctness review,
// 2026-08-28). The pointer is stable across a toggle; the id rides
// along for messages and prefix matching. Pointers are never
// dereferenced during comparison, so a widget destroyed between
// snapshots is harmless (it is simply absent from `after`).
using FurnitureSnapshot = QHash<QWidget *, QPair<QString, QPoint>>;

FurnitureSnapshot furniturePositions(MainWindow *mw) {
    FurnitureSnapshot out;
    QWidget *central = mw->centralWidget();
    const auto elements = trailer_uat::walkChrome(mw);
    for (const auto &e : elements) {
        if (central && (e.widget == central || central->isAncestorOf(e.widget)))
            continue;
        out.insert(e.widget, qMakePair(e.id, e.widget->mapTo(mw, QPoint(0, 0))));
    }
    return out;
}

bool idMatchesPrefix(const QString &id, const QString &prefix) {
    return id == prefix || id.startsWith(prefix + QLatin1Char('/'));
}

bool idWhitelisted(const QString &id, const QStringList &whitelist) {
    for (const QString &p : whitelist) {
        if (idMatchesPrefix(id, p))
            return true;
    }
    return false;
}

// A frozen PRE-EXISTING violation (see file header): movement of `prefix`
// (or its subtree) is tolerated — vertically only, unless allowDx — and
// reported via qInfo with its backlog reference instead of failing.
// Deleting the entry is the cited backlog item's closing threshold.
struct KnownDefect {
    QString prefix;
    bool allowDx;
    QString backlogRef;
    // Magnitude cap (px, per axis) on the tolerated movement. The frozen
    // defects measure 1-4 px; 8 gives headroom for font/DPI variance
    // while keeping the freeze a FREEZE — a future regression moving the
    // same prefix 40 px must still ring the gate, not hide behind the
    // backlog item (HIG review, 2026-08-28). Range tried: unbounded
    // (original) tolerated arbitrarily large drift silently; 4 flaked on
    // a 1px font-metric wobble atop the measured 3px. Symptom to change:
    // a legitimate re-measure of the frozen defect exceeding the cap.
    int maxAbsDelta = 8;
};

// Position deltas for every element visible in both snapshots, minus the
// whitelist. Movement matching a KnownDefect entry is appended to
// `tolerated` instead of the returned violations. Empty return ==
// constancy holds (modulo the frozen, backlog-tracked defects).
QStringList compareSnapshots(const FurnitureSnapshot &before,
                             const FurnitureSnapshot &after,
                             const QStringList &whitelist,
                             const QList<KnownDefect> &knownDefects = {},
                             QStringList *tolerated = nullptr) {
    QStringList violations;
    for (auto it = before.constBegin(); it != before.constEnd(); ++it) {
        if (!after.contains(it.key()))
            continue; // appeared/disappeared: census scope, not sweep scope
        const QString id = it.value().first; // before-snapshot id, for messages
        if (idWhitelisted(id, whitelist))
            continue;
        const QPoint a = it.value().second;
        const QPoint b = after.value(it.key()).second;
        if (a == b)
            continue;
        const QString desc = QStringLiteral("%1 moved (%2,%3) -> (%4,%5)")
                                 .arg(id)
                                 .arg(a.x())
                                 .arg(a.y())
                                 .arg(b.x())
                                 .arg(b.y());
        bool isTolerated = false;
        for (const KnownDefect &d : knownDefects) { // first match wins
            if (!idMatchesPrefix(id, d.prefix))
                continue;
            const bool withinCap = std::abs(a.x() - b.x()) <= d.maxAbsDelta &&
                                   std::abs(a.y() - b.y()) <= d.maxAbsDelta;
            if (withinCap && (d.allowDx || a.x() == b.x())) {
                if (tolerated)
                    *tolerated << desc + QStringLiteral("  [known defect: %1]").arg(d.backlogRef);
                isTolerated = true;
            }
            break;
        }
        if (!isTolerated)
            violations << desc;
    }
    violations.sort();
    return violations;
}

} // namespace

class TestUatConstancySweep : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_xct_093_comparatorNegativeControl();
    void uat_xct_093_togglesDoNotMoveFurniture_data();
    void uat_xct_093_togglesDoNotMoveFurniture();
    void uat_xct_093_documentLifecycleDoesNotMoveFurniture();

  private:
    MainWindow *openPdfWindow(const QString &tag);

    QTemporaryDir m_scratch;
};

void TestUatConstancySweep::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

MainWindow *TestUatConstancySweep::openPdfWindow(const QString &tag) {
    auto *app = qobject_cast<Application *>(qApp);
    if (!app || !m_scratch.isValid())
        return nullptr;
    const QString pdf =
        writeTinyPdf(m_scratch.filePath(QStringLiteral("constancy_%1.pdf").arg(tag)));
    app->openFiles({pdf});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    if (!mw)
        return nullptr;
    mw->resize(1100, 750);
    mw->show();
    QApplication::processEvents();
    auto *dv = mw->findChild<DocumentView *>();
    if (!dv)
        return nullptr;
    if (!pumpUntil([dv] { return dv->currentDocument() != nullptr; }, 5000))
        return nullptr;
    // Let the open-time capability/chrome plumbing settle before the
    // baseline snapshot: re-snapshot until two consecutive position maps
    // 50ms apart agree (the census's settle-oracle technique — see
    // test_uat_chrome_census.cpp's kSettleGapMs rationale — rather than
    // a guessed fixed wait, the load-sensitive-race smell of
    // docs/backlog/2026-08-03-load-sensitive-offscreen-test-races.md).
    QElapsedTimer settle;
    settle.start();
    FurnitureSnapshot prev = furniturePositions(mw);
    while (settle.elapsed() < 5000) {
        QTest::qWait(50);
        const auto now = furniturePositions(mw);
        if (now == prev)
            return mw;
        prev = now;
    }
    return nullptr; // never settled — fail loudly in the caller's QVERIFY
}

// Negative control: the comparator must flag real sibling displacement.
// Inserting a permanent widget at the FRONT of the status bar's
// right-anchored permanent block widens the block leftward and shifts
// every reserved slot — the exact SC-CRIT-1 regression class — so the
// sweep must report movers; if it reports none, every verdict below is
// unsound.
void TestUatConstancySweep::uat_xct_093_comparatorNegativeControl() {
    MainWindow *mw = openPdfWindow(QStringLiteral("negctl"));
    QVERIFY(mw);

    const auto before = furniturePositions(mw);
    QVERIFY2(before.size() >= 10,
             qPrintable(QStringLiteral("only %1 furniture elements in the inventory — walk "
                                       "broken or window not realized")
                            .arg(before.size())));

    auto *probe = new QLabel(QStringLiteral("probe-probe-probe"), mw);
    probe->setObjectName(QStringLiteral("constancyProbeLabel"));
    mw->statusBar()->insertPermanentWidget(0, probe);
    QApplication::processEvents();

    const auto during = furniturePositions(mw);
    const QStringList violations = compareSnapshots(before, during, {});

    mw->statusBar()->removeWidget(probe);
    probe->deleteLater();
    QApplication::processEvents();

    QVERIFY2(!violations.isEmpty(),
             "inserting a permanent status-bar widget displaced no sibling according to the "
             "comparator — the oracle is blind and every sweep verdict is unsound");
    qInfo().noquote() << "negative control detected" << violations.size() << "displaced elements";
}

void TestUatConstancySweep::uat_xct_093_togglesDoNotMoveFurniture_data() {
    QTest::addColumn<QString>("toggleName");

    QTest::newRow("markup-toolbar") << QStringLiteral("markup-toolbar");
    QTest::newRow("form-toolbar") << QStringLiteral("form-toolbar");
    QTest::newRow("sidebar-pages") << QStringLiteral("sidebar-pages");
    QTest::newRow("inspector") << QStringLiteral("inspector");
    QTest::newRow("theme-dark") << QStringLiteral("theme-dark");
    QTest::newRow("zoom-readout") << QStringLiteral("zoom-readout");
    QTest::newRow("ml-indicator") << QStringLiteral("ml-indicator");
    QTest::newRow("read-only-badge") << QStringLiteral("read-only-badge");
    QTest::newRow("large-doc-ocr-hint") << QStringLiteral("large-doc-ocr-hint");
    QTest::newRow("ocr-model-missing-hint") << QStringLiteral("ocr-model-missing-hint");
    QTest::newRow("ml-progress") << QStringLiteral("ml-progress");
    QTest::newRow("search-expand") << QStringLiteral("search-expand");
}

void TestUatConstancySweep::uat_xct_093_togglesDoNotMoveFurniture() {
    QFETCH(QString, toggleName);

    MainWindow *mw = openPdfWindow(toggleName);
    QVERIFY(mw);
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // Resolve the toggle into apply/revert closures + its whitelist +
    // its known-defect tolerances. Whitelists are empty today (see file
    // header); a future entry MUST carry a comment saying why that
    // control legitimately moves. KnownDefect entries freeze
    // PRE-EXISTING violations against their backlog items — see the
    // file header; do not add one without filing the backlog item whose
    // threshold deletes it.
    std::function<void()> apply, revert;
    QStringList whitelist;
    QList<KnownDefect> knownDefects;

    // The status-bar rows below share this entry: slots reserve width
    // but not height, so taller-than-rest content re-centres the whole
    // permanent row vertically (dy only — dx stays hard-asserted).
    const KnownDefect statusBarHeightDefect{
        QStringLiteral("QStatusBar"), /*allowDx=*/false,
        QStringLiteral("docs/backlog/2026-08-28-status-bar-slot-height-not-reserved.md")};

    // Status-bar content widgets are forced visible directly for the
    // geometry probe, uat_zoom_ind_070 style (their production trigger —
    // ML activity, Two-Pages mode, OCR state — is irrelevant to where
    // their SIBLINGS sit).
    auto chromeWidget = [mw](const char *name) {
        return mw->findChild<QWidget *>(QString::fromLatin1(name));
    };

    if (toggleName == QLatin1String("markup-toolbar")) {
        auto *tb = mw->findChild<QToolBar *>(QStringLiteral("MarkupToolbar"));
        QVERIFY(tb);
        QVERIFY2(!tb->isVisible(), "precondition: markup toolbar hidden at rest on a plain PDF");
        apply = [tb] { tb->show(); };
        revert = [tb] { tb->hide(); };
    } else if (toggleName == QLatin1String("form-toolbar")) {
        auto *tb = mw->findChild<QToolBar *>(QStringLiteral("FormToolbar"));
        QVERIFY(tb);
        QVERIFY2(!tb->isVisible(), "precondition: form toolbar hidden at rest on a plain PDF");
        apply = [tb] { tb->show(); };
        revert = [tb] { tb->hide(); };
    } else if (toggleName == QLatin1String("sidebar-pages")) {
        auto *sb = mw->findChild<Sidebar *>();
        QVERIFY(sb);
        apply = [sb] { sb->setMode(Sidebar::Mode::Pages); };
        revert = [sb] { sb->setMode(Sidebar::Mode::Hidden); };
    } else if (toggleName == QLatin1String("inspector")) {
        auto *insp = mw->findChild<QDockWidget *>(QStringLiteral("trailer.inspector"));
        QVERIFY(insp);
        apply = [insp] { insp->show(); };
        revert = [insp] { insp->hide(); };
    } else if (toggleName == QLatin1String("theme-dark")) {
        apply = [app] { app->applyTheme(Theme::Dark); };
        revert = [app] { app->applyTheme(app->settings().theme()); };
    } else if (toggleName == QLatin1String("zoom-readout")) {
        mw->setZoomIndicatorTimingForTesting(60, 40);
        auto *zoomIn = mw->findChild<QAction *>(QStringLiteral("action.view.zoomIn"));
        QVERIFY(zoomIn);
        auto *indicator = mw->findChild<QLabel *>(QStringLiteral("zoomIndicator"));
        QVERIFY(indicator);
        apply = [zoomIn, indicator] {
            zoomIn->trigger();
            QApplication::processEvents();
            QVERIFY2(indicator->isVisible(), "sanity: the readout actually revealed");
        };
        revert = [indicator] {
            QVERIFY2(pumpUntil([indicator] { return !indicator->isVisible(); }, 2000),
                     "sanity: the readout actually faded back out");
        };
    } else if (toggleName == QLatin1String("ml-indicator")) {
        auto *w = chromeWidget("mlIndicator");
        QVERIFY(w);
        apply = [w] { w->setVisible(true); };
        revert = [w] { w->setVisible(false); };
        knownDefects << statusBarHeightDefect; // frame+margin: +3px row growth
    } else if (toggleName == QLatin1String("read-only-badge")) {
        auto *w = chromeWidget("twoPageReadOnlyBadge");
        QVERIFY(w);
        apply = [w] { w->setVisible(true); };
        revert = [w] { w->setVisible(false); };
        knownDefects << statusBarHeightDefect; // styled border/padding: +1px
    } else if (toggleName == QLatin1String("large-doc-ocr-hint")) {
        auto *w = chromeWidget("largeDocOcrHint");
        QVERIFY(w);
        apply = [w] { w->setVisible(true); };
        revert = [w] { w->setVisible(false); };
        knownDefects << statusBarHeightDefect; // link+dismiss row: +4px
    } else if (toggleName == QLatin1String("ocr-model-missing-hint")) {
        // Plain unstyled QLabel — fits the at-rest bar height, so this
        // row holds STRICT today; no tolerance entry.
        auto *w = chromeWidget("ocrModelMissingHint");
        QVERIFY(w);
        apply = [w] { w->setVisible(true); };
        revert = [w] { w->setVisible(false); };
    } else if (toggleName == QLatin1String("ml-progress")) {
        auto *w = chromeWidget("mlProgress");
        QVERIFY(w);
        apply = [w] { w->setVisible(true); };
        revert = [w] { w->setVisible(false); };
        knownDefects << statusBarHeightDefect; // progress+cancel row: +4px
    } else if (toggleName == QLatin1String("search-expand")) {
        auto *find = mw->findChild<QAction *>(QStringLiteral("action.edit.find"));
        QVERIFY(find);
        auto *searchButton = mw->findChild<QWidget *>(QStringLiteral("searchButton"));
        QVERIFY(searchButton);
        auto *searchBar = mw->findChild<SearchBar *>();
        QVERIFY(searchBar);
        auto *input = searchBar->findChild<QLineEdit *>();
        QVERIFY(input);
        apply = [find] { find->trigger(); };
        revert = [input, searchBar] {
            // Esc on the search input collapses the bar back to the
            // icon button (SearchBar::dismissed -> hideSearchBar).
            // Settle on the BAR disappearing, not the button appearing:
            // the frozen defect this row tolerates re-shows the button
            // while the bar is still open, so button visibility is
            // already true mid-collapse and would settle vacuously
            // (HIG review, 2026-08-28).
            QTest::keyClick(input, Qt::Key_Escape);
            QVERIFY2(pumpUntil([searchBar] { return !searchBar->isVisible(); }, 2000),
                     "sanity: dismissing search collapses the bar");
        };
        // Both frozen against the same backlog item; entries are checked
        // first-match, so the button's dx allowance precedes the
        // toolbar-wide dy-only rule.
        const QString searchBacklog = QStringLiteral(
            "docs/backlog/2026-08-28-search-open-reflows-toolbar-and-reshows-button.md");
        // The "hidden" icon is re-shown by QToolBarLayout (wrapper
        // QWidgetAction never hidden) and pushed left by the bar's width.
        // The re-shown button lands beside the open field — displaced by
        // the search bar's full width (~360 px measured offscreen), not
        // a few pixels, so it carries its own cap: bar width + headroom.
        // The cap still rings if the button ever lands somewhere new
        // (e.g. a second row), and dies with the entry when the backlog
        // item closes.
        knownDefects << KnownDefect{QStringLiteral("MainToolbar/searchButton"),
                                    /*allowDx=*/true, searchBacklog, /*maxAbsDelta=*/420};
        // The taller SearchBar grows the row; every row-1 control
        // re-centres +3px down (dy only — dx stays hard-asserted).
        knownDefects << KnownDefect{QStringLiteral("MainToolbar"), /*allowDx=*/false,
                                    searchBacklog};
    } else {
        QFAIL("unknown toggle row");
    }

    const auto baseline = furniturePositions(mw);
    QVERIFY2(baseline.size() >= 10,
             qPrintable(QStringLiteral("only %1 furniture elements before toggle '%2' — walk "
                                       "broken or window not realized")
                            .arg(baseline.size())
                            .arg(toggleName)));

    apply();
    QApplication::processEvents();
    const auto during = furniturePositions(mw);
    QStringList tolerated;
    const QStringList moved = compareSnapshots(baseline, during, whitelist, knownDefects,
                                               &tolerated);
    // Tolerated movement is logged, never silent — the sweep's verdict
    // must stay honest about what the frozen defects cost.
    for (const QString &t : tolerated)
        qInfo().noquote() << "tolerated:" << t;
    QVERIFY2(moved.isEmpty(),
             qPrintable(QStringLiteral(
                            "G10 spatial constancy violated by unrelated toggle '%1' — furniture "
                            "moved:\n  %2\n"
                            "  Either fix the layout so the control holds position (see the "
                            "reserved-slot / floating-overlay patterns in src/ui/MainWindow.cpp), "
                            "or, if this control is LEGITIMATELY affected by this toggle, add it "
                            "to this row's whitelist with a why-comment. See docs/uat/"
                            "06-cross-cutting.md (UAT-XCT-093) and AGENTS.md gate G10.")
                            .arg(toggleName, moved.join(QStringLiteral("\n  ")))));

    revert();
    QApplication::processEvents();
    const auto after = furniturePositions(mw);
    const QStringList drifted = compareSnapshots(baseline, after, {});
    QVERIFY2(drifted.isEmpty(),
             qPrintable(QStringLiteral(
                            "hysteresis after toggling '%1' off again — furniture did not return "
                            "to its baseline position:\n  %2")
                            .arg(toggleName, drifted.join(QStringLiteral("\n  ")))));
}

// Document lifecycle as the unrelated state: opening a second document
// (image, so the per-type default path runs too), switching tabs, and
// closing it must leave every piece of furniture outside the central
// area exactly where it was. The tab bar lives inside DocumentView
// (central) and is covered by the standing exclusion.
void TestUatConstancySweep::uat_xct_093_documentLifecycleDoesNotMoveFurniture() {
    MainWindow *mw = openPdfWindow(QStringLiteral("lifecycle"));
    QVERIFY(mw);
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);

    const auto baseline = furniturePositions(mw);
    QVERIFY2(baseline.size() >= 10, "furniture inventory unexpectedly small");

    // Open a second (image) document into the SAME window as a tab —
    // the default (NewWindow) would spawn a second MainWindow and never
    // exercise this window's furniture. Restored below so no other slot
    // inherits the override.
    const OpenFilesIn openMode = app->settings().openFilesIn();
    app->settings().setOpenFilesIn(OpenFilesIn::NewTab);
    const auto restoreOpenMode =
        qScopeGuard([app, openMode] { app->settings().setOpenFilesIn(openMode); });
    const QString img =
        writeStaticImage(m_scratch.filePath(QStringLiteral("constancy_lifecycle.png")));
    app->openFiles({img});
    QApplication::processEvents();
    QTRY_COMPARE(dv->count(), 2);
    QTest::qWait(150); // per-type default plumbing settles
    QApplication::processEvents();

    const auto afterOpen = furniturePositions(mw);
    QStringList moved = compareSnapshots(baseline, afterOpen, {});
    QVERIFY2(moved.isEmpty(),
             qPrintable(QStringLiteral("opening a second document moved furniture:\n  %1")
                            .arg(moved.join(QStringLiteral("\n  ")))));

    // Switch back to the first tab.
    dv->setCurrentIndex(0);
    QApplication::processEvents();
    QTest::qWait(100);
    QApplication::processEvents();
    const auto afterSwitch = furniturePositions(mw);
    moved = compareSnapshots(baseline, afterSwitch, {});
    QVERIFY2(moved.isEmpty(),
             qPrintable(QStringLiteral("switching tabs moved furniture:\n  %1")
                            .arg(moved.join(QStringLiteral("\n  ")))));

    // Close the second document again.
    QVERIFY(QMetaObject::invokeMethod(dv, "onTabCloseRequested", Q_ARG(int, 1)));
    QApplication::processEvents();
    QTRY_COMPARE(dv->count(), 1);
    const auto afterClose = furniturePositions(mw);
    moved = compareSnapshots(baseline, afterClose, {});
    QVERIFY2(moved.isEmpty(),
             qPrintable(QStringLiteral("closing a document moved furniture:\n  %1")
                            .arg(moved.join(QStringLiteral("\n  ")))));
}

// Custom main: sandbox HOME before Application is constructed so
// Settings/RecentFiles never touch the real config dir.
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
    TestUatConstancySweep tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_constancy_sweep.moc"
