#include "UxEventStream.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QMutexLocker>

namespace trailer {

namespace {

// Self-flush threshold. A mouse-path storm produces a few events per
// flush interval, so in practice the timer drains the buffer first;
// this cap only matters if the owner's flush timer stalls (modal
// native dialog, debugger pause) and keeps memory bounded at roughly
// 256 × a-few-hundred-bytes.
constexpr int kMaxBufferedLines = 256;

} // namespace

UxEventStream::UxEventStream() = default;

UxEventStream::~UxEventStream() {
    close();
}

bool UxEventStream::open(const QString &filePath, const QString &sessionId) {
    QMutexLocker locker(&m_mutex);
    if (m_open) {
        return false;
    }
    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    m_sessionId = sessionId;
    m_sequence = 0;
    m_buffer.clear();
    m_clock.start();
    m_open = true;
    return true;
}

bool UxEventStream::isOpen() const {
    QMutexLocker locker(&m_mutex);
    return m_open;
}

quint64 UxEventStream::append(const QString &source, const QString &type, const QJsonObject &data) {
    QMutexLocker locker(&m_mutex);
    if (!m_open) {
        return 0;
    }
    const quint64 sequence = ++m_sequence;

    QJsonObject envelope;
    envelope.insert(QStringLiteral("schema_version"), kUxSchemaVersion);
    envelope.insert(QStringLiteral("session_id"), m_sessionId);
    envelope.insert(QStringLiteral("sequence"), static_cast<qint64>(sequence));
    envelope.insert(QStringLiteral("timestamp_utc"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    envelope.insert(QStringLiteral("elapsed_ms"), m_clock.elapsed());
    envelope.insert(QStringLiteral("source"), source);
    envelope.insert(QStringLiteral("type"), type);
    envelope.insert(QStringLiteral("data"), data);

    QByteArray line = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    line.append('\n');
    m_buffer.append(std::move(line));

    if (m_buffer.size() >= kMaxBufferedLines) {
        writeBufferedLocked();
    }
    return sequence;
}

void UxEventStream::flush() {
    QMutexLocker locker(&m_mutex);
    if (!m_open) {
        return;
    }
    writeBufferedLocked();
}

void UxEventStream::close() {
    QMutexLocker locker(&m_mutex);
    if (!m_open) {
        return;
    }
    writeBufferedLocked();
    m_file.close();
    m_open = false;
}

qint64 UxEventStream::elapsedMs() const {
    QMutexLocker locker(&m_mutex);
    return m_open ? m_clock.elapsed() : 0;
}

quint64 UxEventStream::eventCount() const {
    QMutexLocker locker(&m_mutex);
    return m_sequence;
}

void UxEventStream::writeBufferedLocked() {
    if (m_buffer.isEmpty()) {
        return;
    }
    for (const QByteArray &line : m_buffer) {
        m_file.write(line);
    }
    m_buffer.clear();
    // Push through Qt's buffer to the OS so a crash after this point
    // loses nothing already flushed. (No fsync: the recorder favours
    // cheap frequent flushes over durability against power loss.)
    m_file.flush();
}

} // namespace trailer
