#include "Sidebar.h"

#include "ThumbnailModel.h"
#include "annotation/AnnotationStore.h"
#include "document/IDocument.h"

#include <QAbstractItemModel>
#include <QDropEvent>
#include <QFontMetrics>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTreeView>
#include <QVBoxLayout>

#include <functional>

namespace trailer {

namespace {

class ThumbnailDelegate : public QStyledItemDelegate {
  public:
    explicit ThumbnailDelegate(QListView *view) : QStyledItemDelegate(view), m_view(view) {}

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex & /*index*/) const override {
        const QSize icon = m_view->iconSize();
        const int h = icon.height() + option.fontMetrics.height() + 16;
        return QSize(m_view->viewport()->width(), h);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        painter->save();

        const bool selected = option.state & QStyle::State_Selected;
        if (selected) {
            painter->fillRect(option.rect, option.palette.highlight());
        }

        const QSize iconSize = m_view->iconSize();
        const QPixmap pm = qvariant_cast<QPixmap>(index.data(Qt::DecorationRole));

        int y = option.rect.top() + 6;
        int pixmapBottom = y + iconSize.height();
        if (!pm.isNull()) {
            const QPixmap scaled =
                pm.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const int x = option.rect.x() + (option.rect.width() - scaled.width()) / 2;
            painter->drawPixmap(x, y, scaled);
            pixmapBottom = y + scaled.height();
        }

        const QString text = index.data(Qt::DisplayRole).toString();
        painter->setPen(selected ? option.palette.highlightedText().color()
                                 : option.palette.text().color());
        const QRect textRect(option.rect.x(), pixmapBottom + 2, option.rect.width(),
                             option.fontMetrics.height());
        painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, text);

        painter->restore();
    }

  private:
    QListView *m_view;
};

class ThumbnailListView : public QListView {
  public:
    using QListView::QListView;

    using MoveHandler = std::function<void(int, int)>;
    void setMoveHandler(MoveHandler h) { m_moveHandler = std::move(h); }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QListView::resizeEvent(event);
        scheduleDelayedItemsLayout();
    }

    void dropEvent(QDropEvent *event) override {
        if (!m_moveHandler || event->source() != this) {
            QListView::dropEvent(event);
            return;
        }
        const auto selected = selectionModel()->selectedIndexes();
        if (selected.size() != 1) {
            event->ignore();
            return;
        }
        const int from = selected.first().row();

        const QModelIndex dropIndex = indexAt(event->position().toPoint());
        int to = dropIndex.isValid() ? dropIndex.row() : model()->rowCount() - 1;
        switch (dropIndicatorPosition()) {
        case QAbstractItemView::AboveItem:
            // insert before drop target
            break;
        case QAbstractItemView::BelowItem:
            to += 1;
            break;
        case QAbstractItemView::OnItem:
            // treat as before the target
            break;
        case QAbstractItemView::OnViewport:
            to = model()->rowCount();
            break;
        }
        if (to > from) {
            to -= 1; // account for the source being removed first
        }
        if (to < 0)
            to = 0;
        const int last = model()->rowCount() - 1;
        if (to > last)
            to = last;

        if (from != to) {
            m_moveHandler(from, to);
        }
        event->acceptProposedAction();
    }

  private:
    MoveHandler m_moveHandler;
};

} // namespace

Sidebar::Sidebar(QWidget *parent) : QDockWidget(tr("Sidebar"), parent) {
    setObjectName(QStringLiteral("trailer.sidebar"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);

    m_stack = new QStackedWidget(this);

    auto *placeholder = new QWidget(m_stack);
    auto *placeholderLayout = new QVBoxLayout(placeholder);
    auto *placeholderLabel = new QLabel(tr("Open a document to see its pages here."), placeholder);
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setWordWrap(true);
    placeholderLayout->addWidget(placeholderLabel);
    placeholderLayout->addStretch();
    m_placeholderIndex = m_stack->addWidget(placeholder);

    m_model = new ThumbnailModel(this);
    m_thumbnails = new ThumbnailListView(m_stack);
    m_thumbnails->setModel(m_model);
    m_thumbnails->setItemDelegate(new ThumbnailDelegate(m_thumbnails));
    m_thumbnails->setViewMode(QListView::ListMode);
    m_thumbnails->setFlow(QListView::TopToBottom);
    m_thumbnails->setMovement(QListView::Static);
    m_thumbnails->setResizeMode(QListView::Adjust);
    m_thumbnails->setUniformItemSizes(false);
    m_thumbnails->setIconSize(m_model->thumbnailSize());
    m_thumbnails->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_thumbnails->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_thumbnails->setDragEnabled(true);
    m_thumbnails->setAcceptDrops(true);
    m_thumbnails->setDropIndicatorShown(true);
    m_thumbnails->setDragDropMode(QAbstractItemView::DragDrop);
    m_thumbnails->setDefaultDropAction(Qt::MoveAction);
    static_cast<ThumbnailListView *>(m_thumbnails)->setMoveHandler([this](int from, int to) {
        emit movePageRequested(from, to);
    });
    m_thumbnails->installEventFilter(this);
    connect(m_thumbnails->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &) {
                if (m_syncingSelection)
                    return;
                onThumbnailActivated(current);
            });
    m_annotations = new QListWidget(m_stack);
    m_annotations->setSelectionMode(QAbstractItemView::SingleSelection);
    m_annotations->setWordWrap(true);
    connect(m_annotations, &QListWidget::itemActivated, this, &Sidebar::onAnnotationActivated);
    connect(m_annotations, &QListWidget::itemClicked, this, &Sidebar::onAnnotationActivated);

    // Pages-thumbnails tab. The 2026-04-30 HITL pass removed the
    // legacy "Annotations" sibling tab from this strip; that view
    // now lives on its own stack page so it can be reached via the
    // sidebar mode picker's "Highlights & Notes" entry.
    m_tabs = new QTabWidget(m_stack);
    m_tabs->addTab(m_thumbnails, tr("Pages"));
    m_tabs->setTabBarAutoHide(true); // single tab → no tab strip
    m_tabsIndex = m_stack->addWidget(m_tabs);
    m_annotationsIndex = m_stack->addWidget(m_annotations);

    // Outline / TOC tree view. Hidden until the active document
    // exposes an outline (PDF /Outlines tree) AND the picker mode
    // is set to TableOfContents. The model is supplied by the
    // document; sidebar stays ignorant of QPdfBookmarkModel.
    m_outline = new QTreeView(m_stack);
    m_outline->setHeaderHidden(true);
    m_outline->setUniformRowHeights(true);
    m_outline->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_outline->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_outline->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_outline, &QTreeView::activated, this, [this](const QModelIndex &idx) {
        if (m_doc && idx.isValid()) {
            m_doc->goToOutlineEntry(idx);
        }
    });
    connect(m_outline, &QTreeView::clicked, this, [this](const QModelIndex &idx) {
        if (m_doc && idx.isValid()) {
            m_doc->goToOutlineEntry(idx);
        }
    });
    m_outlineIndex = m_stack->addWidget(m_outline);

    m_stack->setCurrentIndex(m_placeholderIndex);
    setWidget(m_stack);

    m_pageSyncTimer.setInterval(120);
    connect(&m_pageSyncTimer, &QTimer::timeout, this, &Sidebar::syncSelectionFromDocument);
}

void Sidebar::setDocument(IDocument *doc) {
    m_doc = doc;
    if (doc && doc->supportsThumbnails() && doc->pageCount() > 0) {
        m_model->setDocument(doc);
        m_stack->setCurrentIndex(m_tabsIndex);
        syncSelectionFromDocument();
        m_pageSyncTimer.start();
    } else {
        m_pageSyncTimer.stop();
        m_model->setDocument(nullptr);
        // Annotations-only fallback removed with the Annotations
        // tab — nothing to show until the Highlights & Notes mode
        // is implemented.
        m_stack->setCurrentIndex(m_placeholderIndex);
    }
    // Bind the outline tree to whatever model the document exposes.
    // setModel(nullptr) on a doc without an outline keeps the view
    // safely empty (we never switch to TableOfContents mode for it
    // anyway, but the picker can still toggle there if the user
    // clicks the disabled entry).
    m_outline->setModel(doc ? doc->outlineModel() : nullptr);
    m_outline->expandAll();
    if (auto *store = doc ? doc->annotations() : nullptr) {
        connect(store, &AnnotationStore::changed, this, &Sidebar::refreshAnnotations,
                Qt::UniqueConnection);
    }
    refreshAnnotations();
    // Re-apply the current mode so a doc swap (or removing the
    // doc) honours the picker's last choice. Hidden stays hidden;
    // SearchResults reverts to no-filter when the new doc has no
    // pending matches yet.
    applyMode();
}

void Sidebar::setMode(Mode mode) {
    if (mode == m_mode) {
        applyMode();
        return;
    }
    m_mode = mode;
    applyMode();
    emit modeChanged(m_mode);
}

void Sidebar::setSearchMatchPages(const std::vector<int> &pages) {
    m_searchMatchPages = pages;
    if (m_mode == Mode::SearchResults)
        applyMode();
}

void Sidebar::applyMode() {
    switch (m_mode) {
    case Mode::Hidden:
        m_model->setPageFilter({});
        if (m_doc) {
            m_stack->setCurrentIndex(m_tabsIndex);
        }
        if (isVisible())
            hide();
        return;
    case Mode::Pages:
        m_model->setPageFilter({});
        m_stack->setCurrentIndex(m_doc ? m_tabsIndex : m_placeholderIndex);
        if (!isVisible())
            show();
        return;
    case Mode::SearchResults:
        m_model->setPageFilter(m_searchMatchPages);
        m_stack->setCurrentIndex(m_doc ? m_tabsIndex : m_placeholderIndex);
        if (!isVisible())
            show();
        return;
    case Mode::TableOfContents:
        // Hand over to the outline tree. If the doc has no
        // outline, the QTreeView shows an empty area; the
        // picker entry is gated on hasOutline() in MainWindow
        // so this branch normally only runs for documents with
        // an outline available.
        m_stack->setCurrentIndex(m_doc && m_doc->outlineModel() ? m_outlineIndex
                                                                : m_placeholderIndex);
        if (!isVisible())
            show();
        return;
    case Mode::HighlightsAndNotes:
        // Rebuild the filtered list now in case mode changed
        // without a store->changed signal firing recently.
        refreshAnnotations();
        m_stack->setCurrentIndex(m_doc ? m_annotationsIndex : m_placeholderIndex);
        if (!isVisible())
            show();
        return;
    }
}

bool Sidebar::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_thumbnails && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) {
            const auto selected = m_thumbnails->selectionModel()->selectedIndexes();
            if (!selected.isEmpty()) {
                std::vector<int> pages;
                pages.reserve(static_cast<size_t>(selected.size()));
                for (const QModelIndex &idx : selected) {
                    const int p = m_model->pageForRow(idx.row());
                    if (p >= 0)
                        pages.push_back(p);
                }
                emit deletePagesRequested(pages);
                return true;
            }
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

void Sidebar::refreshThumbnails() {
    if (!m_doc)
        return;
    m_model->refresh();
    syncSelectionFromDocument();
}

namespace {

// Highlights & Notes mode filters the annotation list down to the
// types that carry user-meaningful content. Pure-shape annotations
// (Rectangle, Ellipse, Line, Arrow, Ink, HighlightShape, ZoomLens,
// Redaction, Signature) would clutter the list without contributing
// to the "skim what I marked up" use case. Speech bubbles are
// included because they carry user text; redactions are not because
// they're a content-destruction tool, not an annotation in the
// review-this-document sense.
bool isHighlightOrNoteType(AnnotationType t) {
    switch (t) {
    case AnnotationType::Highlight:
    case AnnotationType::Underline:
    case AnnotationType::StrikeOut:
    case AnnotationType::Note:
    case AnnotationType::Text:
    case AnnotationType::SpeechBubble:
        return true;
    case AnnotationType::Rectangle:
    case AnnotationType::Ellipse:
    case AnnotationType::Line:
    case AnnotationType::Arrow:
    case AnnotationType::Ink:
    case AnnotationType::HighlightShape:
    case AnnotationType::ZoomLens:
    case AnnotationType::Redaction:
    case AnnotationType::Signature:
        return false;
    }
    return false;
}

} // namespace

void Sidebar::refreshAnnotations() {
    m_annotations->clear();
    if (!m_doc)
        return;
    auto *store = m_doc->annotations();
    if (!store)
        return;
    const bool filterForHighlights = (m_mode == Mode::HighlightsAndNotes);
    for (const Annotation &a : store->annotations()) {
        if (filterForHighlights && !isHighlightOrNoteType(a.type))
            continue;
        QString label;
        switch (a.type) {
        case AnnotationType::Rectangle:
            label = tr("Rectangle");
            break;
        case AnnotationType::Ellipse:
            label = tr("Ellipse");
            break;
        case AnnotationType::Line:
            label = tr("Line");
            break;
        case AnnotationType::Arrow:
            label = tr("Arrow");
            break;
        case AnnotationType::Ink:
            label = tr("Freehand");
            break;
        case AnnotationType::Text:
            label = tr("Text");
            break;
        case AnnotationType::Note:
            label = tr("Note");
            break;
        case AnnotationType::Highlight:
            label = tr("Highlight");
            break;
        case AnnotationType::Underline:
            label = tr("Underline");
            break;
        case AnnotationType::StrikeOut:
            label = tr("Strikeout");
            break;
        case AnnotationType::HighlightShape:
            label = tr("Hl Shape");
            break;
        case AnnotationType::SpeechBubble:
            label = tr("Speech Bubble");
            break;
        case AnnotationType::ZoomLens:
            label = tr("Zoom Lens");
            break;
        case AnnotationType::Redaction:
            label = tr("Redaction");
            break;
        case AnnotationType::Signature:
            label = tr("Signature");
            break;
        }
        const QString preview =
            a.text.isEmpty() ? QString()
                             : QStringLiteral(" — %1").arg(a.text.left(60).replace('\n', ' '));
        auto *item = new QListWidgetItem(tr("p.%1  %2%3").arg(a.page + 1).arg(label).arg(preview),
                                         m_annotations);
        item->setData(Qt::UserRole, a.id);
    }
}

int Sidebar::highlightsAndNotesCount() const {
    if (!m_doc)
        return 0;
    auto *store = m_doc->annotations();
    if (!store)
        return 0;
    int count = 0;
    for (const Annotation &a : store->annotations()) {
        if (isHighlightOrNoteType(a.type))
            ++count;
    }
    return count;
}

void Sidebar::onAnnotationActivated() {
    auto *item = m_annotations->currentItem();
    if (!item || !m_doc)
        return;
    const int id = item->data(Qt::UserRole).toInt();
    auto *store = m_doc->annotations();
    if (!store)
        return;
    if (const Annotation *a = store->find(id)) {
        m_doc->goToPage(a->page);
    }
    emit annotationSelected(id);
}

void Sidebar::onThumbnailActivated(const QModelIndex &index) {
    if (!m_doc || !index.isValid()) {
        return;
    }
    const int page = m_model->pageForRow(index.row());
    if (page < 0)
        return;
    m_doc->goToPage(page);
}

void Sidebar::syncSelectionFromDocument() {
    if (!m_doc || !m_doc->supportsThumbnails()) {
        return;
    }
    const int current = m_doc->currentPage();
    const QModelIndex currentIdx = m_thumbnails->currentIndex();
    if (currentIdx.isValid() && m_model->pageForRow(currentIdx.row()) == current) {
        return;
    }
    // Find the row that maps to the document's current page. With
    // a filter active, the current page may not be in the filtered
    // set — leave the selection alone in that case.
    int targetRow = -1;
    if (m_model->isFiltered()) {
        const int total = m_model->rowCount({});
        for (int r = 0; r < total; ++r) {
            if (m_model->pageForRow(r) == current) {
                targetRow = r;
                break;
            }
        }
        if (targetRow < 0)
            return;
    } else {
        targetRow = current;
    }
    const QModelIndex target = m_model->index(targetRow, 0);
    if (!target.isValid())
        return;
    m_syncingSelection = true;
    m_thumbnails->setCurrentIndex(target);
    m_thumbnails->scrollTo(target, QAbstractItemView::EnsureVisible);
    m_syncingSelection = false;
}

} // namespace trailer
