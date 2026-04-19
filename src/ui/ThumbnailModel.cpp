#include "ThumbnailModel.h"

#include "document/IDocument.h"

#include <QImage>
#include <QPixmap>

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
    return m_doc->pageCount();
}

QVariant ThumbnailModel::data(const QModelIndex& index, int role) const {
    if (!m_doc || !index.isValid()) {
        return {};
    }
    const int row = index.row();
    if (row < 0 || row >= m_doc->pageCount()) {
        return {};
    }
    switch (role) {
        case Qt::DisplayRole:
            return QString::number(row + 1);
        case Qt::DecorationRole: {
            auto it = m_cache.find(row);
            if (it == m_cache.end()) {
                const QImage img = m_doc->renderThumbnail(row, m_size);
                it = m_cache.insert(row, img.isNull() ? QPixmap() : QPixmap::fromImage(img));
            }
            return it.value();
        }
        case Qt::ToolTipRole:
            return QObject::tr("Page %1").arg(row + 1);
        default:
            return {};
    }
}

}  // namespace trailer
