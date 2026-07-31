#include "platform/DockRecents.h"

#include <QObject>
#include <QStringList>
#include <QtTest/QtTest>

using namespace trailer;

// DockRecents.mm's Cocoa calls are unverifiable in this offscreen/Linux
// harness (see the PR's "what could/couldn't be verified" note), but the
// cross-platform contract it exposes IS testable everywhere: the constant
// the rest of the app builds its capped list against, and that the
// off-macOS stub (linked here on Linux/Windows) is inert rather than
// crashing or asserting on odd input.
class TestDockRecents : public QObject {
    Q_OBJECT
  private slots:
    void limitConstantIsTen();
    void syncIsSafeWithEmptyList();
    void syncIsSafeWithOddInput();
};

void TestDockRecents::limitConstantIsTen() {
    // The owner's literal ask ("the 10 most recent files") — see
    // DockRecents.h's comment on why this isn't an independently
    // hand-tuned constant.
    QCOMPARE(DockRecents::kMaxSystemRecents, 10);
}

void TestDockRecents::syncIsSafeWithEmptyList() {
    // Off macOS this hits DockRecents_stub.cpp; on macOS it would clear
    // the system list and note nothing back in. Either way, must not
    // crash on the "user has no recents yet" case.
    DockRecents::syncSystemRecents({});
}

void TestDockRecents::syncIsSafeWithOddInput() {
    // Empty strings and a plausible-but-nonexistent path: the real
    // caller (Application::refreshDockRecents) always pre-filters via
    // RecentFiles::existingEntries, but this function's own contract
    // (DockRecents.h) only promises safety, not re-validation — confirm
    // it doesn't crash even when handed input a well-behaved caller
    // wouldn't produce.
    DockRecents::syncSystemRecents({QString(), QStringLiteral("/nonexistent/path.pdf")});
}

QTEST_MAIN(TestDockRecents)
#include "test_dock_recents.moc"
