#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QPixmap>
#include <QSize>

namespace trailer {

class IDocument;

class ThumbnailModel : public QAbstractListModel {
    Q_OBJECT

public:
    explicit ThumbnailModel(QObject* parent = nullptr);

    void setDocument(IDocument* doc);
    void setThumbnailSize(QSize size);
    QSize thumbnailSize() const { return m_size; }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

private:
    IDocument* m_doc = nullptr;
    QSize m_size{128, 160};
    mutable QHash<int, QPixmap> m_cache;
};

}  // namespace trailer
