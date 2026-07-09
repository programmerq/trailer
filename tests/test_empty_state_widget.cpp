// Unit tests for EmptyStateWidget — the empty-state / first-run welcome
// surface. Verifies the "Open File…" button, the openRequested signal,
// drag-highlight state on drag-enter/leave, and filesDropped on a drop
// carrying local file URLs.

#include "ui/EmptyStateWidget.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QUrl>
#include <QtTest/QtTest>

using namespace trailer;

class TestEmptyStateWidget : public QObject {
    Q_OBJECT
  private slots:
    void openButtonExistsAndIsEnabled();
    void clickingOpenButtonEmitsOpenRequested();
    void dropWithFileUrlsEmitsFilesDropped();
    void dragEnterSetsHighlightAndLeaveClears();
};

void TestEmptyStateWidget::openButtonExistsAndIsEnabled() {
    EmptyStateWidget widget;
    auto *button = widget.findChild<QPushButton *>();
    QVERIFY2(button, "EmptyStateWidget should contain an Open File button");
    QVERIFY2(button->isEnabled(), "The Open File button must be enabled");
    QVERIFY2(button->text().contains(QStringLiteral("Open")),
             "The button should be labelled with an Open action");
}

void TestEmptyStateWidget::clickingOpenButtonEmitsOpenRequested() {
    EmptyStateWidget widget;
    auto *button = widget.findChild<QPushButton *>();
    QVERIFY(button);

    QSignalSpy spy(&widget, &EmptyStateWidget::openRequested);
    QVERIFY(spy.isValid());
    button->click();
    QCOMPARE(spy.count(), 1);
}

void TestEmptyStateWidget::dropWithFileUrlsEmitsFilesDropped() {
    EmptyStateWidget widget;
    widget.resize(400, 300);

    QSignalSpy spy(&widget, &EmptyStateWidget::filesDropped);
    QVERIFY(spy.isValid());

    auto *mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(QStringLiteral("/tmp/one.pdf")),
                   QUrl::fromLocalFile(QStringLiteral("/tmp/two.png"))});

    // A real drop is preceded by an accepted drag-enter in the same
    // drag session; send one so the widget is registered as the drop
    // target before the drop lands.
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &enter);

    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &drop);

    QCOMPARE(spy.count(), 1);
    const QStringList paths = spy.takeFirst().at(0).toStringList();
    QCOMPARE(paths.size(), 2);
    QCOMPARE(paths.at(0), QStringLiteral("/tmp/one.pdf"));
    QCOMPARE(paths.at(1), QStringLiteral("/tmp/two.png"));
    QVERIFY2(!widget.isDragHighlighted(), "Highlight must clear after a drop");

    delete mime;
}

void TestEmptyStateWidget::dragEnterSetsHighlightAndLeaveClears() {
    EmptyStateWidget widget;
    widget.resize(400, 300);
    QVERIFY(!widget.isDragHighlighted());

    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QStringLiteral("/tmp/one.pdf"))});

    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &enter);
    QVERIFY2(enter.isAccepted(), "Drag with file URLs should be accepted");
    QVERIFY2(widget.isDragHighlighted(), "Drag-enter with URLs should set the highlight");

    QDragLeaveEvent leave;
    QApplication::sendEvent(&widget, &leave);
    QVERIFY2(!widget.isDragHighlighted(), "Drag-leave should clear the highlight");
}

QTEST_MAIN(TestEmptyStateWidget)
#include "test_empty_state_widget.moc"
