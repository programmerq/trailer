// Focused widget test for MlProgressWidget (ADR 0002 Feature 1).
//
// Exercises the status-bar progress+cancel widget in isolation — no ML
// dependencies, no scheduler. Asserts the determinate / indeterminate
// state machine, the counter label, the elapsed-seconds reassurance, the
// terminal-message → idle return (with the hold set to 0 so there is no
// wall-clock wait), and the cancel signal.

#include "ui/MlProgressWidget.h"

#include <QSignalSpy>
#include <QToolButton>
#include <QtTest/QtTest>

using namespace trailer;

class TestMlProgressWidget : public QObject {
    Q_OBJECT
  private slots:
    void determinateCounterAndState();
    void indeterminateElapsedIsNotAStyleSwitch();
    void finishReturnsToIdle();
    void cancelButtonEmitsSignal();
};

void TestMlProgressWidget::determinateCounterAndState() {
    MlProgressWidget w;
    QCOMPARE(w.state(), MlProgressWidget::Idle);

    w.beginDeterminate(QStringLiteral("Recognising text"), 4);
    QCOMPARE(w.state(), MlProgressWidget::Running);
    QVERIFY(w.isDeterminate());
    QCOMPARE(w.total(), 4);
    QCOMPARE(w.value(), 0);
    QVERIFY(w.cancelVisible());
    QVERIFY(w.labelText().contains(QStringLiteral("0 / 4")));

    // Monotonic non-decreasing progress reaching total.
    int last = w.value();
    for (int done = 1; done <= 4; ++done) {
        w.setProgress(done);
        QVERIFY(w.value() >= last);
        last = w.value();
    }
    QCOMPARE(w.value(), 4);
    QVERIFY(w.labelText().contains(QStringLiteral("4 / 4")));

    // Over-shoot is clamped to total (bar can't misread past 100%).
    w.setProgress(99);
    QCOMPARE(w.value(), 4);
}

void TestMlProgressWidget::indeterminateElapsedIsNotAStyleSwitch() {
    MlProgressWidget w;
    w.beginIndeterminate(QStringLiteral("Removing background…"));
    QCOMPARE(w.state(), MlProgressWidget::Running);
    QVERIFY(!w.isDeterminate());
    QVERIFY(w.cancelVisible());

    // Under 10s: no reassurance suffix.
    w.setElapsedSeconds(5);
    QVERIFY(!w.labelText().contains(QStringLiteral("5s")));

    // >=10s: append " · Ns" but stay indeterminate (not a style switch).
    w.setElapsedSeconds(18);
    QVERIFY(w.labelText().contains(QStringLiteral("18s")));
    QVERIFY(!w.isDeterminate());

    // setProgress is a no-op while indeterminate.
    w.setProgress(3);
    QCOMPARE(w.value(), 0);
}

void TestMlProgressWidget::finishReturnsToIdle() {
    MlProgressWidget w;
    w.setTerminalHoldMs(0); // no wall-clock wait
    w.beginDeterminate(QStringLiteral("Recognising text"), 2);
    w.finishWithMessage(QStringLiteral("Text recognition complete"));
    // With a 0ms hold, finish rolls straight to Idle.
    QCOMPARE(w.state(), MlProgressWidget::Idle);
    QVERIFY(!w.cancelVisible());

    // With a non-zero hold, the terminal message lingers first.
    w.setTerminalHoldMs(20);
    w.beginIndeterminate(QStringLiteral("Removing background…"));
    w.finishWithMessage(QStringLiteral("Background removal complete"));
    QCOMPARE(w.state(), MlProgressWidget::Terminal);
    QCOMPARE(w.labelText(), QStringLiteral("Background removal complete"));
    QVERIFY(!w.cancelVisible());
    QTRY_COMPARE(w.state(), MlProgressWidget::Idle);
}

void TestMlProgressWidget::cancelButtonEmitsSignal() {
    MlProgressWidget w;
    w.beginDeterminate(QStringLiteral("Recognising text"), 3);
    QSignalSpy spy(&w, &MlProgressWidget::cancelRequested);
    auto *cancel = w.findChild<QToolButton *>();
    QVERIFY(cancel);
    QCOMPARE(cancel->accessibleName(), QStringLiteral("Cancel"));
    cancel->click();
    QCOMPARE(spy.count(), 1);
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestMlProgressWidget tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_ml_progress_widget.moc"
