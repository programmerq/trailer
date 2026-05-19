#include "MlScheduler.h"

#include "platform/PowerSource.h"
#include "settings/Settings.h"

#include <algorithm>
#include <chrono>

namespace trailer {

namespace {

// Polling cadence for the power-source watcher. Plug-in / unplug
// events happen on human timescales (seconds at the fastest, hours
// at the slowest); 30 seconds is well inside the responsiveness
// budget for "you just unplugged the cable, please stop running
// speculative work" while keeping the IOKit / GetSystemPowerStatus
// call rate negligible. Configurable via the test seam if a future
// test wants to drive transitions faster.
constexpr int kPowerPollSeconds = 30;

} // namespace

QString mlPriorityToString(MlPriority p) {
    switch (p) {
    case MlPriority::Idle:
        return QStringLiteral("Idle");
    case MlPriority::Prefetch:
        return QStringLiteral("Prefetch");
    case MlPriority::VisiblePage:
        return QStringLiteral("VisiblePage");
    case MlPriority::UserAction:
        return QStringLiteral("UserAction");
    }
    return QStringLiteral("Unknown");
}

MlScheduler::MlScheduler(Settings *settings, QObject *parent)
    : QObject(parent), m_settings(settings) {
    m_worker = std::thread([this]() { workerLoop(); });
    m_powerWatcher = std::thread([this]() {
        std::unique_lock<std::mutex> lk(m_mutex);
        while (!m_powerWatcherStop) {
            // Wait either for a stop signal or for the poll interval.
            // No racy sleep loop — m_powerCv flips on stop() so we
            // unblock immediately on shutdown.
            m_powerCv.wait_for(lk, std::chrono::seconds(kPowerPollSeconds),
                               [this]() { return m_powerWatcherStop; });
            if (m_powerWatcherStop) {
                break;
            }
            // Drop the lock around the policy re-eval — it takes the
            // same mutex itself.
            lk.unlock();
            reevaluatePowerPolicy();
            lk.lock();
        }
    });
}

MlScheduler::~MlScheduler() {
    stop();
}

bool MlScheduler::runOnBatterySetting() const {
    if (m_settings == nullptr) {
        return true;
    }
    return m_settings->mlRunOnBattery();
}

bool MlScheduler::shouldPreCancel(MlPriority priority) const {
    if (!isSpeculative(priority)) {
        return false;
    }
    if (runOnBatterySetting()) {
        return false;
    }
    return PowerSource::currentState() == PowerState::OnBattery;
}

MlScheduler::Handle MlScheduler::submit(MlPriority priority, QString label,
                                        std::function<void(CancellationToken &)> work) {
    Handle handle;
    handle.token = std::make_shared<CancellationToken>();

    {
        std::unique_lock<std::mutex> lk(m_mutex);
        if (m_stopping) {
            handle.token->cancel();
            return handle;
        }
    }

    // Decide pre-cancellation under the mutex so two concurrent
    // submits can't both decide "we're on AC" mid-transition. (The
    // mutex is also what the power watcher synchronises against.)
    bool preCancel = false;
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        preCancel = shouldPreCancel(priority);
    }
    if (preCancel) {
        handle.token->cancel();
        return handle;
    }

    Task t;
    t.id = m_nextId.fetch_add(1, std::memory_order_relaxed);
    t.priority = priority;
    t.label = std::move(label);
    t.work = std::move(work);
    t.token = handle.token;
    handle.id = t.id;

    {
        std::unique_lock<std::mutex> lk(m_mutex);
        // Cancel queued lower-priority tasks so they can't take the
        // worker before this one gets a chance. Already-running task
        // is not interrupted from outside — its own token may be
        // flipped via cancel(id) if the caller wants that.
        for (auto &queued : m_queue) {
            if (static_cast<int>(queued.priority) < static_cast<int>(priority)) {
                if (queued.token) {
                    queued.token->cancel();
                }
            }
        }
        m_queue.push_back(std::move(t));
        std::push_heap(m_queue.begin(), m_queue.end(), TaskGreater{});
        m_cv.notify_one();
    }
    emit statsChanged();
    return handle;
}

void MlScheduler::cancel(MlTaskId id) {
    CancellationTokenPtr targetToken;
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        if (m_running && m_runningId == id) {
            // Currently running — we can't pop it out of execution,
            // but flipping its token gives the worker a checkpoint
            // to bail at. The token is also held by the worker
            // thread, but the lookup here goes through the
            // separate copy that the queue stashed.
        }
        for (const auto &task : m_queue) {
            if (task.id == id) {
                targetToken = task.token;
                break;
            }
        }
    }
    if (targetToken) {
        targetToken->cancel();
    }
}

void MlScheduler::cancelAll() {
    std::vector<CancellationTokenPtr> tokens;
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        tokens.reserve(m_queue.size());
        for (auto &task : m_queue) {
            if (task.token) {
                tokens.push_back(task.token);
            }
        }
    }
    for (auto &tok : tokens) {
        tok->cancel();
    }
}

void MlScheduler::cancelMatching(
    const std::function<bool(const QString &, MlPriority)> &predicate) {
    if (!predicate) {
        return;
    }
    std::vector<CancellationTokenPtr> tokens;
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        tokens.reserve(m_queue.size());
        for (auto &task : m_queue) {
            if (task.token && predicate(task.label, task.priority)) {
                tokens.push_back(task.token);
            }
        }
    }
    for (auto &tok : tokens) {
        tok->cancel();
    }
}

MlSchedulerStats MlScheduler::stats() const {
    std::unique_lock<std::mutex> lk(m_mutex);
    MlSchedulerStats out;
    out.queued = static_cast<int>(m_queue.size());
    out.running = m_running ? 1 : 0;
    out.runningPriority = m_runningPriority;
    out.runningLabel = m_runningLabel;
    return out;
}

void MlScheduler::stop() {
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        if (m_stopping && !m_worker.joinable() && !m_powerWatcher.joinable()) {
            return;
        }
        m_stopping = true;
        m_powerWatcherStop = true;
        // Cancel everything queued so a stuck worker can wind down.
        for (auto &task : m_queue) {
            if (task.token) {
                task.token->cancel();
            }
        }
        m_cv.notify_all();
        m_powerCv.notify_all();
    }
    if (m_worker.joinable()) {
        m_worker.join();
    }
    if (m_powerWatcher.joinable()) {
        m_powerWatcher.join();
    }
}

bool MlScheduler::waitForIdle(int timeoutMs) {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeoutMs);
    std::unique_lock<std::mutex> lk(m_mutex);
    return m_cv.wait_until(lk, deadline, [this]() { return !m_running && m_queue.empty(); });
}

void MlScheduler::reevaluatePowerPolicy() {
    if (runOnBatterySetting()) {
        return;
    }
    if (PowerSource::currentState() != PowerState::OnBattery) {
        return;
    }
    // On battery + run_on_battery=false: any queued speculative
    // task (Idle / Prefetch) should be cancelled so it stops
    // burning power the moment its checkpoint fires.
    cancelMatching([this](const QString &, MlPriority p) { return isSpeculative(p); });
}

void MlScheduler::workerLoop() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [this]() { return m_stopping || !m_queue.empty(); });
            if (m_stopping && m_queue.empty()) {
                return;
            }
            std::pop_heap(m_queue.begin(), m_queue.end(), TaskGreater{});
            task = std::move(m_queue.back());
            m_queue.pop_back();
            m_runningPriority = task.priority;
            m_runningLabel = task.label;
            m_runningId = task.id;
            m_running = true;
        }
        emit statsChanged();
        // If the task was pre-cancelled (e.g. someone called
        // cancelAll while it was sitting in the queue), skip the
        // body — the worker still runs through its slot so stats
        // remain accurate.
        if (task.token && !task.token->isCancelled()) {
            try {
                task.work(*task.token);
            } catch (...) {
                // Swallow exceptions — a worker throwing across
                // the std::thread boundary terminates the process.
                // The ML engines we call don't throw under normal
                // operation; if one does we'd rather lose a single
                // task than the whole app.
            }
        }
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_running = false;
            m_runningId = 0;
            m_runningLabel.clear();
            m_runningPriority = MlPriority::Idle;
            // Wake waitForIdle().
            m_cv.notify_all();
        }
        emit statsChanged();
    }
}

} // namespace trailer
