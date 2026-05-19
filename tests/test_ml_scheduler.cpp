// Unit tests for trailer::MlScheduler.
//
// The scheduler runs at most one task at a time, so the standard
// trick for proving priority ordering is to submit a "gate" task
// that blocks on a condition variable, queue the priority-test
// tasks behind it, then release the gate. When the gate completes,
// the next task chosen by the worker is the one we wanted to assert
// would win — and the others run after it in their own order.
//
// Cancellation is straightforward: submit while the gate holds the
// worker, then cancelAll(). When the gate releases, the queued
// tasks see their tokens flipped before they run.
//
// Power policy: inject `PowerState::OnBattery` via the test seam,
// disable run_on_battery in the Settings store, submit a Prefetch,
// assert the returned token is already cancelled.

#include "ml/CancellationToken.h"
#include "ml/MlScheduler.h"
#include "platform/PowerSource.h"
#include "settings/Settings.h"

#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

using namespace trailer;

namespace {

PowerState forceBattery() {
    return PowerState::OnBattery;
}

PowerState forceAc() {
    return PowerState::OnAC;
}

// Convenience: a "gate" task that holds the worker thread until the
// test driver flips a flag. Used to stall the worker so we can
// observe queue ordering instead of seeing the first-submitted task
// race straight into execution.
struct Gate {
    std::mutex m;
    std::condition_variable cv;
    bool open = false;
    bool entered = false;

    void waitForEntry() {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [this]() { return entered; });
    }
    void release() {
        std::unique_lock<std::mutex> lk(m);
        open = true;
        cv.notify_all();
    }
    void hold(CancellationToken &) {
        {
            std::unique_lock<std::mutex> lk(m);
            entered = true;
            cv.notify_all();
        }
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [this]() { return open; });
    }
};

} // namespace

class TestMlScheduler : public QObject {
    Q_OBJECT
  private slots:
    void init() { PowerSource::setProbeForTesting(&forceAc); }
    void cleanup() { PowerSource::clearProbeForTesting(); }
    void priorityOrdering();
    void higherPriorityWinsOverEqualPriorityFifo();
    void cancelAllFlipsQueuedTokens();
    void cancelByIdFlipsQueuedToken();
    void powerPolicyPreCancelsPrefetchOnBattery();
    void powerPolicyAllowsUserActionOnBattery();
    void runOnBatterySettingDisablesPreCancel();
    void reevaluatePowerPolicyCancelsQueuedSpeculative();
    void statsChangedFiresOnSubmitAndCompletion();
};

void TestMlScheduler::priorityOrdering() {
    Settings s{QString()};
    MlScheduler scheduler(&s);

    Gate gate;
    // Submit the gate first; the worker grabs it immediately. The
    // gate runs at UserAction priority because we want any
    // newly-submitted task to sit behind it without cancelling it.
    scheduler.submit(MlPriority::UserAction, QStringLiteral("gate"),
                     [&gate](CancellationToken &t) { gate.hold(t); });
    gate.waitForEntry();

    std::mutex orderMutex;
    std::vector<QString> runOrder;
    auto record = [&](QString name) {
        return [&, name](CancellationToken &t) {
            // Cancelled tasks are run by the worker as a no-op
            // (the worker checks isCancelled() before invoking the
            // body); guarding here mirrors what a real worker does.
            if (t.isCancelled())
                return;
            std::unique_lock<std::mutex> lk(orderMutex);
            runOrder.push_back(name);
        };
    };
    // Queue two Idle tasks, then a UserAction. Per the spec, the
    // UserAction submission cancels the queued Idle tasks because
    // they are lower priority — only the UserAction body runs. This
    // is the design: a user click never waits behind speculative
    // background work.
    scheduler.submit(MlPriority::Idle, QStringLiteral("idle-1"), record(QStringLiteral("idle-1")));
    scheduler.submit(MlPriority::Idle, QStringLiteral("idle-2"), record(QStringLiteral("idle-2")));
    scheduler.submit(MlPriority::UserAction, QStringLiteral("user"),
                     record(QStringLiteral("user")));

    gate.release();
    QVERIFY(scheduler.waitForIdle(5000));

    std::unique_lock<std::mutex> lk(orderMutex);
    QCOMPARE(runOrder.size(), static_cast<size_t>(1));
    QCOMPARE(runOrder[0], QStringLiteral("user"));
}

void TestMlScheduler::higherPriorityWinsOverEqualPriorityFifo() {
    // Queue several equal-priority tasks behind a gate, then make
    // sure they run in FIFO order — that proves the heap-by-id tie
    // break is doing what we documented.
    Settings s{QString()};
    MlScheduler scheduler(&s);

    Gate gate;
    scheduler.submit(MlPriority::UserAction, QStringLiteral("gate"),
                     [&gate](CancellationToken &t) { gate.hold(t); });
    gate.waitForEntry();

    std::mutex orderMutex;
    std::vector<QString> runOrder;
    auto record = [&](QString name) {
        return [&, name](CancellationToken &t) {
            if (t.isCancelled())
                return;
            std::unique_lock<std::mutex> lk(orderMutex);
            runOrder.push_back(name);
        };
    };
    scheduler.submit(MlPriority::VisiblePage, QStringLiteral("vp-1"),
                     record(QStringLiteral("vp-1")));
    scheduler.submit(MlPriority::VisiblePage, QStringLiteral("vp-2"),
                     record(QStringLiteral("vp-2")));
    scheduler.submit(MlPriority::VisiblePage, QStringLiteral("vp-3"),
                     record(QStringLiteral("vp-3")));

    gate.release();
    QVERIFY(scheduler.waitForIdle(5000));

    std::unique_lock<std::mutex> lk(orderMutex);
    QCOMPARE(runOrder.size(), static_cast<size_t>(3));
    QCOMPARE(runOrder[0], QStringLiteral("vp-1"));
    QCOMPARE(runOrder[1], QStringLiteral("vp-2"));
    QCOMPARE(runOrder[2], QStringLiteral("vp-3"));
}

void TestMlScheduler::cancelAllFlipsQueuedTokens() {
    Settings s{QString()};
    MlScheduler scheduler(&s);

    Gate gate;
    scheduler.submit(MlPriority::UserAction, QStringLiteral("gate"),
                     [&gate](CancellationToken &t) { gate.hold(t); });
    gate.waitForEntry();

    std::atomic<int> ran{0};
    auto body = [&](CancellationToken &) { ran.fetch_add(1, std::memory_order_relaxed); };
    auto h1 = scheduler.submit(MlPriority::Idle, QStringLiteral("a"), body);
    auto h2 = scheduler.submit(MlPriority::Prefetch, QStringLiteral("b"), body);
    auto h3 = scheduler.submit(MlPriority::VisiblePage, QStringLiteral("c"), body);

    // cancelAll() must flip every queued token before any task
    // body sees the worker — that's the contract the spec asks for.
    // Sample the flags here and then again after waiting for idle,
    // because the workers will run-as-no-op after we release the
    // gate.
    scheduler.cancelAll();
    QVERIFY(h1.token->isCancelled());
    QVERIFY(h2.token->isCancelled());
    QVERIFY(h3.token->isCancelled());

    gate.release();
    QVERIFY(scheduler.waitForIdle(5000));
    // The worker short-circuits cancelled tasks — none of the
    // bodies should have been invoked.
    QCOMPARE(ran.load(), 0);
}

void TestMlScheduler::cancelByIdFlipsQueuedToken() {
    Settings s{QString()};
    MlScheduler scheduler(&s);

    Gate gate;
    scheduler.submit(MlPriority::UserAction, QStringLiteral("gate"),
                     [&gate](CancellationToken &t) { gate.hold(t); });
    gate.waitForEntry();

    std::atomic<bool> ranTarget{false};
    std::atomic<bool> ranOther{false};
    auto target = scheduler.submit(MlPriority::Idle, QStringLiteral("target"),
                                   [&ranTarget](CancellationToken &t) {
                                       if (!t.isCancelled())
                                           ranTarget.store(true);
                                   });
    scheduler.submit(MlPriority::Idle, QStringLiteral("other"), [&ranOther](CancellationToken &t) {
        if (!t.isCancelled())
            ranOther.store(true);
    });
    scheduler.cancel(target.id);
    gate.release();
    QVERIFY(scheduler.waitForIdle(5000));

    QVERIFY(!ranTarget.load());
    QVERIFY(ranOther.load());
}

void TestMlScheduler::powerPolicyPreCancelsPrefetchOnBattery() {
    Settings s{QString()};
    s.setMlRunOnBattery(false);
    PowerSource::setProbeForTesting(&forceBattery);
    MlScheduler scheduler(&s);

    auto handle = scheduler.submit(MlPriority::Prefetch, QStringLiteral("speculative"),
                                   [](CancellationToken &) {});
    QVERIFY(handle.token);
    QVERIFY(handle.token->isCancelled());
    // Pre-cancelled — id stays zero, indicating "no queued task".
    QCOMPARE(handle.id, MlTaskId{0});
    QVERIFY(scheduler.waitForIdle(5000));
}

void TestMlScheduler::powerPolicyAllowsUserActionOnBattery() {
    Settings s{QString()};
    s.setMlRunOnBattery(false);
    PowerSource::setProbeForTesting(&forceBattery);
    MlScheduler scheduler(&s);

    std::atomic<bool> ran{false};
    auto handle = scheduler.submit(MlPriority::UserAction, QStringLiteral("user"),
                                   [&](CancellationToken &) { ran.store(true); });
    QVERIFY(handle.id != MlTaskId{0});
    QVERIFY(!handle.token->isCancelled());
    QVERIFY(scheduler.waitForIdle(5000));
    QVERIFY(ran.load());
}

void TestMlScheduler::runOnBatterySettingDisablesPreCancel() {
    Settings s{QString()};
    s.setMlRunOnBattery(true);
    PowerSource::setProbeForTesting(&forceBattery);
    MlScheduler scheduler(&s);

    std::atomic<bool> ran{false};
    auto handle = scheduler.submit(MlPriority::Prefetch, QStringLiteral("ok-on-battery"),
                                   [&](CancellationToken &) { ran.store(true); });
    QVERIFY(handle.id != MlTaskId{0});
    QVERIFY(!handle.token->isCancelled());
    QVERIFY(scheduler.waitForIdle(5000));
    QVERIFY(ran.load());
}

void TestMlScheduler::reevaluatePowerPolicyCancelsQueuedSpeculative() {
    Settings s{QString()};
    s.setMlRunOnBattery(false);
    PowerSource::setProbeForTesting(&forceAc);
    MlScheduler scheduler(&s);

    Gate gate;
    scheduler.submit(MlPriority::UserAction, QStringLiteral("gate"),
                     [&gate](CancellationToken &t) { gate.hold(t); });
    gate.waitForEntry();

    std::atomic<bool> prefetchRan{false};
    std::atomic<bool> userRan{false};
    auto pre =
        scheduler.submit(MlPriority::Prefetch, QStringLiteral("pre"), [&](CancellationToken &t) {
            if (!t.isCancelled())
                prefetchRan.store(true);
        });
    auto user =
        scheduler.submit(MlPriority::UserAction, QStringLiteral("u"), [&](CancellationToken &t) {
            if (!t.isCancelled())
                userRan.store(true);
        });
    QVERIFY(pre.id != MlTaskId{0});
    QVERIFY(user.id != MlTaskId{0});

    // Simulate a "user unplugged" transition. The watcher would
    // normally pick this up on its 30 s poll; here we drive it
    // synchronously.
    PowerSource::setProbeForTesting(&forceBattery);
    scheduler.reevaluatePowerPolicy();

    gate.release();
    QVERIFY(scheduler.waitForIdle(5000));

    QVERIFY(!prefetchRan.load());
    QVERIFY(userRan.load());
}

void TestMlScheduler::statsChangedFiresOnSubmitAndCompletion() {
    Settings s{QString()};
    MlScheduler scheduler(&s);

    QSignalSpy spy(&scheduler, &MlScheduler::statsChanged);
    std::atomic<bool> ran{false};
    scheduler.submit(MlPriority::UserAction, QStringLiteral("u"),
                     [&](CancellationToken &) { ran.store(true); });
    QVERIFY(scheduler.waitForIdle(5000));
    // At minimum: one fire on submit (queue depth changed), one
    // when the task starts (running flips to true), one when it
    // finishes (running flips back to false). Different glibc /
    // libc++ implementations may coalesce these but we should see
    // at least two.
    QVERIFY(spy.count() >= 2);
    QVERIFY(ran.load());
}

QTEST_MAIN(TestMlScheduler)
#include "test_ml_scheduler.moc"
