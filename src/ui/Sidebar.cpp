#include "Sidebar.h"

#include "ThumbnailModel.h"
#include "document/IDocument.h"

#include <QLabel>
#include <QListView>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace trailer {

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
    m_thumbnails = new QListView(m_stack);
    m_thumbnails->setModel(m_model);
    m_thumbnails->setViewMode(QListView::IconMode);
    m_thumbnails->setFlow(QListView::TopToBottom);
    m_thumbnails->setWrapping(false);
    m_thumbnails->setMovement(QListView::Static);
    m_thumbnails->setResizeMode(QListView::Adjust);
    m_thumbnails->setUniformItemSizes(true);
    m_thumbnails->setSpacing(6);
    m_thumbnails->setIconSize(m_model->thumbnailSize());
    m_thumbnails->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_thumbnails, &QListView::activated,
            this, &Sidebar::onThumbnailActivated);
    connect(m_thumbnails, &QListView::clicked,
            this, &Sidebar::onThumbnailActivated);
    m_thumbnailsIndex = m_stack->addWidget(m_thumbnails);

    m_stack->setCurrentIndex(m_placeholderIndex);
    setWidget(m_stack);
}

void Sidebar::setDocument(IDocument* doc) {
    m_doc = doc;
    if (doc && doc->supportsThumbnails() && doc->pageCount() > 0) {
        m_model->setDocument(doc);
        m_stack->setCurrentIndex(m_thumbnailsIndex);
    } else {
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

}  // namespace trailer
