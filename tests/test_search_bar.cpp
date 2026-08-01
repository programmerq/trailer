// Unit test for SearchBar — the counter/nav-button layout invariant this
// file exists to protect.
//
// G10/SC-MOD-1 (spatial constancy, AGENTS.md;
// docs/audit-2026-07-31-g10-deference.md): SearchBar::setMatchCounter()
// used to hide()/show() the match-count label as the count crossed zero,
// which collapsed/restored its slot in the shared QHBoxLayout and shifted
// Prev/Next/Close sideways. This is a bare-widget test (no MainWindow
// embedding, mirroring test_markup_toolbar.cpp's pattern) so the
// invariant is pinned against the WIDGET's own layout behaviour, not
// against an incidental width constraint MainWindow happens to apply
// (setMaximumWidth(360) on the embedded instance) that could otherwise
// mask a real defect in a narrower or differently-constrained embedding.

#include "ui/SearchBar.h"

#include <QDir>
#include <QLayout>
#include <QToolButton>
#include <QtTest/QtTest>

using namespace trailer;

class TestSearchBar : public QObject {
    Q_OBJECT
  private slots:
    void navButtonsStayPutAsMatchCountCrossesZero();
};

namespace {

QToolButton *findByTooltip(SearchBar *bar, const QString &tip) {
    const auto buttons = bar->findChildren<QToolButton *>();
    for (QToolButton *b : buttons) {
        if (b->toolTip() == tip)
            return b;
    }
    return nullptr;
}

// G2 evidence, opt-in via TRAILER_UAT_EVIDENCE_DIR (mirrors
// test_uat_ml_affordances.cpp's saveEvidence helper) — off by default so a
// normal ctest run never writes files outside the build tree.
void saveEvidence(QWidget *w, const QString &fileName) {
    const QString dir = qEnvironmentVariable("TRAILER_UAT_EVIDENCE_DIR");
    if (dir.isEmpty() || !w)
        return;
    QDir().mkpath(dir);
    w->grab().save(QDir(dir).filePath(fileName), "PNG");
}

} // namespace

void TestSearchBar::navButtonsStayPutAsMatchCountCrossesZero() {
    SearchBar bar;
    bar.show();
    // Best-effort readiness signal, NOT a correctness gate for this test:
    // Wine's offscreen QPA plugin (2026-08-01, PR #141 CI failure under
    // "Windows cross-build + Wine unit tests") does not reliably fire a
    // window-exposed event within the default 5s timeout, even though the
    // bar's own layout is already computable by the time show() returns.
    // Hard-failing on the wait would fail a platform quirk unrelated to the
    // invariant this test protects: SC-MOD-1 is a DIFFERENTIAL property (a
    // button's position is unchanged as the match count crosses zero),
    // which needs a settled layout, not a confirmed window-manager paint.
    // So: wait, but don't assert on the result -- settle() below (called
    // after every state change, including the first) forces layout
    // settlement explicitly regardless of whether the wait timed out.
    (void)QTest::qWaitForWindowExposed(&bar);

    QToolButton *prev = findByTooltip(&bar, QStringLiteral("Previous match"));
    QToolButton *next = findByTooltip(&bar, QStringLiteral("Next match"));
    QToolButton *close = findByTooltip(&bar, QStringLiteral("Close search"));
    QVERIFY(prev && next && close);

    // Let the top-level widget settle to its OWN natural (unconstrained)
    // size after each content change, rather than pinning it to an
    // explicit resize() — a fixed outer width would let m_input's stretch
    // factor silently absorb the counter's width change and mask the
    // defect this test exists to catch (matching how the widget behaves
    // when nothing external constrains its width).
    auto settle = [&bar]() {
        bar.layout()->activate();
        bar.adjustSize();
        QApplication::processEvents();
    };

    bar.setMatchCounter(0, 0);
    settle();
    const QPoint prevBaseline = prev->pos();
    const QPoint nextBaseline = next->pos();
    const QPoint closeBaseline = close->pos();
    saveEvidence(&bar, QStringLiteral("searchbar_no_query.png"));

    bar.setMatchCounter(1, 3);
    settle();
    saveEvidence(&bar, QStringLiteral("searchbar_with_matches.png"));
    QCOMPARE(prev->pos(), prevBaseline);
    QCOMPARE(next->pos(), nextBaseline);
    QCOMPARE(close->pos(), closeBaseline);

    bar.setMatchCounter(0, 0);
    settle();
    QCOMPARE(prev->pos(), prevBaseline);
    QCOMPARE(next->pos(), nextBaseline);
    QCOMPARE(close->pos(), closeBaseline);
}

QTEST_MAIN(TestSearchBar)
#include "test_search_bar.moc"
