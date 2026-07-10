// Shared test support for the structural performance tests.
//
// InstrumentedIODevice is a read-only QIODevice over an in-memory byte
// buffer that records, for every read the consumer performs:
//   * which byte ranges were touched (so a test can tell whether the
//     ENTIRE file has been consumed yet), and
//   * the QThread that issued the read (so a test can assert whether IO
//     happened on the GUI/main thread or a worker).
//
// It is the injectable IO seam the perf tests use: QPdfDocument and
// QImageReader both accept a QIODevice* overload, so a test can drive
// the exact render engine the production adapters wrap while observing
// its read pattern — something the path-based overloads the adapters
// currently call do not expose.

#pragma once

#include <QIODevice>
#include <QThread>
#include <QByteArray>

#include <vector>

namespace trailer::perf {

class InstrumentedIODevice : public QIODevice {
  public:
    explicit InstrumentedIODevice(QByteArray data, QObject *parent = nullptr)
        : QIODevice(parent), m_data(std::move(data)),
          m_covered(static_cast<size_t>(m_data.size()), false) {}

    bool isSequential() const override { return false; }

    qint64 size() const override { return m_data.size(); }

    // Total number of distinct file bytes the consumer has read so far.
    // Strictly less than size() means the whole file has NOT been
    // consumed yet.
    qint64 uniqueBytesRead() const {
        qint64 n = 0;
        for (bool b : m_covered)
            if (b)
                ++n;
        return n;
    }

    bool fullyConsumed() const { return uniqueBytesRead() == size(); }

    // Distinct threads that issued reads, in first-seen order.
    const std::vector<QThread *> &readThreads() const { return m_readThreads; }
    int readCount() const { return m_readCount; }

    void resetInstrumentation() {
        std::fill(m_covered.begin(), m_covered.end(), false);
        m_readThreads.clear();
        m_readCount = 0;
    }

  protected:
    qint64 readData(char *data, qint64 maxSize) override {
        const qint64 p = pos();
        const qint64 available = m_data.size() - p;
        const qint64 n = std::min(maxSize, available);
        if (n <= 0)
            return 0;
        memcpy(data, m_data.constData() + p, static_cast<size_t>(n));
        for (qint64 i = 0; i < n; ++i)
            m_covered[static_cast<size_t>(p + i)] = true;
        ++m_readCount;
        QThread *t = QThread::currentThread();
        if (std::find(m_readThreads.begin(), m_readThreads.end(), t) == m_readThreads.end())
            m_readThreads.push_back(t);
        return n;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

  private:
    QByteArray m_data;
    std::vector<bool> m_covered;
    std::vector<QThread *> m_readThreads;
    int m_readCount = 0;
};

} // namespace trailer::perf
