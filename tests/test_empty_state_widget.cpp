// Unit tests for EmptyStateWidget — the empty-state / first-run welcome
// surface. Verifies the "Open File…" button, the openRequested signal,
// drag-highlight state on drag-enter/leave, filesDropped on a drop
// carrying local file URLs, and the inline Open Recent list
// (populate / cap / hide-when-empty / click-emits-openRecentRequested).

#include "recent/RecentFiles.h"
#include "ui/EmptyStateWidget.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QList>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QUrl>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

// Build a synthetic recent-file list of `count` entries, most-recent
// first, with predictable display names and paths.
QList<RecentEntry> makeRecent(int count) {
    QList<RecentEntry> entries;
    for (int i = 0; i < count; ++i) {
        RecentEntry e;
        e.path = QStringLiteral("/docs/file%1.pdf").arg(i);
        e.displayName = QStringLiteral("file%1.pdf").arg(i);
        entries.append(e);
    }
    return entries;
}

} // namespace

class TestEmptyStateWidget : public QObject {
    Q_OBJECT
  private slots:
    void openButtonExistsAndIsEnabled();
    void clickingOpenButtonEmitsOpenRequested();
    void dropWithFileUrlsEmitsFilesDropped();
    void dragEnterSetsHighlightAndLeaveClears();
    void recentSectionHiddenByDefaultAndWhenEmpty();
    void recentEntriesPopulateAndShowSection();
    void recentListCapsAtMaxShown();
    void clickingRecentEntryEmitsPathAndSetsToolTip();
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

// A freshly-constructed widget, and one handed an empty list, hides the
// recent section entirely — no empty placeholder (no-lying-controls /
// no-empty-affordance philosophy).
void TestEmptyStateWidget::recentSectionHiddenByDefaultAndWhenEmpty() {
    EmptyStateWidget widget;
    // isVisibleTo is used so the check is valid whether or not the
    // top-level widget itself has been shown.
    QVERIFY2(!widget.isRecentSectionVisible(),
             "The recent section must be hidden before any entries are set");
    QCOMPARE(widget.recentEntryCount(), 0);

    widget.setRecentEntries(makeRecent(3));
    QVERIFY2(widget.isRecentSectionVisible(), "Setting entries must show the recent section");

    widget.setRecentEntries({});
    QVERIFY2(!widget.isRecentSectionVisible(),
             "Clearing the recent list must hide the section again (no empty placeholder)");
    QCOMPARE(widget.recentEntryCount(), 0);
}

// Setting a non-empty list renders one clickable entry per file and
// shows the section.
void TestEmptyStateWidget::recentEntriesPopulateAndShowSection() {
    EmptyStateWidget widget;
    widget.setRecentEntries(makeRecent(3));

    QVERIFY(widget.isRecentSectionVisible());
    QCOMPARE(widget.recentEntryCount(), 3);
}

// The list never renders more than kMaxRecentShown entries even when the
// model holds more.
void TestEmptyStateWidget::recentListCapsAtMaxShown() {
    EmptyStateWidget widget;
    widget.setRecentEntries(makeRecent(EmptyStateWidget::kMaxRecentShown + 5));
    QCOMPARE(widget.recentEntryCount(), EmptyStateWidget::kMaxRecentShown);
}

// Clicking a recent entry emits openRecentRequested carrying that
// entry's path, and the entry button exposes the full path as a tooltip.
void TestEmptyStateWidget::clickingRecentEntryEmitsPathAndSetsToolTip() {
    EmptyStateWidget widget;
    widget.setRecentEntries(makeRecent(3));

    // The recent-entry buttons carry the path as their tooltip; the
    // "Open File…" button does not, so filter on that to find them.
    QList<QPushButton *> recentButtons;
    for (QPushButton *b : widget.findChildren<QPushButton *>()) {
        if (b->toolTip().startsWith(QStringLiteral("/docs/")))
            recentButtons.append(b);
    }
    QCOMPARE(recentButtons.size(), 3);

    QSignalSpy spy(&widget, &EmptyStateWidget::openRecentRequested);
    QVERIFY(spy.isValid());

    // Click the first entry (file0.pdf → /docs/file0.pdf).
    QPushButton *first = nullptr;
    for (QPushButton *b : recentButtons) {
        if (b->text() == QStringLiteral("file0.pdf"))
            first = b;
    }
    QVERIFY2(first, "Expected a recent entry labelled with its display name");
    QCOMPARE(first->toolTip(), QStringLiteral("/docs/file0.pdf"));

    first->click();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("/docs/file0.pdf"));
}

QTEST_MAIN(TestEmptyStateWidget)
#include "test_empty_state_widget.moc"
