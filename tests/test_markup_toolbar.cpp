// Unit tests for MarkupToolbar — the markup-side counterpart of
// test_form_toolbar. Focused on the enabled-gating behaviour that
// disables tools on document types without a text layer. The existing
// test_uat_search_and_markup exercises the integrated toolbar via
// MainWindow; this file pins the contract of setToolEnabled in
// isolation so a regression doesn't sneak through the bigger UAT's
// many other moving parts.
//
// G10/SC-CRIT-2 (docs/decision-records/2026-08-01-markup-toolbar-disable-not-hide.md):
// setToolEnabled disables a tool in place rather than hiding it, so every
// action keeps its on-screen slot in the shared toolbar row regardless of
// document capability — hiding used to collapse the action's slot,
// shifting every action after it. These tests pin the disable-in-place
// contract (isVisible() never changes) that replaced the old
// hide/show + separator-visibility contract.

#include "ui/MarkupToolbar.h"

#include <QAction>
#include <QLayout>
#include <QMetaType>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace trailer;

Q_DECLARE_METATYPE(trailer::AnnotationTool)

class TestMarkupToolbar : public QObject {
    Q_OBJECT
private slots:
    void disablingATextAwareToolKeepsItVisibleButDisabled();
    void disablingTheActiveToolFallsBackToSelect();
    void disabledToolCarriesTheGivenTooltip();
    void reEnablingATextAwareToolRestoresItsPlainLabelTooltip();
    void toolPositionsNeverMoveAcrossEnableDisableToggles();
    void disabledTooltipUpdatesWhenReasonChangesWhileStayingDisabled();
};

namespace {

QAction* find(MarkupToolbar* bar, const QString& text) {
    for (QAction* a : bar->actions()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}

}  // namespace

void TestMarkupToolbar::disablingATextAwareToolKeepsItVisibleButDisabled() {
    MarkupToolbar bar;
    QAction* underline = find(&bar, QStringLiteral("Underline"));
    QVERIFY(underline);
    QVERIFY(underline->isVisible());
    QVERIFY(underline->isEnabled());

    bar.setToolEnabled(AnnotationTool::Underline, false, QStringLiteral("why"));
    QVERIFY2(underline->isVisible(),
             "G10: a disabled tool stays in the toolbar's layout, not hidden");
    QVERIFY(!underline->isEnabled());

    bar.setToolEnabled(AnnotationTool::Underline, true);
    QVERIFY(underline->isVisible());
    QVERIFY(underline->isEnabled());
}

void TestMarkupToolbar::disablingTheActiveToolFallsBackToSelect() {
    MarkupToolbar bar;
    QAction* highlight = find(&bar, QStringLiteral("Highlight"));
    QVERIFY(highlight);

    // Arm Highlight as the active tool.
    highlight->trigger();
    QCOMPARE(bar.activeTool(), AnnotationTool::Highlight);

    // Disable it — the toolbar must un-arm and revert to Select so the
    // overlay isn't stuck consuming click-drags for a tool the user can
    // no longer engage.
    bar.setToolEnabled(AnnotationTool::Highlight, false, QStringLiteral("why"));
    QCOMPARE(bar.activeTool(), AnnotationTool::Select);
    QAction* selectAction = find(&bar, QStringLiteral("Select"));
    QVERIFY(selectAction);
    QVERIFY(selectAction->isChecked());
}

void TestMarkupToolbar::disabledToolCarriesTheGivenTooltip() {
    MarkupToolbar bar;
    QAction* strike = find(&bar, QStringLiteral("Strikeout"));
    QVERIFY(strike);

    // G3: a disabled control states why it can't act right now.
    const QString why = QStringLiteral("Available once this page has recognisable text");
    bar.setToolEnabled(AnnotationTool::StrikeOut, false, why);
    QCOMPARE(strike->toolTip(), why);
}

void TestMarkupToolbar::reEnablingATextAwareToolRestoresItsPlainLabelTooltip() {
    MarkupToolbar bar;
    QAction* underline = find(&bar, QStringLiteral("Underline"));
    QVERIFY(underline);
    const QString plainTooltip = underline->toolTip();
    QCOMPARE(plainTooltip, QStringLiteral("Underline"));

    bar.setToolEnabled(AnnotationTool::Underline, false, QStringLiteral("why"));
    QVERIFY(underline->toolTip() != plainTooltip);

    bar.setToolEnabled(AnnotationTool::Underline, true);
    QCOMPARE(underline->toolTip(), plainTooltip);
}

// The direct regression guard for SC-CRIT-2: every OTHER action's
// on-screen position is unaffected by disabling/re-enabling the
// text-aware trio and the SAM pair, in any combination. Positions are
// compared via QToolBar::widgetForAction()->pos(), the same style of
// geometry assertion ADR 0007's toolbar-anchoring tests use.
void TestMarkupToolbar::toolPositionsNeverMoveAcrossEnableDisableToggles() {
    MarkupToolbar bar;
    bar.resize(900, 40);
    bar.show();
    // Best-effort readiness signal, NOT a correctness gate for this test:
    // Wine's offscreen QPA plugin (2026-08-01, PR #141 CI failure under
    // "Windows cross-build + Wine unit tests") does not reliably fire a
    // window-exposed event within the default 5s timeout, even though the
    // toolbar's own layout is already fully computed by the time show()
    // returns. Hard-failing on the wait would fail a platform quirk
    // unrelated to the invariant this test protects: SC-CRIT-2 is a
    // DIFFERENTIAL property (a widget's position is unchanged across state
    // transitions), which needs a settled layout, not a confirmed
    // window-manager paint. So: wait, but don't assert on the result, and
    // force layout settlement explicitly below regardless of whether the
    // wait timed out.
    (void)QTest::qWaitForWindowExposed(&bar);
    bar.layout()->activate();
    QApplication::processEvents();

    QAction* redact = find(&bar, QStringLiteral("Redact"));
    QAction* instantAlpha = find(&bar, QStringLiteral("Instant Alpha"));
    QAction* smartLasso = find(&bar, QStringLiteral("Smart Lasso"));
    QVERIFY(redact && instantAlpha && smartLasso);

    QWidget* redactWidget = bar.widgetForAction(redact);
    QWidget* instantAlphaWidget = bar.widgetForAction(instantAlpha);
    QWidget* smartLassoWidget = bar.widgetForAction(smartLasso);
    QVERIFY(redactWidget && instantAlphaWidget && smartLassoWidget);

    const QPoint redactBaseline = redactWidget->pos();
    const QPoint instantAlphaBaseline = instantAlphaWidget->pos();
    const QPoint smartLassoBaseline = smartLassoWidget->pos();

    auto assertUnchanged = [&](const char *step) {
        QVERIFY2(redactWidget->pos() == redactBaseline, step);
        QVERIFY2(instantAlphaWidget->pos() == instantAlphaBaseline, step);
        QVERIFY2(smartLassoWidget->pos() == smartLassoBaseline, step);
    };

    bar.setToolEnabled(AnnotationTool::Underline, false, QStringLiteral("why"));
    bar.setToolEnabled(AnnotationTool::Highlight, false, QStringLiteral("why"));
    bar.setToolEnabled(AnnotationTool::StrikeOut, false, QStringLiteral("why"));
    assertUnchanged("text-aware trio disabled");

    bar.setToolEnabled(AnnotationTool::InstantAlpha, false, QStringLiteral("why"));
    bar.setToolEnabled(AnnotationTool::SmartLasso, false, QStringLiteral("why"));
    assertUnchanged("SAM pair disabled (self-disabling, but Redact must still hold)");

    bar.setToolEnabled(AnnotationTool::Underline, true);
    bar.setToolEnabled(AnnotationTool::Highlight, true);
    bar.setToolEnabled(AnnotationTool::StrikeOut, true);
    assertUnchanged("text-aware trio re-enabled");
}

// Correctness-skeptic finding (review-before-push self-review): two
// different documents can both leave a tool disabled for DIFFERENT
// reasons — e.g. Instant Alpha disabled on a PDF ("images only") vs.
// disabled on an image with a blocked download policy ("Never
// Download..."). setToolEnabled() must refresh the tooltip on every call,
// not just on an enabled/disabled state TRANSITION, or the first reason's
// tooltip goes stale on the second still-disabled document.
void TestMarkupToolbar::disabledTooltipUpdatesWhenReasonChangesWhileStayingDisabled() {
    MarkupToolbar bar;
    QAction* instantAlpha = find(&bar, QStringLiteral("Instant Alpha"));
    QVERIFY(instantAlpha);

    const QString imagesOnly = QStringLiteral("Available for images only");
    const QString policyBlocked =
        QStringLiteral("This model is set to Never Download. "
                        "Open Tools → Manage ML Models… to allow it.");

    // Disabled on a PDF (format can't support it).
    bar.setToolEnabled(AnnotationTool::InstantAlpha, false, imagesOnly);
    QCOMPARE(instantAlpha->toolTip(), imagesOnly);
    QVERIFY(!instantAlpha->isEnabled());

    // Switch to an image with a blocked model policy — STILL disabled
    // (isEnabled() does not change: false -> false), but for a different
    // reason. The tooltip must follow.
    bar.setToolEnabled(AnnotationTool::InstantAlpha, false, policyBlocked);
    QVERIFY2(instantAlpha->toolTip() == policyBlocked,
             "disabled reason must refresh even when isEnabled() itself is unchanged");
    QVERIFY(!instantAlpha->isEnabled());
}

QTEST_MAIN(TestMarkupToolbar)
#include "test_markup_toolbar.moc"
