#pragma once

#include <QAbstractListModel>
#include <QCache>
#include <QPixmap>
#include <QSize>

namespace trailer {

class IDocument;

class ThumbnailModel : public QAbstractListModel {
    Q_OBJECT

  public:
    // Custom item roles. AspectRole exposes each page's width/height ratio
    // (from IDocument::pageSizeHint, no render) so ThumbnailDelegate can
    // size a row's height from the column width without decoding a pixmap.
    enum Roles {
        AspectRole = Qt::UserRole + 1,
    };

    explicit ThumbnailModel(QObject *parent = nullptr);

    void setDocument(IDocument *doc);
    void refresh();
    void setThumbnailSize(QSize size);
    QSize thumbnailSize() const { return m_size; }

    // Lightweight render-width update for sidebar resizes. Unlike
    // setThumbnailSize (which does a full model reset and would drop the
    // selection/scroll on every resize settle), this only re-renders: it
    // clears the pixmap cache and emits dataChanged for DecorationRole,
    // leaving row identity — and thus selection — intact. `w` is the
    // logical column width in pixels; the stored box height is generous
    // (w*2) so tall portrait pages render at full resolution. No-op when
    // the width barely changed (see the 8 px hysteresis in the .cpp).
    void setRenderWidth(int w);

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
    // Cost-bounded LRU of rendered page pixmaps (QCache owns the QPixmap*).
    // The cost unit is kilobytes (each entry's byte size / 1024) and the
    // total budget is set in the constructor — see kThumbCacheBudgetKB in
    // ThumbnailModel.cpp for the magnitude and its rationale. Bounded so a
    // large deck scrolled at a wide (high-render-width) sidebar can't hold
    // gigabytes of thumbnails resident.
    mutable QCache<int, QPixmap> m_cache;
    std::vector<int> m_filter;
};

} // namespace trailer
