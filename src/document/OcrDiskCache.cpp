#include "OcrDiskCache.h"

#include "settings/AppPaths.h"

#include <QByteArray>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <vector>

namespace trailer {

namespace {

// On-disk format tag. Bump kFormatVersion whenever the serialized layout
// below changes so a file written by an older build is discarded rather
// than mis-read (load() treats a version mismatch as a miss).
constexpr std::uint32_t kMagic = 0x54524f43;    // 'T','R','O','C'
constexpr std::uint32_t kFormatVersion = 1;
// Pin the QDataStream encoding so QPolygon/QString/float wire formats are
// stable across Qt point releases; a file's own version tag guards the
// rest.
constexpr int kStreamVersion = QDataStream::Qt_6_5;

constexpr QLatin1String kEntrySuffix(".ocrtext");

QByteArray serialize(std::uint64_t contentHash,
                     const std::vector<OcrEngine::TextBlock> &blocks) {
    QByteArray buf;
    QDataStream ds(&buf, QIODevice::WriteOnly);
    ds.setVersion(kStreamVersion);
    ds << kMagic << kFormatVersion << static_cast<quint64>(contentHash)
       << static_cast<quint32>(blocks.size());
    for (const auto &b : blocks) {
        ds << b.polygon << b.text << b.confidence;
    }
    return buf;
}

} // namespace

OcrDiskCache::OcrDiskCache(QString cacheDir, std::int64_t ceilingBytes)
    : m_dir(std::move(cacheDir)), m_ceilingBytes(ceilingBytes) {
    QDir().mkpath(m_dir);
    reindexFromDisk();
}

OcrDiskCache::OcrDiskCache() : OcrDiskCache(AppPaths::ocrCacheDir()) {}

QString OcrDiskCache::pathFor(std::uint64_t contentHash) const {
    // 16-hex-digit, zero-padded filename — the content hash is the key.
    const QString name =
        QStringLiteral("%1").arg(contentHash, 16, 16, QLatin1Char('0')) + kEntrySuffix;
    return QDir(m_dir).filePath(name);
}

void OcrDiskCache::reindexFromDisk() {
    m_index.clear();
    m_totalBytes = 0;
    m_seqCounter = 0;

    QDir dir(m_dir);
    const auto infos = dir.entryInfoList({QStringLiteral("*") + kEntrySuffix}, QDir::Files);

    // Oldest-first by mtime, so the initial access sequence approximates
    // real LRU order carried over from the previous run.
    std::vector<QFileInfo> ordered(infos.begin(), infos.end());
    std::sort(ordered.begin(), ordered.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() < b.lastModified();
    });

    for (const QFileInfo &fi : ordered) {
        bool ok = false;
        const std::uint64_t hash = fi.completeBaseName().toULongLong(&ok, 16);
        if (!ok)
            continue; // not one of ours / unparseable name — leave it alone
        Entry e;
        e.size = fi.size();
        e.seq = ++m_seqCounter;
        m_index[hash] = e;
        m_totalBytes += e.size;
    }
    // A directory that arrived already over budget (e.g. a smaller
    // ceiling than a previous run) is trimmed immediately.
    enforceCeiling();
}

std::optional<std::vector<OcrEngine::TextBlock>>
OcrDiskCache::load(std::uint64_t contentHash) {
    auto it = m_index.find(contentHash);
    if (it == m_index.end())
        return std::nullopt;

    const QString path = pathFor(contentHash);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        // Index says it's here but the file vanished — drop the stale
        // accounting and report a miss.
        m_totalBytes -= it->second.size;
        m_index.erase(it);
        return std::nullopt;
    }
    const QByteArray buf = file.readAll();
    file.close();

    QDataStream ds(buf);
    ds.setVersion(kStreamVersion);
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    quint64 storedHash = 0;
    quint32 count = 0;
    ds >> magic >> version >> storedHash >> count;

    auto discardCorrupt = [&]() -> std::optional<std::vector<OcrEngine::TextBlock>> {
        file.remove();
        m_totalBytes -= it->second.size;
        m_index.erase(it);
        return std::nullopt;
    };

    if (ds.status() != QDataStream::Ok || magic != kMagic || version != kFormatVersion ||
        storedHash != static_cast<quint64>(contentHash)) {
        return discardCorrupt();
    }

    std::vector<OcrEngine::TextBlock> blocks;
    blocks.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        OcrEngine::TextBlock b;
        ds >> b.polygon >> b.text >> b.confidence;
        if (ds.status() != QDataStream::Ok)
            return discardCorrupt();
        blocks.push_back(std::move(b));
    }

    // Touch: most-recently-used.
    it->second.seq = ++m_seqCounter;
    return blocks;
}

void OcrDiskCache::store(std::uint64_t contentHash,
                         const std::vector<OcrEngine::TextBlock> &blocks) {
    // A text-less page has nothing worth persisting, and writing an empty
    // entry would let a later load() report a "hit" for a page that has no
    // text — the honesty seam (ADR 0013 §G13.2) lives one layer up, but we
    // reinforce it here by refusing to cache empties.
    if (blocks.empty())
        return;

    QDir().mkpath(m_dir);
    const QByteArray buf = serialize(contentHash, blocks);
    const QString path = pathFor(contentHash);

    // Atomic write: QSaveFile writes to a sibling temp file and renames on
    // commit(), so a crash mid-write never leaves a half-written entry that
    // load() would treat as valid. (We own the write with a Qt file handle
    // and never hand the path to a non-Qt writer, so the Windows
    // QTemporaryFile share-mode gotcha does not apply here.)
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.write(buf);
    if (!out.commit())
        return;

    const std::int64_t newSize = QFileInfo(path).size();
    auto it = m_index.find(contentHash);
    if (it != m_index.end()) {
        m_totalBytes += newSize - it->second.size;
        it->second.size = newSize;
        it->second.seq = ++m_seqCounter;
    } else {
        Entry e;
        e.size = newSize;
        e.seq = ++m_seqCounter;
        m_index[contentHash] = e;
        m_totalBytes += newSize;
    }

    enforceCeiling();
}

void OcrDiskCache::remove(std::uint64_t contentHash) {
    auto it = m_index.find(contentHash);
    if (it == m_index.end())
        return;
    QFile::remove(pathFor(contentHash));
    m_totalBytes -= it->second.size;
    m_index.erase(it);
}

bool OcrDiskCache::contains(std::uint64_t contentHash) const {
    return m_index.find(contentHash) != m_index.end();
}

void OcrDiskCache::enforceCeiling() {
    // Evict least-recently-used entries (smallest access seq first) until
    // the total on-disk size is within the ceiling. Never evict the entry
    // just written past the point of emptiness — the loop stops as soon as
    // the budget is met, and a single entry larger than the whole ceiling
    // is left in place (a degenerate config, not a real workload).
    while (m_totalBytes > m_ceilingBytes && m_index.size() > 1) {
        auto lru = std::min_element(
            m_index.begin(), m_index.end(),
            [](const auto &a, const auto &b) { return a.second.seq < b.second.seq; });
        if (lru == m_index.end())
            break;
        QFile::remove(pathFor(lru->first));
        m_totalBytes -= lru->second.size;
        m_index.erase(lru);
    }
}

} // namespace trailer
