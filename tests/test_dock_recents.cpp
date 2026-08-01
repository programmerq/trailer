#include "platform/DockRecents.h"

#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

// DockRecents.mm's Cocoa calls are unverifiable in an offscreen/Linux
// harness (see the PR's "what could/couldn't be verified" note), but the
// cross-platform contract it exposes IS testable everywhere: the constant
// the rest of the app builds its capped list against, and that the
// off-macOS stub (linked here on Linux/Windows) is inert rather than
// crashing or asserting on odd input.
//
// On a REAL Mac (Q_OS_MACOS), DockRecents.mm's calls hit the actual,
// persistent NSDocumentController system Recent Documents store for this
// process — the same store a real Trailer.app on that machine uses. Every
// test below that calls syncSystemRecents() on macOS wraps itself in
// SystemRecentsRestoreGuard so it restores whatever was there before the
// test ran, REGARDLESS of the test's outcome (a QCOMPARE/QVERIFY failure
// expands to a `return`, but a local RAII object's destructor still runs
// then) — a plain `ctest` run must never permanently alter a developer's
// or a shared self-hosted CI runner's real Dock/system recents state.
#ifdef Q_OS_MACOS
namespace {
class SystemRecentsRestoreGuard {
  public:
    SystemRecentsRestoreGuard() : m_snapshot(DockRecents::systemRecentsForTesting()) {}
    ~SystemRecentsRestoreGuard() { DockRecents::syncSystemRecents(m_snapshot); }

  private:
    QStringList m_snapshot;
};
} // namespace
#endif

class TestDockRecents : public QObject {
    Q_OBJECT
  private slots:
    void limitConstantIsTen();
    void syncIsSafeWithEmptyList();
    void syncIsSafeWithOddInput();
#ifdef Q_OS_MACOS
    void macosSystemRecentsRoundTrip();
#endif
};

void TestDockRecents::limitConstantIsTen() {
    // The owner's literal ask ("the 10 most recent files") — see
    // DockRecents.h's comment on why this isn't an independently
    // hand-tuned constant.
    QCOMPARE(DockRecents::kMaxSystemRecents, 10);
}

void TestDockRecents::syncIsSafeWithEmptyList() {
#ifdef Q_OS_MACOS
    SystemRecentsRestoreGuard restoreOnExit;
#endif
    // Off macOS this hits DockRecents_stub.cpp; on macOS it clears the
    // REAL system list (restored above). Either way, must not crash on
    // the "user has no recents yet" case.
    DockRecents::syncSystemRecents({});
}

void TestDockRecents::syncIsSafeWithOddInput() {
#ifdef Q_OS_MACOS
    SystemRecentsRestoreGuard restoreOnExit;
#endif
    // Empty strings and a plausible-but-nonexistent path: the real
    // caller (Application::refreshDockRecents) always pre-filters via
    // RecentFiles::existingEntries, but this function's own contract
    // (DockRecents.h) only promises safety, not re-validation — confirm
    // it doesn't crash even when handed input a well-behaved caller
    // wouldn't produce.
    DockRecents::syncSystemRecents({QString(), QStringLiteral("/nonexistent/path.pdf")});
}

#ifdef Q_OS_MACOS
// Real-Mac-only mechanical proof of Part 1's "works when Trailer isn't
// running" claim: writes to the actual NSDocumentController system store
// via syncSystemRecents(), then reads it back via the real
// recentDocumentURLs accessor — the exact API Launch Services/the Dock
// consult with Trailer's process dead. This does NOT (and cannot, from a
// headless ctest binary) prove the Dock actually RENDERS the menu; it
// proves the persisted data the Dock would render from is correct.
//
// Opt-in via TRAILER_MACOS_TOUCH_SYSTEM_RECENTS=1 (QSKIP otherwise) on top
// of the restore guard every test in this file already uses on macOS —
// belt-and-suspenders, since this is the most invasive of the three macOS
// cases (writes kMaxSystemRecents distinct real temp-file URLs, not just
// an empty/odd list). A default `ctest` run — including CI's gating
// "Unit tests" step — never needs this env var and never runs this case.
void TestDockRecents::macosSystemRecentsRoundTrip() {
    if (qEnvironmentVariableIntValue("TRAILER_MACOS_TOUCH_SYSTEM_RECENTS") != 1) {
        QSKIP("Set TRAILER_MACOS_TOUCH_SYSTEM_RECENTS=1 to run this test — it "
              "writes to (then restores) this machine's REAL, persistent macOS "
              "system Recent Documents list (NSDocumentController). Opt-in "
              "only so a normal `ctest` run never touches real Dock/Launch-"
              "Services state on a developer machine or the shared "
              "self-hosted CI runner.");
    }

    SystemRecentsRestoreGuard restoreOnExit;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QStringList mostRecentFirst;
    for (int i = 0; i < DockRecents::kMaxSystemRecents; ++i) {
        const QString path = dir.filePath(QString("dock-recents-test-%1.pdf").arg(i));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("test", 4);
        f.close();
        mostRecentFirst.append(QFileInfo(path).canonicalFilePath());
    }

    DockRecents::syncSystemRecents(mostRecentFirst);
    const QStringList readBack = DockRecents::systemRecentsForTesting();

    // Apple's own NSDocumentController.maximumRecentDocumentCount docs
    // say the OS may apply its own additional cap — "subject to change
    // and may or may not be derived from a setting made by the user in
    // System Preferences" (quoted in the PR body). Assert readBack is a
    // most-recent-first PREFIX of what was written, not necessarily an
    // exact-size match, so this test is honest about that OS-side
    // uncertainty rather than assuming a specific number.
    QVERIFY2(!readBack.isEmpty(),
             "system Recent Documents list was empty after syncSystemRecents "
             "— the write-then-read-back round trip did not persist anything");
    for (int i = 0; i < readBack.size(); ++i) {
        QCOMPARE(readBack[i], mostRecentFirst[i]);
    }
    qInfo("macOS system Recent Documents readback: %d of %d written entries "
          "survived (the OS's own cap, if any, observed on THIS machine)",
          readBack.size(), mostRecentFirst.size());
}
#endif

QTEST_MAIN(TestDockRecents)
#include "test_dock_recents.moc"
