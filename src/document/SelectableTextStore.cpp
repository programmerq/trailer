#include "SelectableTextStore.h"

#include <QImage>

namespace trailer {

const std::vector<OcrEngine::TextBlock> SelectableTextStore::kEmpty;

SelectableTextStore::SelectableTextStore(QObject *parent) : QObject(parent) {}

bool SelectableTextStore::hasResults(int pageIndex) const {
    return m_entries.find(pageIndex) != m_entries.end();
}

const std::vector<OcrEngine::TextBlock> &SelectableTextStore::blocks(int pageIndex) const {
    auto it = m_entries.find(pageIndex);
    if (it == m_entries.end())
        return kEmpty;
    return it->second.blocks;
}

void SelectableTextStore::put(int pageIndex, std::uint64_t contentHash,
                              std::vector<OcrEngine::TextBlock> blocks) {
    PageEntry entry;
    entry.contentHash = contentHash;
    entry.blocks = std::move(blocks);
    m_entries[pageIndex] = std::move(entry);
    emit pageChanged(pageIndex);
    emit changed();
}

void SelectableTextStore::invalidate(int pageIndex) {
    auto it = m_entries.find(pageIndex);
    if (it == m_entries.end())
        return;
    m_entries.erase(it);
    emit pageChanged(pageIndex);
    emit changed();
}

void SelectableTextStore::clear() {
    if (m_entries.empty())
        return;
    m_entries.clear();
    emit changed();
}

std::uint64_t SelectableTextStore::contentHashFor(int pageIndex) const {
    auto it = m_entries.find(pageIndex);
    if (it == m_entries.end())
        return 0;
    return it->second.contentHash;
}

// FNV-1a 64-bit on the raw pixel scanlines. We deliberately convert to
// a known format (Format_RGB888) so the hash is stable across the
// memory layouts QImage can produce internally. Image dimensions are
// folded in first so two same-pixel images at different sizes don't
// collide.
std::uint64_t hashImageContent(const QImage &image) {
    if (image.isNull())
        return 0;
    const QImage canonical = image.format() == QImage::Format_RGB888
                                 ? image
                                 : image.convertToFormat(QImage::Format_RGB888);
    constexpr std::uint64_t kPrime = 0x100000001b3ULL;
    constexpr std::uint64_t kOffset = 0xcbf29ce484222325ULL;
    std::uint64_t h = kOffset;
    auto mix = [&h](std::uint8_t b) {
        h ^= static_cast<std::uint64_t>(b);
        h *= kPrime;
    };
    const int w = canonical.width();
    const int h32 = canonical.height();
    auto mixInt = [&mix](int v) {
        for (int i = 0; i < 4; ++i) {
            mix(static_cast<std::uint8_t>((v >> (i * 8)) & 0xff));
        }
    };
    mixInt(w);
    mixInt(h32);
    const int bytesPerLine = w * 3;
    for (int y = 0; y < h32; ++y) {
        const uchar *scan = canonical.constScanLine(y);
        for (int x = 0; x < bytesPerLine; ++x) {
            mix(scan[x]);
        }
    }
    // Avoid 0 because callers use it as "no entry" sentinel. The
    // probability of FNV emitting 0 is negligible but folding to 1
    // makes the contract explicit.
    return h == 0 ? 1ULL : h;
}

} // namespace trailer
