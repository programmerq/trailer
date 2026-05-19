#pragma once

#include <atomic>
#include <memory>

namespace trailer {

// Cooperative cancellation primitive shared between the producer of a
// long-running task (typically MlScheduler) and the worker code that
// runs inside it. The worker polls isCancelled() between major steps
// (e.g. between OCR detector and recognizer, or per image inside a
// loop over detected regions) and bails as soon as it sees true.
//
// Ownership rule: the *caller* owns the token. Holding the token is
// what gives you the right to cancel — handing one out is granting
// permission to interrupt. Workers should hold a const pointer or a
// const reference; only owners call cancel().
//
// Two ways to use this:
//
//   1. Stack-allocate when the cancellation lifetime is bounded by the
//      caller's stack frame (rare; mostly tests).
//
//   2. std::shared_ptr<CancellationToken> for the normal case where
//      the scheduler hands the token off to a worker thread. The
//      worker captures the shared_ptr by value; the scheduler keeps
//      its own copy so cancelAll() can still flip the flag even if
//      the worker has exited.
//
// A default-constructed token never reports cancellation, which lets
// existing call-sites that take `const CancellationToken *` keep a
// nullptr default without special-casing it in the worker.
class CancellationToken {
  public:
    CancellationToken() = default;
    ~CancellationToken() = default;

    // Non-copyable, non-movable. Workers and owners share the *same*
    // token via pointer/reference; copying would defeat that.
    CancellationToken(const CancellationToken &) = delete;
    CancellationToken &operator=(const CancellationToken &) = delete;
    CancellationToken(CancellationToken &&) = delete;
    CancellationToken &operator=(CancellationToken &&) = delete;

    // Mark the token cancelled. Idempotent. Thread-safe; can be
    // called from any thread.
    void cancel() noexcept { m_cancelled.store(true, std::memory_order_release); }

    // Snapshot the cancellation flag. Thread-safe. Returns false on a
    // default-constructed token forever.
    bool isCancelled() const noexcept { return m_cancelled.load(std::memory_order_acquire); }

    // Helper used by callers that hold an optional pointer: treats
    // nullptr as "no cancellation requested" so existing call-sites
    // that pass a `const CancellationToken *` default-null don't need
    // to special-case it.
    static bool isCancelled(const CancellationToken *token) noexcept {
        return token != nullptr && token->isCancelled();
    }

  private:
    std::atomic<bool> m_cancelled{false};
};

// Convenience alias — most schedulers hand out shared ownership so the
// token survives the worker that captured it.
using CancellationTokenPtr = std::shared_ptr<CancellationToken>;

} // namespace trailer
