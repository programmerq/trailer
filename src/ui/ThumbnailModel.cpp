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
#include <QTemporaryFile>
#include <QUrl>

#include <cmath>

#include <algorithm>

namespace trailer {

ThumbnailModel::ThumbnailModel(QObject* parent) : QAbstractListModel(parent) {}

void ThumbnailModel::setDocument(IDocument* doc) {
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

QStringList ThumbnailModel::mimeTypes() const {
    return {QStringLiteral("application/x-trailer-pages"),
            QStringLiteral("text/uri-list")};
}

QMimeData* ThumbnailModel::mimeData(const QModelIndexList& indexes) const {
    auto* data = new QMimeData;

    // Convert view-row indices to underlying document pages so
    // drag-out / extract works the same whether the model is
    // showing every page or a search-filtered subset.
    std::vector<int> rows;
    rows.reserve(indexes.size());
    for (const QModelIndex& idx : indexes) {
        if (!idx.isValid()) continue;
        const int page = pageForRow(idx.row());
        if (page >= 0) rows.push_back(page);
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
        QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/") + hint +
                           QStringLiteral("-XXXXXX.pdf"));
        tmp.setAutoRemove(false);
        if (tmp.open()) {
            const QString path = tmp.fileName();
            tmp.close();
            if (m_doc->extractPages(rows, path)) {
                data->setUrls({QUrl::fromLocalFile(path)});
            } else {
                QFile::remove(path);
            }
        }
    }
    return data;
}

Qt::ItemFlags ThumbnailModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags f = QAbstractListModel::flags(index);
    if (index.isValid()) {
        f |= Qt::ItemIsDragEnabled;
    }
    return f;
}

int ThumbnailModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !m_doc || !m_doc->supportsThumbnails()) {
        return 0;
    }
    if (!m_filter.empty()) return static_cast<int>(m_filter.size());
    return m_doc->pageCount();
}

void ThumbnailModel::setPageFilter(const std::vector<int>& pages) {
    if (pages == m_filter) return;
    beginResetModel();
    m_filter = pages;
    endResetModel();
}

int ThumbnailModel::pageForRow(int row) const {
    if (m_filter.empty()) return row;
    if (row < 0 || row >= static_cast<int>(m_filter.size())) return -1;
    return m_filter[static_cast<size_t>(row)];
}

QVariant ThumbnailModel::data(const QModelIndex& index, int role) const {
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
        case Qt::DecorationRole: {
            auto it = m_cache.find(page);
            if (it == m_cache.end()) {
                // Render at native resolution for the user's primary
                // screen and stamp devicePixelRatio on the result so
                // Qt treats the pixmap as logical m_size while
                // sampling the high-DPI pixels. Without this the
                // sidebar thumbnail looks blurry on Retina.
                qreal dpr = 1.0;
                if (auto* screen = QGuiApplication::primaryScreen()) {
                    dpr = screen->devicePixelRatio();
                }
                const QSize nativeSize(
                    int(std::ceil(m_size.width() * dpr)),
                    int(std::ceil(m_size.height() * dpr)));
                QImage img = m_doc->renderThumbnail(page, nativeSize);
                if (!img.isNull()) {
                    img.setDevicePixelRatio(dpr);
                }
                it = m_cache.insert(page, img.isNull() ? QPixmap() : QPixmap::fromImage(img));
            }
            return it.value();
        }
        case Qt::ToolTipRole:
            return QObject::tr("Page %1").arg(page + 1);
        default:
            return {};
    }
}

}  // namespace trailer
