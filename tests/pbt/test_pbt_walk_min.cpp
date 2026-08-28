// PBT harness — S0 minimal slice: seeded 30-step navigation walk.
//
// Property (the G1 line for this slice): after every step of a generated
// 30-step navigation walk over a generated 8-40-page mixed-orientation
// PDF, the viewport paints non-surround content within 5 s, no control
// outside the viewport changes geometry (G10 spatial constancy), and the
// current page index stays in range. A page revisited without an
// intervening geometry change (zoom / view mode / resize) must be
// non-blank after ONE event-loop drain — the strict tier that catches the
// async-renderer-dropped-the-request blank-page bug immediately.
//
// ADVISORY TIER. This binary is a random explorer, not a regression
// guard: it carries the `advisory` ctest label (tests/pbt/CMakeLists.txt)
// and must NEVER join a merge or release gate — see AGENTS.md "CI
// cadence" (only deterministic hard-oracle checks gate; advisory never
// blocks). A failure here is a {seed, pageSpecs, actions} JSON to triage,
// and a confirmed defect then lands as a deterministic replay under the
// `uat` label — the explorer itself stays out of the gates.
//
// Reproduction: every run prints its seed; rerun with
// TRAILER_PBT_SEED=<seed> for an identical walk (proven: 20 consecutive
// fixed-seed runs produce byte-identical traces). On failure the full
// trace JSON is printed and the offending viewport grab() is saved to
// $TRAILER_PBT_EVIDENCE_DIR (or a temp fallback) — matching the
// TRAILER_*_EVIDENCE_DIR env-seam idiom of tests/uat/.
//
// Anti-vacuity: the frame oracle is calibrated EVERY run before the walk
// (it must flag a known-blank surround-filled frame AND pass the settled
// page-1 frame, else the run aborts as broken-harness), and the settling
// primitive (tests/pbt/PbtSettle.h) has its own self-test slots below. An
// uncalibrated pixel oracle silently rotting is the #1 failure mode of a
// harness like this.

#include "PbtSettle.h"

#include "app/Application.h"
#include "document/IDocument.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "util/DocumentSurroundColor.h"

#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenuBar>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfView>
#include <QPdfWriter>
#include <QRandomGenerator>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QtTest/QtTest>

#include <algorithm>
#include <cstdio>

using namespace trailer;

namespace {

// ---- Tuned constants (each: what it is / range tried / symptom to change) --

// Walk length. What it represents: how deep one case explores before the
// process resets. Range tried: 10 (too shallow to compose zoom+nav+resize
// interactions), 30 (S0 spec value; a case runs in seconds), 100 (S1's
// nightly shape — pointless before a shrinker exists, eyeballing a
// 100-action JSON is not "week one"). Symptom to change: raise once the
// ddmin shrinker lands; lower if a walk stops fitting a PR-CI minute.
constexpr int kWalkSteps = 30;

// Generated document size. What it represents: enough pages that
// goToPage(rand) genuinely jumps across unrealized layout, small enough
// that offscreen layout stays cheap. Range tried: 8-40 (S0 spec; 8 is the
// existing writeSamplePdf fixture size, 40 keeps continuous-mode layout
// under a second offscreen). Symptom to change: raise the ceiling when
// hunting lazy-layout bugs specifically (S3's 5000-page forge is the real
// tool for that).
constexpr int kMinPages = 8;
constexpr int kMaxPages = 40;

// Eventually-non-blank timeout, ms. What it represents: the outer bound
// for "content or an honest placeholder must appear" after a step —
// generous because blank-during-load is POLICY here (the staged-open ADR's
// grace window), so only surround-forever is a failure. Range tried:
// 1000 (false alarms on a cold first render under load), 5000 (design
// value, no false alarms observed). Symptom to change: raise if a loaded
// runner ever times out on a frame that a rerun of the same seed passes.
constexpr int kEventuallyMs = 5000;

// Poll interval inside the eventually-non-blank loop, ms. Range tried:
// 10-50 (grab() is ~1 ms offscreen at 900x700, so this only bounds
// detection latency). Symptom to change: none expected; it is not a
// correctness knob.
constexpr int kBlankPollMs = 25;

// Frame-oracle grid: kGridN x kGridN samples over the central
// kCentralFrac of the viewport grab. What they represent: a cheap
// fixed-cost sample that avoids margins and scrollbar gutters
// (brainstorm §4 — cheaper than downsample+histogram, as effective).
// Range tried: 8x8 (a 0.10-zoom page can slip between samples), 16x16
// (256 samples; a page must cover >1% of the central area to register —
// see kMinWalkZoom below, which keeps that true), 32x32 (no extra signal,
// 4x the loops). kCentralFrac 0.8 tried against 1.0 (full frame includes
// QPdfView's own margins, diluting the surround fraction near the 99%
// boundary). Symptom to change: if calibration ever flags the settled
// page-1 frame as blank, widen the grid before touching the threshold.
constexpr int kGridN = 16;
constexpr double kCentralFrac = 0.80;

// Per-channel tolerance for "this sample IS the surround colour", 0-255.
// What it represents: absorbs offscreen raster rounding on the pinned
// palette colour without ever confusing surround grey with page white
// (stock palettes: surround #9f9f9f vs page #ffffff — distance 96).
// Range tried: 0 (exact match works offscreen today but couples the
// oracle to raster internals), 4-16 (all discriminate cleanly; 8 keeps
// >10x headroom to the grey/white gap). Symptom to change: raise if
// calibration's known-blank check starts failing on a platform whose
// backing store dithers; lower if a dark theme ever narrows the
// surround/page distance below ~3x the tolerance.
constexpr int kSurroundEps = 8;

// Fraction of samples at surround colour for a frame to count as BLANK.
// What it represents: "the viewport shows nothing but canvas" while
// tolerating a stray sample on a page shadow/border. Range tried: 1.0
// (a single border-straddling sample makes a truly blank frame pass as
// content — vacuous oracle), 0.99 (design value: >= 254/256 samples;
// content must cover >= 3 samples), 0.95 (a sliver of stale page at a
// viewport edge already counts as "content" — misses partial-blank bugs).
// Symptom to change: lower toward 0.95 only with evidence of a false
// blank verdict on a legitimately tiny-content frame the walk should
// accept.
constexpr double kBlankFraction = 0.99;

// Zoom floor below which the walk skips further zoomOut steps. What it
// represents: a generator constraint, not an oracle weakening — at
// QPdfView's kZoomMin (0.10) an A4 page covers ~1.5% of the central
// sample area, right at the 99%-blank boundary, so the oracle could call
// a real page "blank". At 0.5 the page covers >20% of the frame with
// wide margin. Range tried: 0.25 (page ~5% of samples — passes but with
// no headroom against a landscape page + tall window), 0.5. Symptom to
// change: lower it if zoomed-way-out bugs become a target AND the oracle
// gains a page-box-aware sampler. Skipped steps are recorded in the
// trace ("executed": false) so the walk stays a pure function of seed.
constexpr double kMinWalkZoom = 0.5;

// Window-resize bounds for the resize action, logical px. What they
// represent: plausible desktop window sizes that keep the offscreen
// virtual screen honest and the fit-mode relayouts real. Range tried:
// 700-1100 x 500-900 around the 900x700 baseline every UAT slot uses.
// Symptom to change: widen once tiny/huge-window robustness becomes a
// target (that is Layer-1 sweep territory, not this walk's).
constexpr int kMinW = 700, kMaxW = 1100, kMinH = 500, kMaxH = 900;

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

// ---- Seeded document generator ---------------------------------------------

// Extends the writeSamplePdf idiom (tests/uat/test_uat_viewer.cpp): one
// labelled A4 page per entry, orientation switched MID-DOCUMENT via
// setPageLayout before each newPage (QPagedPaintDevice applies a layout
// change to the pages that follow it).
QString writeMixedOrientationPdf(const QString &path, const QList<bool> &landscapePerPage) {
    QPdfWriter writer(path);
    const QPageSize a4(QPageSize::A4);
    auto layoutFor = [&a4](bool landscape) {
        return QPageLayout(a4, landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                           QMarginsF());
    };
    writer.setPageLayout(layoutFor(landscapePerPage.value(0)));
    QPainter p(&writer);
    for (int i = 0; i < landscapePerPage.size(); ++i) {
        p.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter,
                   QStringLiteral("Page %1").arg(i + 1));
        if (i < landscapePerPage.size() - 1) {
            writer.setPageLayout(layoutFor(landscapePerPage.at(i + 1)));
            writer.newPage();
        }
    }
    p.end();
    return path;
}

// ---- Walk actions ----------------------------------------------------------

enum class Action {
    NextPage,
    PrevPage,
    GoToPage,
    ViewModeToggle,
    ZoomIn,
    ZoomOut,
    FitWidth,
    Resize
};
constexpr int kActionCount = 8;

const char *actionName(Action a) {
    switch (a) {
    case Action::NextPage:
        return "nextPage";
    case Action::PrevPage:
        return "prevPage";
    case Action::GoToPage:
        return "goToPage";
    case Action::ViewModeToggle:
        return "viewModeToggle";
    case Action::ZoomIn:
        return "zoomIn";
    case Action::ZoomOut:
        return "zoomOut";
    case Action::FitWidth:
        return "fitWidth";
    case Action::Resize:
        return "resize";
    }
    return "?";
}

struct Step {
    Action action;
    int p1 = -1; // GoToPage: target page. Resize: width.
    int p2 = -1; // Resize: height.
};

// Does this action change render geometry, invalidating the thin model's
// "this page's rendered frame is cached and servable" promise?
bool invalidatesRenderCache(Action a) {
    return a == Action::ViewModeToggle || a == Action::ZoomIn || a == Action::ZoomOut ||
           a == Action::FitWidth || a == Action::Resize;
}

// ---- Frame oracle ----------------------------------------------------------

struct FrameOracle {
    QColor surround;

    // Fraction of grid samples within kSurroundEps of the surround colour.
    double surroundFraction(const QImage &frame) const {
        const int w = frame.width(), h = frame.height();
        if (w <= 0 || h <= 0)
            return 1.0; // an empty grab is maximally blank
        const double x0 = w * (1.0 - kCentralFrac) / 2.0;
        const double y0 = h * (1.0 - kCentralFrac) / 2.0;
        const double cw = w * kCentralFrac, ch = h * kCentralFrac;
        int atSurround = 0;
        for (int iy = 0; iy < kGridN; ++iy) {
            for (int ix = 0; ix < kGridN; ++ix) {
                const int sx = qBound(0, int(x0 + (ix + 0.5) * cw / kGridN), w - 1);
                const int sy = qBound(0, int(y0 + (iy + 0.5) * ch / kGridN), h - 1);
                const QColor c = frame.pixelColor(sx, sy);
                if (std::abs(c.red() - surround.red()) <= kSurroundEps &&
                    std::abs(c.green() - surround.green()) <= kSurroundEps &&
                    std::abs(c.blue() - surround.blue()) <= kSurroundEps)
                    ++atSurround;
            }
        }
        return double(atSurround) / (kGridN * kGridN);
    }

    bool isBlank(const QImage &frame) const { return surroundFraction(frame) >= kBlankFraction; }
};

} // namespace

class TestPbtWalkMin : public QObject {
    Q_OBJECT
  private slots:
    void init();

    // Self-tests for the settling primitive (PbtSettle.h) — the walk's
    // credibility rests on this one helper, so it gets its own oracle.
    void settle_staticWidgetQuiesces();
    void settle_animatedWidgetNeverQuiesces();

    void walk_min();

  private:
    QImage grabViewport();
    void saveEvidence(const QImage &frame, quint32 seed, int step, const QString &why);
    QJsonObject traceJson() const;
    QJsonObject traceJsonGenerated() const;

    QTemporaryDir m_scratch;

    // Walk bookkeeping, kept as members so the failure path can dump the
    // full {seed, pageSpecs, actions} JSON from any assertion site.
    quint32 m_seed = 0;
    QList<bool> m_landscape;
    QList<Step> m_steps;
    QJsonArray m_traceActions;
    QPdfView *m_view = nullptr;
};

void TestPbtWalkMin::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// A shown-but-untouched widget must reach paint quiescence well inside the
// timeout — otherwise every walk step would burn its full settle budget.
void TestPbtWalkMin::settle_staticWidgetQuiesces() {
    QWidget w;
    w.resize(300, 200);
    w.show();
    QVERIFY2(pbt::waitForPaintQuiescence(&w, 2000),
             "a static widget must quiesce (settling primitive broken: nothing will settle)");
}

// A widget repainting on a timer must NOT quiesce — otherwise the helper
// would declare mid-animation (mid-render) views settled and every
// downstream oracle would race. Also asserts paints were actually counted,
// so a filter installed on the wrong object can't pass vacuously.
void TestPbtWalkMin::settle_animatedWidgetNeverQuiesces() {
    QWidget w;
    w.resize(300, 200);
    w.show();
    pbt::PaintCounter proof;
    w.installEventFilter(&proof);
    QTimer repaint;
    QObject::connect(&repaint, &QTimer::timeout, &w, qOverload<>(&QWidget::update));
    repaint.start(pbt::kSettleSpinWaitMs / 2 + 1); // repaint faster than a quiet spin
    QVERIFY2(!pbt::waitForPaintQuiescence(&w, 500),
             "a continuously-repainting widget must not read as quiescent");
    QVERIFY2(proof.paints > 0, "vacuity guard: the animated widget never painted at all");
}

QImage TestPbtWalkMin::grabViewport() {
    return m_view->viewport()->grab().toImage();
}

void TestPbtWalkMin::saveEvidence(const QImage &frame, quint32 seed, int step, const QString &why) {
    // Env-seam idiom (TRAILER_*_EVIDENCE_DIR, tests/uat/): the caller
    // points this at a gitignored working dir (uat-screenshots/ is the
    // repo's throwaway bucket); unset, fall back to the system temp dir so
    // a bare failing run still leaves an inspectable frame. Never a
    // committed path — G2's curated-evidence flow does not apply to
    // explorer failure artifacts.
    QString dir = QString::fromLocal8Bit(qgetenv("TRAILER_PBT_EVIDENCE_DIR"));
    if (dir.isEmpty())
        dir = QDir::temp().filePath(QStringLiteral("trailer-pbt-evidence"));
    QDir().mkpath(dir);
    const QString file = QDir(dir).filePath(
        QStringLiteral("pbt_walk_seed%1_step%2_%3.png").arg(seed).arg(step).arg(why));
    if (frame.save(file, "PNG"))
        qWarning().noquote() << "PBT-EVIDENCE" << file;
    else
        qWarning().noquote() << "PBT-EVIDENCE could not save" << file;
}

QJsonObject TestPbtWalkMin::traceJson() const {
    QJsonArray pages;
    for (bool l : m_landscape)
        pages.append(l ? QStringLiteral("landscape") : QStringLiteral("portrait"));
    QJsonObject o;
    o.insert(QStringLiteral("seed"), qint64(m_seed));
    o.insert(QStringLiteral("pageSpecs"), pages);
    o.insert(QStringLiteral("actions"), m_traceActions);
    return o;
}

// The GENERATED inputs (pure function of the seed): page orientations plus
// the full pre-execution action list. This is what the hang-forensics
// stderr line carries — execution hasn't happened yet, so there are no
// executed/pageAfter fields.
QJsonObject TestPbtWalkMin::traceJsonGenerated() const {
    QJsonArray pages;
    for (bool l : m_landscape)
        pages.append(l ? QStringLiteral("landscape") : QStringLiteral("portrait"));
    QJsonArray actions;
    for (int i = 0; i < m_steps.size(); ++i) {
        const Step &s = m_steps.at(i);
        QJsonObject e;
        e.insert(QStringLiteral("i"), i);
        e.insert(QStringLiteral("a"), QLatin1String(actionName(s.action)));
        if (s.p1 >= 0)
            e.insert(QStringLiteral("p1"), s.p1);
        if (s.p2 >= 0)
            e.insert(QStringLiteral("p2"), s.p2);
        actions.append(e);
    }
    QJsonObject o;
    o.insert(QStringLiteral("seed"), qint64(m_seed));
    o.insert(QStringLiteral("pageSpecs"), pages);
    o.insert(QStringLiteral("actions"), actions);
    return o;
}

void TestPbtWalkMin::walk_min() {
    QVERIFY(m_scratch.isValid());

    // ---- Seed (printed for replay; override via TRAILER_PBT_SEED) ----------
    const QByteArray seedEnv = qgetenv("TRAILER_PBT_SEED");
    bool seedOk = false;
    m_seed = seedEnv.toUInt(&seedOk);
    if (!seedOk && !seedEnv.isEmpty()) {
        // A set-but-unparsable seed (hex spelling, stray space, > 2^32-1)
        // must NOT silently fall back to a random walk: a triager
        // replaying a failure would get a passing random walk and wrongly
        // close the finding as non-reproducing (correctness review,
        // 2026-08-28). Decimal unsigned only, by contract.
        QFAIL(qPrintable(QStringLiteral("TRAILER_PBT_SEED is set but not a decimal uint32: "
                                        "'%1' — refusing to substitute a random seed")
                             .arg(QString::fromLatin1(seedEnv))));
    }
    if (!seedOk)
        m_seed = QRandomGenerator::global()->generate();
    qInfo().noquote()
        << QStringLiteral("PBT walk seed %1 (replay: TRAILER_PBT_SEED=%1)").arg(m_seed);
    QRandomGenerator rng(m_seed);

    // ---- Generate: document spec + the full action list, up front, so both
    // are a pure function of the seed (determinism is this slice's G1 line).
    const int pageCount = kMinPages + int(rng.bounded(quint32(kMaxPages - kMinPages + 1)));
    m_landscape.clear();
    for (int i = 0; i < pageCount; ++i)
        m_landscape.append(rng.bounded(2u) == 1u);
    m_steps.clear();
    for (int i = 0; i < kWalkSteps; ++i) {
        Step s;
        s.action = Action(rng.bounded(quint32(kActionCount)));
        if (s.action == Action::GoToPage)
            s.p1 = int(rng.bounded(quint32(pageCount)));
        else if (s.action == Action::Resize) {
            s.p1 = kMinW + int(rng.bounded(quint32(kMaxW - kMinW + 1)));
            s.p2 = kMinH + int(rng.bounded(quint32(kMaxH - kMinH + 1)));
        }
        m_steps.append(s);
    }

    // Hang forensics: qInfo/qWarning are BUFFERED by QtTest until the slot
    // finishes, so a walk that livelocks the GUI thread (observed: a
    // posted-event storm under a seeded resize/fit combination — see the
    // S0 findings) would die under ctest's TIMEOUT leaving no trace at
    // all. Write the full generated input, and each step as it STARTS,
    // straight to stderr with an explicit flush: a killed run then still
    // carries its input and the step it died in.
    {
        const QByteArray gen = QJsonDocument(traceJsonGenerated()).toJson(QJsonDocument::Compact);
        std::fprintf(stderr, "PBT-GENERATED %s\n", gen.constData());
        std::fflush(stderr);
    }

    const QString pdfPath = writeMixedOrientationPdf(
        m_scratch.filePath(QStringLiteral("pbt_walk_%1.pdf").arg(m_seed)), m_landscape);

    // ---- Open + realize (the house idiom from tests/uat/test_uat_viewer.cpp)
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(900, 700);
    mw->show();
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    m_view = mw->findChild<QPdfView *>();
    QVERIFY2(m_view && m_view->document(), "MainWindow should host a QPdfView with a document");
    QCOMPARE(doc->pageCount(), pageCount); // docgen sanity: the spec round-trips

    // Settle the open, then wait out the staged-open grace window: content
    // (not the surround canvas) must eventually appear on page 1.
    FrameOracle oracle{documentSurroundColor(QApplication::palette())};
    // A view that never quiesces would silently turn every later oracle
    // timing-dependent — the exact flake class PbtSettle.h exists to
    // prevent — so non-quiescence is a loud failure, not a shrug
    // (correctness review, 2026-08-28). Advisory tier: a loaded machine
    // failing here is a true report of an unusable measurement.
    QVERIFY2(pbt::waitForPaintQuiescence(m_view->viewport(), kEventuallyMs),
             "open never reached paint quiescence — walk oracles would be timing-dependent");
    {
        QElapsedTimer t;
        t.start();
        while (oracle.isBlank(grabViewport()) && t.elapsed() < kEventuallyMs)
            QTest::qWait(kBlankPollMs);
    }

    // ---- Calibration preamble (anti-vacuity; runs EVERY walk) --------------
    // (a) a genuinely blank view — a widget filled with the exact surround
    // colour, through the same grab() pipeline — must be flagged blank;
    // (b) the settled page-1 frame must pass; (c) the oracle's surround
    // colour must equal the canvas colour the app itself pinned on the
    // view (PdfDocument::applyViewPalette pins QPalette::Dark to
    // documentSurroundColor(); if that derivation ever moves while this
    // harness's copy doesn't, blank frames would silently stop matching
    // and the walk would go vacuous — abort loudly instead). Any of the
    // three failing means the harness, not the app, is broken: abort
    // before walking.
    {
        QVERIFY2(m_view->palette().color(QPalette::Dark).rgb() == oracle.surround.rgb(),
                 "BROKEN HARNESS: oracle surround colour no longer matches the canvas "
                 "colour the app pins on the QPdfView (QPalette::Dark, "
                 "PdfDocument::applyViewPalette) — blank detection would be vacuous");
        QWidget blankView;
        QPalette pal = blankView.palette();
        pal.setColor(QPalette::Window, oracle.surround);
        blankView.setPalette(pal);
        blankView.setAutoFillBackground(true);
        blankView.resize(m_view->viewport()->size());
        const QImage blankFrame = blankView.grab().toImage();
        QVERIFY2(oracle.isBlank(blankFrame),
                 "BROKEN HARNESS: frame oracle failed to flag a known-blank surround-filled "
                 "view — walk results would be vacuous, aborting");
        const QImage page1 = grabViewport();
        if (oracle.isBlank(page1)) {
            saveEvidence(page1, m_seed, -1, QStringLiteral("calibration"));
            QFAIL("calibration: settled page-1 frame reads as blank. EITHER the app "
                  "genuinely failed to render page 1 (a real regression — check the evidence "
                  "PNG) OR the oracle's surround colour / sampling rect / settle is wrong. "
                  "Do not assume broken harness; triage the PNG first");
        }
    }

    // ---- Chrome geometry baseline (G10 spatial constancy oracle) -----------
    // Position+size of every visible control outside the viewport, keyed in
    // child order. Re-baselined only after the one action that legitimately
    // reshapes chrome (window resize).
    auto chromeSnapshot = [mw]() {
        QList<QRect> rects;
        if (QMenuBar *mb = mw->menuBar(); mb && mb->isVisible())
            rects.append(QRect(mb->mapTo(mw, QPoint(0, 0)), mb->size()));
        const auto toolbars = mw->findChildren<QToolBar *>();
        for (QToolBar *tb : toolbars) {
            if (tb->isVisible())
                rects.append(QRect(tb->mapTo(mw, QPoint(0, 0)), tb->size()));
        }
        return rects;
    };
    QList<QRect> chromeBaseline = chromeSnapshot();
    QVERIFY2(!chromeBaseline.isEmpty(),
             "BROKEN HARNESS: no visible menubar/toolbar found — the spatial-constancy "
             "oracle would check nothing (checked > 0 guard)");

    // ---- Thin model --------------------------------------------------------
    // Only what the strict oracle needs: pages whose CURRENT-geometry render
    // this walk has already confirmed non-blank. Geometry-changing actions
    // clear it — a cached frame at the old zoom/mode/size is not servable.
    QSet<int> visited;
    visited.insert(doc->currentPage());

    // ---- The walk ----------------------------------------------------------
    int nonBlankChecks = 0;
    for (int i = 0; i < m_steps.size(); ++i) {
        const Step &s = m_steps.at(i);

        // Hang forensics (see the PBT-GENERATED note above): mark the step
        // as it STARTS, unbuffered, so a livelocked run names the step it
        // died in even when ctest's TIMEOUT kills it.
        std::fprintf(stderr, "PBT-STEP %d %s\n", i, actionName(s.action));
        std::fflush(stderr);

        // Resolve nav targets against the live document BEFORE acting (the
        // same clamped calls MainWindow's Next/Previous Page actions make —
        // src/ui/MainWindow.cpp goToPage(nextPageIndex()) — so the walk
        // drives the app's own navigation path, not a parallel one).
        int navTarget = -1;
        bool executed = true;
        switch (s.action) {
        case Action::NextPage:
            navTarget = std::min(doc->pageCount() - 1, doc->nextPageIndex());
            break;
        case Action::PrevPage:
            navTarget = std::max(0, doc->previousPageIndex());
            break;
        case Action::GoToPage:
            navTarget = s.p1;
            break;
        case Action::ZoomOut:
            // Generator floor (see kMinWalkZoom): recorded as skipped, so
            // the trace stays a pure, replayable function of the seed.
            if (doc->zoomMode() == ZoomMode::Custom && doc->zoomFactor() <= kMinWalkZoom)
                executed = false;
            break;
        default:
            break;
        }

        const bool strict =
            navTarget >= 0 && visited.contains(navTarget) && !invalidatesRenderCache(s.action);
        if (invalidatesRenderCache(s.action) && executed)
            visited.clear();

        if (executed) {
            switch (s.action) {
            case Action::NextPage:
            case Action::PrevPage:
            case Action::GoToPage:
                doc->goToPage(navTarget);
                break;
            case Action::ViewModeToggle:
                // SinglePage <-> Continuous only: both live in the same
                // QPdfView. TwoPages swaps in a different widget
                // (TwoPageView) and gets its own grab surface in S1.
                doc->setViewMode(doc->viewMode() == ViewMode::Continuous ? ViewMode::SinglePage
                                                                         : ViewMode::Continuous);
                break;
            case Action::ZoomIn:
                doc->zoomIn();
                break;
            case Action::ZoomOut:
                doc->zoomOut();
                break;
            case Action::FitWidth:
                doc->zoomFitWidth();
                break;
            case Action::Resize:
                mw->resize(s.p1, s.p2);
                break;
            }
        }

        // Trace entry (before the oracles, so a failing step is included).
        {
            QJsonObject e;
            e.insert(QStringLiteral("i"), i);
            e.insert(QStringLiteral("a"), QLatin1String(actionName(s.action)));
            if (s.p1 >= 0)
                e.insert(QStringLiteral("p1"), s.p1);
            if (s.p2 >= 0)
                e.insert(QStringLiteral("p2"), s.p2);
            e.insert(QStringLiteral("executed"), executed);
            m_traceActions.append(e);
        }
        auto failMsg = [this, i, &s](const char *what) {
            qWarning().noquote() << "PBT-FAILURE"
                                 << QString::fromUtf8(
                                        QJsonDocument(traceJson()).toJson(QJsonDocument::Compact));
            return QStringLiteral("step %1 (%2): %3 — replay with TRAILER_PBT_SEED=%4")
                .arg(i)
                .arg(QLatin1String(actionName(s.action)))
                .arg(QLatin1String(what))
                .arg(m_seed);
        };

        // Oracle 1a — STRICT tier: a revisited page (same render geometry)
        // must be non-blank after ONE event-loop drain; the cache must
        // serve it. This is the immediate catch for the classic
        // async-renderer-dropped-the-request blank-page bug.
        if (strict && executed) {
            QApplication::processEvents();
            const QImage frame = grabViewport();
            if (oracle.isBlank(frame)) {
                saveEvidence(frame, m_seed, i, QStringLiteral("strict-blank"));
                QFAIL(qPrintable(failMsg("revisited page blank after one drain (strict tier)")));
            }
            ++nonBlankChecks;
        }

        // Oracle 1b — eventually-non-blank: content or an honest placeholder
        // within kEventuallyMs; pure surround forever is a failure.
        {
            QElapsedTimer t;
            t.start();
            QImage frame = grabViewport();
            while (oracle.isBlank(frame) && t.elapsed() < kEventuallyMs) {
                QTest::qWait(kBlankPollMs);
                frame = grabViewport();
            }
            if (oracle.isBlank(frame)) {
                saveEvidence(frame, m_seed, i, QStringLiteral("eventually-blank"));
                QFAIL(qPrintable(failMsg("viewport still blank after 5 s")));
            }
            ++nonBlankChecks;
        }

        // Settle before reading geometry or acting again — the ONE audited
        // settling primitive; no ad-hoc waits (PbtSettle.h). Failing loud
        // on non-quiescence also converts a livelock-shaped hang into a
        // diagnosable step failure instead of an opaque ctest TIMEOUT.
        QVERIFY2(pbt::waitForPaintQuiescence(m_view->viewport(), kEventuallyMs),
                 qPrintable(QStringLiteral("step %1 never reached paint quiescence — "
                                           "livelock-shaped; see the PBT-STEP trace above")
                                .arg(i)));

        // Oracle 2 — spatial constancy (G10): no control outside the
        // viewport moved. A resize legitimately reshapes chrome, so it
        // re-baselines instead of comparing.
        if (s.action == Action::Resize && executed) {
            chromeBaseline = chromeSnapshot();
        } else {
            const QList<QRect> now = chromeSnapshot();
            if (now != chromeBaseline) {
                saveEvidence(mw->grab().toImage(), m_seed, i, QStringLiteral("chrome-moved"));
                QFAIL(qPrintable(
                    failMsg("toolbar/menubar geometry changed (spatial constancy, G10)")));
            }
        }

        // Oracle 3 — page index in range.
        const int page = doc->currentPage();
        if (page < 0 || page >= doc->pageCount()) {
            saveEvidence(grabViewport(), m_seed, i, QStringLiteral("page-out-of-range"));
            QFAIL(qPrintable(failMsg("current page out of range")));
        }

        // Record the OBSERVED page in the trace: the determinism proof then
        // witnesses settled app state per step, not just generated inputs.
        {
            QJsonObject e = m_traceActions.last().toObject();
            e.insert(QStringLiteral("pageAfter"), page);
            m_traceActions.replace(m_traceActions.size() - 1, e);
        }

        // Model update: this page's frame is confirmed non-blank at the
        // current geometry.
        visited.insert(page);
    }

    // Anti-vacuity roll-up (the collectLayoutViolations `checked > 0`
    // idiom): the walk must actually have evaluated its oracles.
    QVERIFY2(nonBlankChecks >= kWalkSteps, "walk finished without evaluating its oracles");

    // Full trace on success too, so fixed-seed runs can be diffed
    // byte-for-byte (the 20-identical-runs determinism proof).
    qInfo().noquote() << "PBT-TRACE"
                      << QString::fromUtf8(
                             QJsonDocument(traceJson()).toJson(QJsonDocument::Compact));
}

int main(int argc, char **argv) {
    // Fresh fake HOME per PROCESS — and this binary runs exactly one walk
    // case per process, so this is the design's fresh-HOME-per-case rule:
    // recent-files, per-type defaults, and settings state cannot bleed
    // between cases (the pollution class UAT-VWR-101/102 documents).
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
    TestPbtWalkMin tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_pbt_walk_min.moc"
