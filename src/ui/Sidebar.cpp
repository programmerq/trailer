#include "Sidebar.h"

#include "IconHelper.h"
#include "ThumbnailModel.h"
#include "ThumbnailPaint.h"
#include "annotation/AnnotationStore.h"
#include "document/IDocument.h"
#include "document/PageChangeNotifier.h"
#include "recent/RecentFiles.h"

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
#include <QPalette>
#include <QPixmap>
#include <QResizeEvent>
#include <QTimer>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace trailer {

// Ensure the lightweight SidebarMode enum kept in recent/RecentFiles.h
// stays in lock-step with Sidebar::Mode. Whenever a new mode is added
// either side, this assert fires so the layering boundary is preserved
// without recent/ having to pull in QtWidgets.
static_assert(static_cast<int>(SidebarMode::Hidden) == static_cast<int>(Sidebar::Mode::Hidden),
              "SidebarMode and Sidebar::Mode out of sync");
static_assert(static_cast<int>(SidebarMode::Pages) == static_cast<int>(Sidebar::Mode::Pages),
              "SidebarMode and Sidebar::Mode out of sync");
static_assert(static_cast<int>(SidebarMode::SearchResults) ==
                  static_cast<int>(Sidebar::Mode::SearchResults),
              "SidebarMode and Sidebar::Mode out of sync");
static_assert(static_cast<int>(SidebarMode::TableOfContents) ==
                  static_cast<int>(Sidebar::Mode::TableOfContents),
              "SidebarMode and Sidebar::Mode out of sync");
static_assert(static_cast<int>(SidebarMode::HighlightsAndNotes) ==
                  static_cast<int>(Sidebar::Mode::HighlightsAndNotes),
              "SidebarMode and Sidebar::Mode out of sync");

namespace {

// Vertical padding around the thumbnail inside each list item. The
// 2026-05 HITL pass shrank the logical thumbnail to ~80x100 and
// moved the page number from a separate text row below the image
// into a corner badge drawn on top of it, so the only padding the
// item needs is breathing room above and below the thumbnail.
constexpr int kThumbVerticalPadding = 4;

// Horizontal gutter between the thumbnail and each edge of the sidebar
// column. The thumbnail is scaled to fill (viewport width − 2×this) and
// left-aligned at this inset, so the page image reads as filling the
// column rather than floating in it. 6 px matches the vertical breathing
// room's scale; tried 0 (thumbnail touches the scrollbar/edge, looks
// cramped) and 12 (visibly wasteful on a narrow sidebar) — 6 is the
// balance. Symptom to change: thumbnails crowd the edge, or leave an
// obvious empty margin.
constexpr int kThumbHorizontalMargin = 6;

class ThumbnailDelegate : public QStyledItemDelegate {
  public:
    explicit ThumbnailDelegate(QListView *view) : QStyledItemDelegate(view), m_view(view) {}

    QSize sizeHint(const QStyleOptionViewItem & /*option*/,
                   const QModelIndex &index) const override {
        // Row height tracks the page aspect at the current column width,
        // so portrait rows are tall and landscape rows short — no fixed
        // 108 px slack. availW is the width the thumbnail is scaled to.
        const int vpW = m_view->viewport()->width();
        const int availW = std::max(16, vpW - 2 * kThumbHorizontalMargin);
        qreal aspect = index.data(ThumbnailModel::AspectRole).toReal();
        if (aspect <= 0.0)
            aspect = 0.8; // legacy 80x100 fallback (see ThumbnailModel::data)
        const int thumbH = int(std::lround(availW / aspect));
        return QSize(vpW, thumbH + 2 * kThumbVerticalPadding);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        painter->save();

        const bool selected = option.state & QStyle::State_Selected;
        if (selected) {
            painter->fillRect(option.rect, option.palette.highlight());
        }

        const QPixmap pm = qvariant_cast<QPixmap>(index.data(Qt::DecorationRole));

        const int availW = option.rect.width() - 2 * kThumbHorizontalMargin;
        const int y = option.rect.top() + kThumbVerticalPadding;
        const int x = option.rect.x() + kThumbHorizontalMargin;
        QRect imageRect(x, y, std::max(0, availW), option.rect.height() - 2 * kThumbVerticalPadding);
        if (!pm.isNull() && availW > 0) {
            // Fill the column width and left-align at the margin, so the
            // thumbnail reads as filling the sidebar rather than floating a
            // small fixed box in its centre. The scaling goes through
            // scaleToLogicalWidth (see ui/ThumbnailPaint.h), which corrects
            // for devicePixelRatio so the thumbnail fills the column in
            // LOGICAL pixels on HiDPI: a raw scaledToWidth(availW) on a
            // Retina (dpr=2) pixmap would paint at availW/2 -- the
            // sidebar-slack bug. It is a no-op at dpr=1.
            const QPixmap scaled = scaleToLogicalWidth(pm, availW);
            painter->drawPixmap(x, y, scaled);
            const QSize drawn = logicalSize(scaled);
            imageRect = QRect(x, y, drawn.width(), drawn.height());

            // Subtle 1 px border around the page so its edge is visible
            // against the sidebar base (and gives UAT a detectable edge).
            painter->setRenderHint(QPainter::Antialiasing, false);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(option.palette.color(QPalette::Mid));
            painter->drawRect(imageRect.adjusted(0, 0, -1, -1));
        }

        // Page-number badge in the lower-right corner of the
        // thumbnail. Drawn here (not as a per-item child widget)
        // so it scales with the thumbnail and costs only the
        // painter's text run per visible item.
        const QString text = index.data(Qt::DisplayRole).toString();
        if (!text.isEmpty() && !pm.isNull()) {
            const QFontMetrics fm(option.fontMetrics);
            const int padX = 4;
            const int padY = 1;
            const int textW = fm.horizontalAdvance(text);
            const int textH = fm.height();
            const int badgeW = textW + 2 * padX;
            const int badgeH = textH + 2 * padY;
            // Tuck the badge inside the lower-right corner of the
            // page image with a 2 px inset so it visually sits
            // "on" the page rather than overflowing its edge.
            const int inset = 2;
            const int badgeX = imageRect.right() - badgeW - inset + 1;
            const int badgeY = imageRect.bottom() - badgeH - inset + 1;
            const QRect badgeRect(badgeX, badgeY, badgeW, badgeH);
            QColor bg(0, 0, 0, 160);
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->drawRoundedRect(badgeRect, 3, 3);
            painter->setPen(Qt::white);
            painter->drawText(badgeRect, Qt::AlignCenter, text);
        }

        painter->restore();
    }

  private:
    QListView *m_view;
};

class ThumbnailListView : public QListView {
  public:
    explicit ThumbnailListView(QWidget *parent = nullptr) : QListView(parent) {
        // Debounce render-width updates: a resize relayouts immediately
        // (so scale-to-width fills instantly) but the crisp re-render at
        // the new column width waits ~120 ms so dragging the splitter
        // doesn't thrash the render cache. Tried 0 ms (re-rendered every
        // intermediate drag width) and 400 ms (visibly late crispness);
        // 120 ms is the settled resize-debounce interval.
        m_renderTimer.setSingleShot(true);
        m_renderTimer.setInterval(120);
        connect(&m_renderTimer, &QTimer::timeout, this, [this]() {
            if (auto *tm = qobject_cast<ThumbnailModel *>(model())) {
                tm->setRenderWidth(m_pendingRenderWidth);
                // Keep iconSize consistent with the render box even though
                // the delegate no longer derives layout from it.
                setIconSize(QSize(m_pendingRenderWidth, m_pendingRenderWidth * 2));
            }
        });
    }

    using MoveHandler = std::function<void(int, int)>;
    void setMoveHandler(MoveHandler h) { m_moveHandler = std::move(h); }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QListView::resizeEvent(event);
        // Immediate layout so per-row sizeHint (and scale-to-width paint)
        // responds to the new width on this frame.
        scheduleDelayedItemsLayout();
        // Clamp the render width to a sane band: [48, 600] px. Below 48 a
        // thumbnail is illegible; above 600 the render cost/pixmap memory
        // outgrows any sidebar a user would actually widen to. Symptom to
        // change: thumbnails blur when the sidebar is very wide (raise the
        // cap) or the app renders needlessly large pixmaps (lower it).
        m_pendingRenderWidth =
            std::clamp(viewport()->width() - 2 * kThumbHorizontalMargin, 48, 600);
        m_renderTimer.start();
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
    QTimer m_renderTimer;
    int m_pendingRenderWidth = 0;
};

} // namespace

// G10 (deference): no visible "Sidebar" caption — a label that describes
// the chrome instead of what it contains is the chrome announcing itself
// (docs/ux-guidelines.md's motivating example for this gate; DR
// 2026-07-31-dock-panel-labels-removed-accessible-names-kept). The panel's
// own content (page thumbnails, search results, table of contents) already
// makes its purpose obvious.
//
// IMPORTANT (verified empirically, not assumed — see the DR above): a
// QDockWidget's built-in accessibility interface reads windowTitle()
// DIRECTLY for its screen-reader Name, NOT accessibleName() — unlike
// almost every other widget (QPushButton, QGroupBox, ...), which check
// accessibleName() first. Blanking windowTitle() and calling
// setAccessibleName() instead — the obvious-looking fix — silently
// produces an EMPTY accessible name (confirmed with a standalone
// QAccessible::queryAccessibleInterface probe against this Qt build), the
// exact "cleanup that quietly breaks screen-reader users" trap this change
// has to avoid. So windowTitle() stays "Sidebar" (below, unchanged) —
// keeping the native accessibility path intact — and only the VISUAL
// caption is replaced with a textless custom title bar (a stretch + the
// existing close button, re-created from the platform style, shared with
// Inspector — buildTextlessDockTitleBar(), IconHelper.{h,cpp}) via
// setTitleBarWidget() further down. setAccessibleName() is also set, as
// defence-in-depth for any AT bridge that reads the property directly
// rather than through Qt's interface, but it is not what makes this work.
//
// Spatial constancy (G10): the custom title bar keeps the same features
// (DockWidgetMovable | DockWidgetClosable — Sidebar does NOT carry
// DockWidgetFloatable, so the shared helper omits the float button for it;
// the close button still works, Qt forwards title-bar-area mouse handling
// to a custom title bar widget same as the native one) and a comparable
// strip height, so the sidebar's CONTENT (m_stack, set as the dock's
// central widget) does not shift position — this is a direct, deliberate
// consequence of replacing the caption row's content, not a reflow
// triggered by unrelated state.
Sidebar::Sidebar(QWidget *parent) : QDockWidget(tr("Sidebar"), parent) {
    setObjectName(QStringLiteral("trailer.sidebar"));
    setAccessibleName(tr("Sidebar"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    setTitleBarWidget(buildTextlessDockTitleBar(this));

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
}

void Sidebar::setDocument(IDocument *doc) {
    m_doc = doc;
    if (doc && doc->supportsThumbnails() && doc->pageCount() > 0) {
        m_model->setDocument(doc);
        m_stack->setCurrentIndex(m_tabsIndex);
        syncSelectionFromDocument();
        // Follow the document's current page via its real page-changed signal
        // instead of polling. PdfDocument fires this from the navigator, so
        // keyboard paging, thumbnail jumps, and continuous-scroll page
        // crossings all keep the thumbnail selection in sync. UniqueConnection
        // guards against duplicate connections on repeated setDocument calls.
        if (auto *notifier = doc->pageChangeNotifier()) {
            connect(notifier, &PageChangeNotifier::currentPageChanged, this,
                    &Sidebar::syncSelectionFromDocument, Qt::UniqueConnection);
        }
    } else {
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
        // Route changed() through the debouncer rather than calling
        // refreshAnnotations directly. The store emits one changed()
        // per mutation, and an undo / Cmd+A delete fan-out can fire
        // hundreds back-to-back; the QListWidget rebuild dominates if
        // we run it on every emit.
        connect(store, &AnnotationStore::changed, this, &Sidebar::scheduleAnnotationRefresh,
                Qt::UniqueConnection);
    }
    invalidateHighlightsAndNotesCache();
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

void Sidebar::scheduleAnnotationRefresh() {
    // First emit in this event-loop tick: schedule a 0-ms single-
    // shot that drains all subsequent emits into one rebuild. The
    // highlights count cache is invalidated synchronously so any
    // listener calling highlightsAndNotesCount() before the
    // singleShot fires still gets a fresh count.
    invalidateHighlightsAndNotesCache();
    if (m_annotationRefreshScheduled)
        return;
    m_annotationRefreshScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_annotationRefreshScheduled = false;
        refreshAnnotations();
    });
}

void Sidebar::invalidateHighlightsAndNotesCache() {
    m_highlightsAndNotesCountCache = -1;
}

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
    // Cached result; -1 means "stale, recompute". MainWindow polls
    // this on every annotation store changed() signal, so a burst of
    // 50+ emits during an undo of a long drag would otherwise rescan
    // m_annotations 50+ times. invalidateHighlightsAndNotesCache()
    // hooks into scheduleAnnotationRefresh so the cache stays in
    // sync with the list widget rebuild.
    if (m_highlightsAndNotesCountCache >= 0)
        return m_highlightsAndNotesCountCache;
    if (!m_doc) {
        m_highlightsAndNotesCountCache = 0;
        return 0;
    }
    auto *store = m_doc->annotations();
    if (!store) {
        m_highlightsAndNotesCountCache = 0;
        return 0;
    }
    int count = 0;
    for (const Annotation &a : store->annotations()) {
        if (isHighlightOrNoteType(a.type))
            ++count;
    }
    m_highlightsAndNotesCountCache = count;
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
