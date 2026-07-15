#include "ThumbnailModel.h"

#include "document/IDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QPixmap>
#include <QScreen>
#include "util/TempPath.h"
#include <QUrl>

#include <cmath>

#include <algorithm>
#include <cstdlib>

namespace trailer {

namespace {

// Total pixmap-cache budget, expressed in kilobytes (the QCache cost unit
// used here is each entry's byte size / 1024). 256 MB. Rationale: with the
// viewport-driven render size (m_size = {w, w*2}, w clamped to [48,600] in
// Sidebar.cpp) a single 600-px-wide portrait page on a 2x display renders to
// roughly 8-11 MB; a 200-500 page deck fully scrolled would, with the old
// unbounded QHash, hold gigabytes resident. 256 MB caps that with cost-based
// LRU eviction while still keeping a few hundred small thumbnails hot.
// Symptom to change: thumbnails re-render visibly on scroll-back at a wide
// sidebar (raise) or the app's resident memory balloons on huge decks (lower).
constexpr int kThumbCacheBudgetKB = 256 * 1024;

// Cache cost of a rendered pixmap, in kilobytes (matches the budget unit).
// depth() is bits-per-pixel; /8 -> bytes; /1024 -> KB. Floored at 1 so even
// a tiny/empty pixmap consumes a slot and eviction accounting stays sane.
int pixmapCostKB(const QPixmap &pm) {
    const qint64 bytes = qint64(pm.width()) * pm.height() * pm.depth() / 8;
    return int(std::max<qint64>(1, bytes / 1024));
}

} // namespace

ThumbnailModel::ThumbnailModel(QObject *parent) : QAbstractListModel(parent) {
    m_cache.setMaxCost(kThumbCacheBudgetKB);
}

void ThumbnailModel::setDocument(IDocument *doc) {
    beginResetModel();
    m_doc = doc;
    m_cache.clear();
    endResetModel();
}

void ThumbnailModel::refresh() {
    beginResetModel();
    m_cache.clear();
    endResetModel();
}

void ThumbnailModel::setThumbnailSize(QSize size) {
    if (size == m_size) {
        return;
    }
    beginResetModel();
    m_size = size;
    m_cache.clear();
    endResetModel();
}

void ThumbnailModel::setRenderWidth(int w) {
    // Generous height so tall portrait pages render at full resolution
    // when scaled to the column width by the delegate. Height is not a
    // layout constraint (the delegate sizes rows by aspect); it only caps
    // the render so a very tall page still fits the cache pixmap.
    const QSize target(w, w * 2);
    // 8 px hysteresis: skip re-renders for sub-pixel/one-off resize jitter
    // so a slow drag doesn't thrash the render cache. Tried tighter (0 px)
    // — every intermediate drag width re-rendered; 8 px settles cleanly.
    if (std::abs(w - m_size.width()) <= 8) {
        return;
    }
    m_size = target;
    m_cache.clear();
    const int last = rowCount({}) - 1;
    if (last >= 0) {
        // Preserve selection/scroll: re-render in place rather than reset.
        emit dataChanged(index(0), index(last), {Qt::DecorationRole});
    }
}

QStringList ThumbnailModel::mimeTypes() const {
    return {QStringLiteral("application/x-trailer-pages"), QStringLiteral("text/uri-list")};
}

QMimeData *ThumbnailModel::mimeData(const QModelIndexList &indexes) const {
    auto *data = new QMimeData;

    // Convert view-row indices to underlying document pages so
    // drag-out / extract works the same whether the model is
    // showing every page or a search-filtered subset.
    std::vector<int> rows;
    rows.reserve(static_cast<size_t>(indexes.size()));
    for (const QModelIndex &idx : indexes) {
        if (!idx.isValid())
            continue;
        const int page = pageForRow(idx.row());
        if (page >= 0)
            rows.push_back(page);
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    QByteArray marker;
    for (int r : rows) {
        marker += QByteArray::number(r) + '\n';
    }
    data->setData(QStringLiteral("application/x-trailer-pages"), marker);

    if (m_doc && !rows.empty() && m_doc->supportsEditing()) {
        const QString base = QFileInfo(m_doc->filePath()).completeBaseName();
        const QString hint = base.isEmpty() ? QStringLiteral("pages") : base;
        // makeUniqueTempPath (not QTemporaryFile) so qpdf can open the
        // path for writing on Windows. The caller of mimeData() —
        // typically a Qt drag-out target — takes ownership of the file
        // we hand off via setUrls; we only remove on the extract
        // failure path. See util/TempPath.h.
        const QString path = makeUniqueTempPath(hint + QStringLiteral("-XXXXXX.pdf"));
        if (!path.isEmpty()) {
            if (m_doc->extractPages(rows, path)) {
                data->setUrls({QUrl::fromLocalFile(path)});
            } else {
                QFile::remove(path);
            }
        }
    }
    return data;
}

Qt::ItemFlags ThumbnailModel::flags(const QModelIndex &index) const {
    Qt::ItemFlags f = QAbstractListModel::flags(index);
    if (index.isValid()) {
        f |= Qt::ItemIsDragEnabled;
    }
    return f;
}

int ThumbnailModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid() || !m_doc || !m_doc->supportsThumbnails()) {
        return 0;
    }
    if (!m_filter.empty())
        return static_cast<int>(m_filter.size());
    return m_doc->pageCount();
}

void ThumbnailModel::setPageFilter(const std::vector<int> &pages) {
    if (pages == m_filter)
        return;
    beginResetModel();
    m_filter = pages;
    endResetModel();
}

int ThumbnailModel::pageForRow(int row) const {
    if (m_filter.empty())
        return row;
    if (row < 0 || row >= static_cast<int>(m_filter.size()))
        return -1;
    return m_filter[static_cast<size_t>(row)];
}

QVariant ThumbnailModel::data(const QModelIndex &index, int role) const {
    if (!m_doc || !index.isValid()) {
        return {};
    }
    const int row = index.row();
    // Map row → underlying page when a filter is active. The
    // cache is keyed on the page index so cross-filter switches
    // don't re-render the same page.
    const int page = pageForRow(row);
    if (page < 0 || page >= m_doc->pageCount()) {
        return {};
    }
    switch (role) {
    case Qt::DisplayRole:
        return QString::number(page + 1);
    case AspectRole: {
        // width/height of the page, computed without rendering. Falls back
        // to 0.8 — the legacy 80x100 logical box ratio — when the document
        // can't supply a size (null size / unsupported adapter), so the
        // delegate always has a usable aspect.
        const QSizeF hint = m_doc->pageSizeHint(page);
        if (hint.isEmpty() || hint.height() <= 0.0) {
            return qreal(0.8);
        }
        return qreal(hint.width() / hint.height());
    }
    case Qt::DecorationRole: {
        if (const QPixmap *cached = m_cache.object(page)) {
            return *cached;
        }
        // Render at native resolution for the user's primary
        // screen and stamp devicePixelRatio on the result so
        // Qt treats the pixmap as logical m_size while
        // sampling the high-DPI pixels. Without this the
        // sidebar thumbnail looks blurry on Retina.
        qreal dpr = 1.0;
        if (auto *screen = QGuiApplication::primaryScreen()) {
            dpr = screen->devicePixelRatio();
        }
        const QSize nativeSize(int(std::ceil(m_size.width() * dpr)),
                               int(std::ceil(m_size.height() * dpr)));
        QImage img = m_doc->renderThumbnail(page, nativeSize);
        if (!img.isNull()) {
            img.setDevicePixelRatio(dpr);
        }
        const QPixmap pm = img.isNull() ? QPixmap() : QPixmap::fromImage(img);
        // Cache a copy under a cost-bounded LRU. QCache takes ownership of the
        // pointer and may drop it immediately if the cost exceeds the whole
        // budget, so return the local `pm` value (cheap implicit-share copy)
        // rather than dereferencing the just-inserted pointer. A failed render
        // caches an empty pixmap (cost 1) so the page isn't re-rendered on
        // every repaint — matching the prior QHash sentinel behaviour.
        m_cache.insert(page, new QPixmap(pm), pixmapCostKB(pm));
        return pm;
    }
    case Qt::ToolTipRole:
        return QObject::tr("Page %1").arg(page + 1);
    default:
        return {};
    }
}

} // namespace trailer
