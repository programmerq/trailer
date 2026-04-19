#include "DocumentView.h"

namespace trailer {

DocumentView::DocumentView(QWidget* parent) : QTabWidget(parent) {
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);
    connect(this, &QTabWidget::tabCloseRequested, this, &DocumentView::onTabCloseRequested);
    connect(this, &QTabWidget::currentChanged, this, [this](int) {
        if (QWidget* w = currentWidget()) {
            w->setFocus();
        }
        emit currentDocumentChanged(currentDocument());
    });
}

IDocument* DocumentView::currentDocument() const {
    const int index = currentIndex();
    if (index < 0 || index >= static_cast<int>(m_documents.size())) {
        return nullptr;
    }
    return m_documents[static_cast<size_t>(index)].get();
}

void DocumentView::addDocument(std::unique_ptr<IDocument> document) {
    if (!document) {
        return;
    }
    QWidget* view = document->createView(this);
    const QString label = document->displayName();
    const QString tip = document->filePath();

    m_documents.push_back(std::move(document));
    const int index = addTab(view, label);
    if (!tip.isEmpty()) {
        setTabToolTip(index, tip);
    }
    setCurrentIndex(index);
}

void DocumentView::onTabCloseRequested(int index) {
    if (index < 0 || index >= static_cast<int>(m_documents.size())) {
        return;
    }
    QWidget* view = widget(index);
    removeTab(index);
    if (view) {
        view->deleteLater();
    }
    m_documents.erase(m_documents.begin() + index);
    if (m_documents.empty()) {
        emit allTabsClosed();
    }
}

}  // namespace trailer
