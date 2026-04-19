#include "Sidebar.h"

#include "ThumbnailModel.h"
#include "document/IDocument.h"

#include <QFontMetrics>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

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

protected:
    void resizeEvent(QResizeEvent* event) override {
        QListView::resizeEvent(event);
        scheduleDelayedItemsLayout();
    }
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
    m_thumbnails->setSelectionMode(QAbstractItemView::SingleSelection);
    m_thumbnails->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
