// Unit tests for trailer::CancellationToken — a tiny atomic-flag
// primitive shared across the scheduler worker thread and the ML
// engines. The behaviour is intentionally trivial; the only thing
// worth proving is that:
//
//   - A default-constructed token reports false forever.
//   - cancel() flips the flag and is idempotent.
//   - The nullptr-tolerant static helper treats nullptr as "not
//     cancelled" so existing call sites with `const CancellationToken
//     *` default-null don't crash.
//   - Concurrent cancel() / isCancelled() across many threads doesn't
//     produce UB. We can't formally prove a data race didn't happen,
//     but we can spin the operations hard enough that a release build
//     with no atomics would visibly fail under TSan.

#include "ml/CancellationToken.h"

#include <QObject>
#include <QtTest/QtTest>

#include <array>
#include <atomic>
#include <thread>

using namespace trailer;

class TestCancellationToken : public QObject {
    Q_OBJECT
  private slots:
    void defaultsToNotCancelled();
    void cancelFlipsTheFlag();
    void cancelIsIdempotent();
    void nullPointerHelperReturnsFalse();
    void concurrentCancelAndQueryIsSafe();
};

void TestCancellationToken::defaultsToNotCancelled() {
    CancellationToken token;
    QVERIFY(!token.isCancelled());
}

void TestCancellationToken::cancelFlipsTheFlag() {
    CancellationToken token;
    QVERIFY(!token.isCancelled());
    token.cancel();
    QVERIFY(token.isCancelled());
}

void TestCancellationToken::cancelIsIdempotent() {
    CancellationToken token;
    token.cancel();
    token.cancel();
    token.cancel();
    QVERIFY(token.isCancelled());
}

void TestCancellationToken::nullPointerHelperReturnsFalse() {
    QVERIFY(!CancellationToken::isCancelled(nullptr));
    CancellationToken live;
    QVERIFY(!CancellationToken::isCancelled(&live));
    live.cancel();
    QVERIFY(CancellationToken::isCancelled(&live));
}

void TestCancellationToken::concurrentCancelAndQueryIsSafe() {
    // Eight reader threads + four writer threads beat on the same
    // token for a few hundred ms. A non-atomic implementation would
    // show up as a data race under TSan; a correct implementation
    // just returns. We assert the eventual state ("cancelled")
    // because the test still proves the writers reached the token.
    constexpr int kReaderThreads = 8;
    constexpr int kWriterThreads = 4;
    constexpr int kIterations = 5000;
    CancellationToken token;
    std::atomic<bool> stop{false};
    std::atomic<int> observedTrue{0};

    std::array<std::thread, kReaderThreads> readers;
    for (auto &t : readers) {
        t = std::thread([&]() {
            while (!stop.load(std::memory_order_acquire)) {
                if (token.isCancelled()) {
                    observedTrue.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    std::array<std::thread, kWriterThreads> writers;
    for (auto &t : writers) {
        t = std::thread([&]() {
            for (int i = 0; i < kIterations; ++i) {
                token.cancel();
            }
        });
    }
    for (auto &t : writers) {
        t.join();
    }
    stop.store(true, std::memory_order_release);
    for (auto &t : readers) {
        t.join();
    }
    QVERIFY(token.isCancelled());
    // At least some readers observed the cancellation while the
    // writers were still running — if observedTrue is zero the
    // writers never reached the flag, which would point at a
    // memory-ordering bug.
    QVERIFY(observedTrue.load() > 0);
}

QTEST_MAIN(TestCancellationToken)
#include "test_cancellation_token.moc"
