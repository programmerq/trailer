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
    explicit ThumbnailModel(QObject *parent = nullptr);

    void setDocument(IDocument *doc);
    void refresh();
    void setThumbnailSize(QSize size);
    QSize thumbnailSize() const { return m_size; }

    // When non-empty, the model only reports rows matching the
    // listed page indices (in document order). Used by the
    // sidebar's SearchResults mode. Empty vector disables the
    // filter — the model returns every page in pageCount() order.
    void setPageFilter(const std::vector<int> &pages);
    bool isFiltered() const { return !m_filter.empty(); }
    // Map a row index in the filtered view back to the underlying
    // document page index. Returns row when no filter is active.
    int pageForRow(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;

  private:
    IDocument *m_doc = nullptr;
    // Logical thumbnail size used by ThumbnailDelegate. Shrunk from
    // 128x160 in the 2026-05 HITL pass to roughly double sidebar
    // density. Preserves the original 0.8 aspect (matches Letter
    // /A4 close enough that KeepAspectRatio leaves nothing pinned
    // to one edge), and pairs with the page-number badge drawn
    // inside the thumbnail (no separate text row below).
    QSize m_size{80, 100};
    mutable QHash<int, QPixmap> m_cache;
    std::vector<int> m_filter;
};

} // namespace trailer
