#include "ThumbnailModel.h"

#include "document/IDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QPixmap>
#include <QTemporaryFile>
#include <QUrl>

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

    std::vector<int> rows;
    rows.reserve(indexes.size());
    for (const QModelIndex& idx : indexes) {
        if (idx.isValid()) rows.push_back(idx.row());
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
