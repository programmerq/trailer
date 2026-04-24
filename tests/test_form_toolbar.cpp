// Unit tests for FormToolbar — the toolbar introduced in Phase 5 for
// "fill in a form that doesn't have proper AcroForm fields" (design
// doc §6.4). We don't test the rendering; we test that clicking each
// tool action emits `toolChanged` with the right AnnotationTool and
// the expected preset text (✓ / ✗ glyph or empty), and that the
// AutoFill / Sign Here buttons emit their own signals.

#include "ui/FormToolbar.h"

#include <QAction>
#include <QMetaType>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace trailer;

// QSignalSpy stores signal arguments as QVariants, so the enum type
// has to be registered with Qt's meta-type system for take/at lookups
// to work.
Q_DECLARE_METATYPE(trailer::AnnotationTool)

class TestFormToolbar : public QObject {
    Q_OBJECT
private slots:
    void textBoxEmitsTextToolWithEmptyPreset();
    void checkmarkEmitsTextToolWithCheckGlyph();
    void xMarkEmitsTextToolWithCrossGlyph();
    void autoFillSignalFiresOnce();
    void signHereSignalFiresOnce();
    void toolsAreMutuallyExclusive();
};

namespace {

// Find a QAction on the toolbar by its visible text. Returns nullptr
// if nothing matches (meaning the test's assumption about the toolbar
// layout is wrong — fail loudly rather than pass silently).
QAction* findAction(FormToolbar* bar, const QString& text) {
    for (QAction* a : bar->actions()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}

}  // namespace

void TestFormToolbar::textBoxEmitsTextToolWithEmptyPreset() {
    FormToolbar bar;
    QSignalSpy spy(&bar, &FormToolbar::toolChanged);

    QAction* a = findAction(&bar, QStringLiteral("Text Box"));
    QVERIFY(a);
    a->trigger();

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).value<AnnotationTool>(), AnnotationTool::Text);
    QCOMPARE(args.at(1).toString(), QString());
    QCOMPARE(bar.activeTool(), AnnotationTool::Text);
    QVERIFY(bar.pendingText().isEmpty());
}

void TestFormToolbar::checkmarkEmitsTextToolWithCheckGlyph() {
    FormToolbar bar;
    QSignalSpy spy(&bar, &FormToolbar::toolChanged);

    QAction* a = findAction(&bar, QStringLiteral("Checkmark"));
    QVERIFY(a);
    a->trigger();

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).value<AnnotationTool>(), AnnotationTool::Text);
    QCOMPARE(args.at(1).toString(), QStringLiteral(u"\u2713"));
    QCOMPARE(bar.pendingText(), QStringLiteral(u"\u2713"));
}

void TestFormToolbar::xMarkEmitsTextToolWithCrossGlyph() {
    FormToolbar bar;
    QSignalSpy spy(&bar, &FormToolbar::toolChanged);

    QAction* a = findAction(&bar, QStringLiteral("X Mark"));
    QVERIFY(a);
    a->trigger();

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).value<AnnotationTool>(), AnnotationTool::Text);
    QCOMPARE(args.at(1).toString(), QStringLiteral(u"\u2717"));
    QCOMPARE(bar.pendingText(), QStringLiteral(u"\u2717"));
}

void TestFormToolbar::autoFillSignalFiresOnce() {
    FormToolbar bar;
    QSignalSpy spy(&bar, &FormToolbar::autoFillRequested);
    QAction* a = findAction(&bar, QStringLiteral("AutoFill"));
    QVERIFY(a);
    a->trigger();
    QCOMPARE(spy.count(), 1);
}

void TestFormToolbar::signHereSignalFiresOnce() {
    FormToolbar bar;
    QSignalSpy spy(&bar, &FormToolbar::signHereRequested);
    QAction* a = findAction(&bar, QStringLiteral("Sign Here"));
    QVERIFY(a);
    a->trigger();
    QCOMPARE(spy.count(), 1);
}

void TestFormToolbar::toolsAreMutuallyExclusive() {
    FormToolbar bar;

    QAction* checkAction = findAction(&bar, QStringLiteral("Checkmark"));
    QAction* textAction = findAction(&bar, QStringLiteral("Text Box"));
    QVERIFY(checkAction);
    QVERIFY(textAction);

    checkAction->trigger();
    QVERIFY(checkAction->isChecked());
    QVERIFY(!textAction->isChecked());

    textAction->trigger();
    QVERIFY(textAction->isChecked());
    QVERIFY(!checkAction->isChecked());
    // Preset must reset when switching back to plain Text Box so the
    // overlay opens its input dialog instead of silently dropping ✓.
    QVERIFY(bar.pendingText().isEmpty());
}

QTEST_MAIN(TestFormToolbar)
#include "test_form_toolbar.moc"
