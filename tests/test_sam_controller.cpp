// Unit tests for trailer::SamController.
//
// The controller's public surface is "submit work to MlScheduler, cache
// the prepared embedding, throttle decoder dispatches". These tests
// exercise the LRU + throttling without standing up the heavy ONNX
// path — `insertSyntheticCacheEntryForTest` lets the LRU test poke
// pre-encoded entries directly, and the burst throttle test counts
// dispatches via `decoderDispatchCountForTest`.
//
//   uat_sam_ctl_010_LruEvictsOldestAfterCapacity
//       Insert four distinct (doc, page, hash) entries. The fourth
//       evicts the LRU; the cache holds at most kLruCapacity entries.
//
//   uat_sam_ctl_020_BurstOfRequestsThrottlesDispatches
//       Push 10 requestSegment calls with no delay; only ~1-2
//       dispatches reach the scheduler. The throttle interval is
//       33 ms (~30 Hz) per kThrottleIntervalMs.
//
//   uat_sam_ctl_030_PurgeDocumentDropsCacheEntries
//       Insert entries for two documents; purge one. Only the
//       other survives.

#include "app/Application.h"
#include "ml/SamSession.h"
#include "ui/SamController.h"

#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <chrono>
#include <thread>

using namespace trailer;

namespace {

// Stand-in for IDocument*. The controller treats the pointer as a
// scalar key (no methods are called on it), so any distinct address
// works. We hand out integer addresses cast through IDocument*; the
// controller never dereferences them.
IDocument *fakeDoc(int slot) {
    return reinterpret_cast<IDocument *>(static_cast<std::uintptr_t>(0x100 + slot));
}

// Helper: pump pending events until `done()` returns true or
// `deadlineMs` ticks elapse. QueuedConnection invocations from the
// scheduler land on the GUI thread, so a test that's blocking on a
// reply needs to spin the event loop.
void pumpUntil(int deadlineMs, const std::function<bool()> &done) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
               .count() < deadlineMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (done())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

} // namespace

class TestSamController : public QObject {
    Q_OBJECT
  public:
    // Owned by the hand-rolled main() below so each test method shares
    // a single Application instance — QApplication is a process-wide
    // singleton and constructing it twice (which QTEST_MAIN combined
    // with per-test stack instances was doing) segfaults on teardown.
    Application *app = nullptr;

  private slots:
    void initTestCase();
    void uat_sam_ctl_010_LruEvictsOldestAfterCapacity();
    void uat_sam_ctl_020_BurstOfRequestsThrottlesDispatches();
    void uat_sam_ctl_030_PurgeDocumentDropsCacheEntries();

  private:
    QTemporaryDir m_home;
};

void TestSamController::initTestCase() {
    QVERIFY(m_home.isValid());
    qputenv("HOME", m_home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (m_home.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + "/.local/share").toUtf8());
    QDir().mkpath(m_home.path() + "/.config/trailer");
    QDir().mkpath(m_home.path() + "/.local/share/trailer");
}

void TestSamController::uat_sam_ctl_010_LruEvictsOldestAfterCapacity() {
    SamController ctl(app);

    QCOMPARE(SamController::kLruCapacity, 3);

    // Insert kLruCapacity distinct keys — cache fills exactly to
    // capacity.
    for (int i = 0; i < SamController::kLruCapacity; ++i) {
        const int size = ctl.insertSyntheticCacheEntryForTest(
            fakeDoc(1), i, 0x1000ULL + static_cast<unsigned long long>(i));
        QCOMPARE(size, i + 1);
    }
    QCOMPARE(ctl.cacheSizeForTest(), SamController::kLruCapacity);

    // The next distinct key evicts the LRU entry; cache stays at
    // capacity.
    const int afterOverflow =
        ctl.insertSyntheticCacheEntryForTest(fakeDoc(1), 99, 0x9999ULL);
    QCOMPARE(afterOverflow, SamController::kLruCapacity);
    QCOMPARE(ctl.cacheSizeForTest(), SamController::kLruCapacity);
}

void TestSamController::uat_sam_ctl_020_BurstOfRequestsThrottlesDispatches() {
    SamController ctl(app);
    ctl.setDocument(fakeDoc(1), 0);

    // Without real ONNX models, requestSegment still dispatches to
    // the scheduler — the decoder returns a null mask but the
    // dispatch count increments. That's what the throttle test
    // observes.
    int callbackInvocations = 0;
    auto cb = [&callbackInvocations](const QImage &) { ++callbackInvocations; };

    // Burst of 10 requests synthesised at "0 ms intervals". The first
    // dispatches immediately (no prior dispatch). The remaining 9
    // arrive while the first is in flight — they overwrite each other
    // in m_pending and produce at most one additional dispatch when
    // the in-flight decoder completes.
    for (int i = 0; i < 10; ++i) {
        QVector<QPoint> positives{QPoint(10 + i, 10 + i)};
        ctl.requestSegment(positives, {}, cb);
    }

    // Spin the event loop until both dispatches complete. With no
    // model the decoder returns immediately; cap at 500 ms.
    pumpUntil(500, [&callbackInvocations]() {
        return callbackInvocations >= 2;
    });

    const int dispatches = ctl.decoderDispatchCountForTest();
    QVERIFY2(dispatches <= 2,
             qPrintable(QString("expected at most 2 dispatches, got %1").arg(dispatches)));
    QVERIFY2(dispatches >= 1, "expected at least one dispatch from the burst");
    QCOMPARE(SamController::kThrottleIntervalMs, 33);
}

void TestSamController::uat_sam_ctl_030_PurgeDocumentDropsCacheEntries() {
    SamController ctl(app);

    QCOMPARE(ctl.cacheSizeForTest(), 0);

    // Two entries for doc A.
    ctl.insertSyntheticCacheEntryForTest(fakeDoc(1), 0, 0xAAAA);
    ctl.insertSyntheticCacheEntryForTest(fakeDoc(1), 1, 0xAAAB);
    // One for doc B.
    ctl.insertSyntheticCacheEntryForTest(fakeDoc(2), 0, 0xBBBB);
    QCOMPARE(ctl.cacheSizeForTest(), 3);

    ctl.purgeDocument(fakeDoc(1));
    QCOMPARE(ctl.cacheSizeForTest(), 1);

    // Purge with no matching entries is a no-op.
    ctl.purgeDocument(fakeDoc(99));
    QCOMPARE(ctl.cacheSizeForTest(), 1);

    // Purge doc B too.
    ctl.purgeDocument(fakeDoc(2));
    QCOMPARE(ctl.cacheSizeForTest(), 0);
}

// Hand-rolled main rather than QTEST_MAIN — we need exactly one
// trailer::Application (which subclasses QApplication) for the whole
// binary; QTEST_MAIN constructs a plain QApplication, which conflicts
// with per-test Application instances on the stack.
int main(int argc, char *argv[]) {
    // See tests/test_image_scale.cpp's main() for why this is needed on
    // macOS: QSettings(org, app) (used by Application's DocumentTypeDefaults
    // / RecentFiles) defaults to NativeFormat there, which ignores the HOME
    // sandboxing TestSamController::initTestCase() sets up below. Must be
    // set before Application is constructed (a process-global QSettings
    // setting, not tied to the sandboxed HOME itself).
    QSettings::setDefaultFormat(QSettings::IniFormat);

    trailer::Application app(argc, argv);
    TestSamController tc;
    tc.app = &app;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_sam_controller.moc"
