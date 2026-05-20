#pragma once

#include "ml/CancellationToken.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace trailer {

class Settings;

// Priority levels for ML work submitted to MlScheduler. Higher
// values run first; when a higher-priority task arrives while a
// lower-priority queued task is waiting, the queued task's
// cancellation token is flipped before it ever gets picked up so
// the worker thread is freed for the user-facing click.
//
// The currently-running task continues to its next cancellation
// checkpoint — we don't kill threads from outside, that's the
// PHILOSOPHY ("no unsafe interruption") and the engineering reality
// (ORT sessions hate having their stack ripped out). Workers poll
// `CancellationToken::isCancelled()` between major steps.
enum class MlPriority : int {
    Idle = 0,        // Best-effort speculative work. First to be
                     // cancelled on policy changes (battery), last
                     // to run.
    Prefetch = 1,    // Background preparation of likely-needed work
                     // (e.g. encode the next visible page's SAM
                     // embedding). Cancelled along with Idle on
                     // battery if run_on_battery=false.
    VisiblePage = 2, // Work for the page the user is looking at
                     // right now — runs even on battery.
    UserAction = 3,  // The user explicitly clicked a button. Always
                     // runs; preempts every queued task below it.
};

struct MlSchedulerStats {
    int running = 0; // 0 or 1 — we have a single worker.
    int queued = 0;
    // Snapshot of the running task's priority + label, valid only
    // when `running == 1`. Used by the status-bar indicator to
    // decide whether to show itself.
    MlPriority runningPriority = MlPriority::Idle;
    QString runningLabel;
};

// Task identifier. Returned by submit(); accepted by cancel(). Zero
// is reserved for "no task" / pre-cancelled.
using MlTaskId = std::uint64_t;

// Single-worker ML task scheduler with priority-ordered queueing
// and cooperative cancellation. Constructed and owned by the
// Application (one per process). Workers should access it via
// Application::mlScheduler() so the same instance is shared across
// MainWindows.
//
// Submission semantics:
//
//   - submit() takes ownership of the work lambda and a label
//     (shown in the status-bar tooltip). It returns a token id
//     that can later be passed to cancel().
//   - The work signature is `void(CancellationToken &)`. The token
//     reference is the worker's read-only check-point: poll it
//     between stages, bail when it flips.
//   - Higher-priority submissions cancel lower-priority *queued*
//     work. The currently-running task is not interrupted from
//     outside — its own token may flip if the caller invokes
//     cancel(id).
//
// Power policy:
//
//   - On battery + !Settings::runOnBattery(): Prefetch and Idle
//     submissions are pre-cancelled (the returned token already
//     reports cancelled before submit() returns). VisiblePage and
//     UserAction submissions run normally.
//   - On AC: every priority runs.
//   - Power state is polled every ~30 s; if we just transitioned
//     to battery and the policy says "no", queued Prefetch/Idle
//     are cancelled.
//
// Lifetime: stop() blocks the worker thread joins. Destruction
// implies stop(). Submissions after stop() are silently dropped
// (returned future is pre-cancelled).
class MlScheduler : public QObject {
    Q_OBJECT
  public:
    // Settings pointer is optional. nullptr means "treat as
    // run_on_battery=true (always run)" — useful for tests that
    // don't want to wire a Settings store, and for the very
    // earliest startup phase before an Application is alive.
    explicit MlScheduler(Settings *settings = nullptr, QObject *parent = nullptr);
    ~MlScheduler() override;

    // Hand the work lambda over to the worker thread. The caller
    // receives a token id (for cancel()) and a shared_ptr to the
    // token (so they can also cancel the task by holding the token
    // — handy for "scope cancellation" patterns).
    //
    // `label` is shown in the status-bar tooltip when this task is
    // running. Translate it at the call site.
    struct Handle {
        MlTaskId id = 0;
        CancellationTokenPtr token;
    };
    Handle submit(MlPriority priority, QString label,
                  std::function<void(CancellationToken &)> work);

    // Cancel a single queued or in-flight task by id. The task's
    // CancellationToken is flipped; the worker thread will see it
    // on its next isCancelled() check. Idempotent.
    void cancel(MlTaskId id);

    // Cancel every queued or in-flight task. Used on document
    // close ("everything I cared about is gone — drop the work").
    void cancelAll();

    // Cancel every task for which `predicate(label, priority)`
    // returns true. Used by the power-policy reactor to drop just
    // the speculative work without touching UserAction.
    void cancelMatching(const std::function<bool(const QString &, MlPriority)> &predicate);

    // Snapshot the queue depth / running status. Used by the
    // status-bar indicator. Cheap (locks the queue mutex briefly).
    MlSchedulerStats stats() const;

    // Wait for any currently-running task to drain and stop the
    // worker thread. Idempotent. After stop(), submissions return
    // pre-cancelled handles.
    void stop();

    // Test seam: synchronously wait until the queue is empty and
    // no task is running. Useful in unit tests; production code
    // should react to signals rather than block.
    bool waitForIdle(int timeoutMs = 5000);

    // Re-evaluate the power policy now and cancel speculative work
    // if needed. Normally called by the internal poll timer; tests
    // call it directly after flipping the test-seam power state.
    void reevaluatePowerPolicy();

  signals:
    // Emitted whenever queue depth or running-task identity
    // changes. The status-bar indicator listens for this to show
    // or hide itself. Fired from the worker thread via
    // QueuedConnection.
    void statsChanged();

  private:
    struct Task {
        MlTaskId id = 0;
        MlPriority priority = MlPriority::Idle;
        QString label;
        std::function<void(CancellationToken &)> work;
        CancellationTokenPtr token;
    };

    // Compare-by-priority for the priority queue. Higher priority
    // first; among equal priorities, lower id (older) first — FIFO
    // within a priority band.
    struct TaskGreater {
        bool operator()(const Task &lhs, const Task &rhs) const {
            if (lhs.priority != rhs.priority) {
                return static_cast<int>(lhs.priority) < static_cast<int>(rhs.priority);
            }
            return lhs.id > rhs.id;
        }
    };

    void workerLoop();
    bool isSpeculative(MlPriority p) const {
        return p == MlPriority::Idle || p == MlPriority::Prefetch;
    }
    bool runOnBatterySetting() const;
    bool shouldPreCancel(MlPriority priority) const;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<Task> m_queue; // heap-ordered by TaskGreater
    bool m_stopping = false;

    // Currently running task's snapshot, written under m_mutex so
    // stats() can read it without racing with workerLoop().
    MlPriority m_runningPriority = MlPriority::Idle;
    QString m_runningLabel;
    MlTaskId m_runningId = 0;
    bool m_running = false;
    // Running task's cancellation token. Held here (as well as on the
    // worker thread's local Task copy) so cancel() / cancelAll() can
    // flip it without racing with the worker loop's pop. Cleared
    // under m_mutex when the running slot is released.
    CancellationTokenPtr m_runningToken;

    std::atomic<MlTaskId> m_nextId{1};
    std::thread m_worker;
    Settings *m_settings = nullptr;

    // Power-state polling. Started after construction; stop() joins.
    // 30 s polling is well below the timescale of plugging-in /
    // unplugging a laptop and well above the cost of querying IOKit.
    std::thread m_powerWatcher;
    std::condition_variable m_powerCv;
    bool m_powerWatcherStop = false;
};

QString mlPriorityToString(MlPriority p);

} // namespace trailer
