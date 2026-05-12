// Unit tests for MarkupToolbar — the markup-side counterpart of
// test_form_toolbar. Focused on the visibility-gating behaviour that
// shrinks the toolbar on document types without a text layer. The
// existing test_uat_search_and_markup exercises the integrated
// toolbar via MainWindow; this file pins the contract of
// setToolVisible in isolation so a regression doesn't sneak through
// the bigger UAT's many other moving parts.

#include "ui/MarkupToolbar.h"

#include <QAction>
#include <QMetaType>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace trailer;

Q_DECLARE_METATYPE(trailer::AnnotationTool)

class TestMarkupToolbar : public QObject {
    Q_OBJECT
private slots:
    void hidingATextAwareToolHidesItsAction();
    void hidingTheActiveToolFallsBackToSelect();
    void hidingAllTextAwareToolsHidesPrecedingSeparator();
    void reShowingATextAwareToolBringsBackTheSeparator();
};

namespace {

QAction* find(MarkupToolbar* bar, const QString& text) {
    for (QAction* a : bar->actions()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}

}  // namespace

void TestMarkupToolbar::hidingATextAwareToolHidesItsAction() {
    MarkupToolbar bar;
    QAction* underline = find(&bar, QStringLiteral("Underline"));
    QVERIFY(underline);
    QVERIFY(underline->isVisible());

    bar.setToolVisible(AnnotationTool::Underline, false);
    QVERIFY(!underline->isVisible());

    bar.setToolVisible(AnnotationTool::Underline, true);
    QVERIFY(underline->isVisible());
}

void TestMarkupToolbar::hidingTheActiveToolFallsBackToSelect() {
    MarkupToolbar bar;
    QAction* highlight = find(&bar, QStringLiteral("Highlight"));
    QVERIFY(highlight);

    // Arm Highlight as the active tool.
    highlight->trigger();
    QCOMPARE(bar.activeTool(), AnnotationTool::Highlight);

    // Hide it — the toolbar must un-arm and revert to Select so the
    // overlay isn't stuck consuming click-drags for an unreachable
    // tool.
    bar.setToolVisible(AnnotationTool::Highlight, false);
    QCOMPARE(bar.activeTool(), AnnotationTool::Select);
    QAction* selectAction = find(&bar, QStringLiteral("Select"));
    QVERIFY(selectAction);
    QVERIFY(selectAction->isChecked());
}

void TestMarkupToolbar::hidingAllTextAwareToolsHidesPrecedingSeparator() {
    MarkupToolbar bar;

    // Find the separator that sits immediately before the text-aware
    // group. The toolbar's action list interleaves real actions and
    // separators in declaration order; the one preceding Highlight is
    // the group separator.
    QAction* groupSep = nullptr;
    QAction* prevSep = nullptr;
    for (QAction* a : bar.actions()) {
        if (a->isSeparator()) {
            prevSep = a;
            continue;
        }
        if (a->text() == QStringLiteral("Highlight")) {
            groupSep = prevSep;
            break;
        }
    }
    QVERIFY2(groupSep,
             "couldn't locate the separator preceding the text-aware "
             "group — toolbar layout changed?");
    QVERIFY(groupSep->isVisible());

    bar.setToolVisible(AnnotationTool::Highlight, false);
    QVERIFY2(groupSep->isVisible(),
             "separator hidden too eagerly — still two tools visible.");
    bar.setToolVisible(AnnotationTool::Underline, false);
    QVERIFY2(groupSep->isVisible(),
             "separator hidden too eagerly — one tool still visible.");
    bar.setToolVisible(AnnotationTool::StrikeOut, false);
    QVERIFY2(!groupSep->isVisible(),
             "separator should hide once every text-aware tool is "
             "hidden — leaving two adjacent dividers around nothing.");
}

void TestMarkupToolbar::reShowingATextAwareToolBringsBackTheSeparator() {
    MarkupToolbar bar;
    QAction* groupSep = nullptr;
    QAction* prevSep = nullptr;
    for (QAction* a : bar.actions()) {
        if (a->isSeparator()) {
            prevSep = a;
            continue;
        }
        if (a->text() == QStringLiteral("Highlight")) {
            groupSep = prevSep;
            break;
        }
    }
    QVERIFY(groupSep);

    bar.setToolVisible(AnnotationTool::Highlight, false);
    bar.setToolVisible(AnnotationTool::Underline, false);
    bar.setToolVisible(AnnotationTool::StrikeOut, false);
    QVERIFY(!groupSep->isVisible());

    // Re-showing any single tool brings the separator back so the
    // group has its leading divider again.
    bar.setToolVisible(AnnotationTool::Underline, true);
    QVERIFY(groupSep->isVisible());
}

QTEST_MAIN(TestMarkupToolbar)
#include "test_markup_toolbar.moc"
