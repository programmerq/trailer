#include "Sidebar.h"

#include "ThumbnailModel.h"
#include "document/IDocument.h"

#include <QDropEvent>
#include <QFontMetrics>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <functional>

namespace trailer {

namespace {

class ThumbnailDelegate : public QStyledItemDelegate {
public:
    explicit ThumbnailDelegate(QListView* view)
        : QStyledItemDelegate(view), m_view(view) {}

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& /*index*/) const override {
        const QSize icon = m_view->iconSize();
        const int h = icon.height() + option.fontMetrics.height() + 16;
        return QSize(m_view->viewport()->width(), h);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
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
            const QPixmap scaled = pm.scaled(iconSize, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
            const int x = option.rect.x() + (option.rect.width() - scaled.width()) / 2;
            painter->drawPixmap(x, y, scaled);
            pixmapBottom = y + scaled.height();
        }

        const QString text = index.data(Qt::DisplayRole).toString();
        painter->setPen(selected ? option.palette.highlightedText().color()
                                 : option.palette.text().color());
        const QRect textRect(option.rect.x(), pixmapBottom + 2,
                             option.rect.width(), option.fontMetrics.height());
        painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, text);

        painter->restore();
    }

private:
    QListView* m_view;
};

class ThumbnailListView : public QListView {
public:
    using QListView::QListView;

    using MoveHandler = std::function<void(int, int)>;
    void setMoveHandler(MoveHandler h) { m_moveHandler = std::move(h); }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QListView::resizeEvent(event);
        scheduleDelayedItemsLayout();
    }

    void dropEvent(QDropEvent* event) override {
        if (!m_moveHandler) {
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
            to -= 1;  // account for the source being removed first
        }
        if (to < 0) to = 0;
        const int last = model()->rowCount() - 1;
        if (to > last) to = last;

        if (from != to) {
            m_moveHandler(from, to);
        }
        event->acceptProposedAction();
    }

private:
    MoveHandler m_moveHandler;
};

}  // namespace

Sidebar::Sidebar(QWidget* parent) : QDockWidget(tr("Sidebar"), parent) {
    setObjectName(QStringLiteral("trailer.sidebar"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);

    m_stack = new QStackedWidget(this);

    auto* placeholder = new QWidget(m_stack);
    auto* placeholderLayout = new QVBoxLayout(placeholder);
    auto* placeholderLabel = new QLabel(
        tr("Open a document to see its pages here."), placeholder);
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
    m_thumbnails->setDragDropMode(QAbstractItemView::InternalMove);
    m_thumbnails->setDefaultDropAction(Qt::MoveAction);
    static_cast<ThumbnailListView*>(m_thumbnails)->setMoveHandler(
        [this](int from, int to) { emit movePageRequested(from, to); });
    m_thumbnails->installEventFilter(this);
    connect(m_thumbnails->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
                if (m_syncingSelection) return;
                onThumbnailActivated(current);
            });
    m_thumbnailsIndex = m_stack->addWidget(m_thumbnails);

    m_stack->setCurrentIndex(m_placeholderIndex);
    setWidget(m_stack);

    m_pageSyncTimer.setInterval(120);
    connect(&m_pageSyncTimer, &QTimer::timeout,
            this, &Sidebar::syncSelectionFromDocument);
}

void Sidebar::setDocument(IDocument* doc) {
    m_doc = doc;
    if (doc && doc->supportsThumbnails() && doc->pageCount() > 0) {
        m_model->setDocument(doc);
        m_stack->setCurrentIndex(m_thumbnailsIndex);
        syncSelectionFromDocument();
        m_pageSyncTimer.start();
    } else {
        m_pageSyncTimer.stop();
        m_model->setDocument(nullptr);
        m_stack->setCurrentIndex(m_placeholderIndex);
    }
}

bool Sidebar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_thumbnails && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) {
            const auto selected = m_thumbnails->selectionModel()->selectedIndexes();
            if (!selected.isEmpty()) {
                std::vector<int> rows;
                rows.reserve(selected.size());
                for (const QModelIndex& idx : selected) {
                    rows.push_back(idx.row());
                }
                emit deletePagesRequested(rows);
                return true;
            }
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

void Sidebar::refreshThumbnails() {
    if (!m_doc) return;
    m_model->refresh();
    syncSelectionFromDocument();
}

void Sidebar::onThumbnailActivated(const QModelIndex& index) {
    if (!m_doc || !index.isValid()) {
        return;
    }
    m_doc->goToPage(index.row());
}

void Sidebar::syncSelectionFromDocument() {
    if (!m_doc || !m_doc->supportsThumbnails()) {
        return;
    }
    const int current = m_doc->currentPage();
    const QModelIndex currentIdx = m_thumbnails->currentIndex();
    if (current == currentIdx.row()) {
        return;
    }
    const QModelIndex target = m_model->index(current, 0);
    if (!target.isValid()) {
        return;
    }
    m_syncingSelection = true;
    m_thumbnails->setCurrentIndex(target);
    m_thumbnails->scrollTo(target, QAbstractItemView::EnsureVisible);
    m_syncingSelection = false;
}

}  // namespace trailer
