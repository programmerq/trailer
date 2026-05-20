#include "DocumentView.h"

namespace trailer {

DocumentView::DocumentView(QWidget *parent) : QTabWidget(parent) {
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);
    // Window-per-file is the default now, so a single-document window
    // should show no tab strip at all. The user only sees tabs when
    // they have opted into the legacy "new_tab" open-files mode and
    // actually have multiple tabs open. Built-in Qt support: the bar
    // only appears when count() > 1.
    setTabBarAutoHide(true);
    connect(this, &QTabWidget::tabCloseRequested, this, &DocumentView::onTabCloseRequested);
    connect(this, &QTabWidget::currentChanged, this, [this](int) {
        if (QWidget *w = currentWidget()) {
            w->setFocus();
        }
        emit currentDocumentChanged(currentDocument());
    });
}

IDocument *DocumentView::currentDocument() const {
    const int index = currentIndex();
    if (index < 0 || index >= static_cast<int>(m_documents.size())) {
        return nullptr;
    }
    return m_documents[static_cast<size_t>(index)].get();
}

int DocumentView::documentAt(int index, IDocument **out) const {
    if (index < 0 || index >= static_cast<int>(m_documents.size())) {
        if (out)
            *out = nullptr;
        return 0;
    }
    if (out)
        *out = m_documents[static_cast<size_t>(index)].get();
    return 1;
}

void DocumentView::addDocument(std::unique_ptr<IDocument> document) {
    if (!document) {
        return;
    }
    QWidget *view = document->createView(this);
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
    QWidget *view = widget(index);
    removeTab(index);
    // Emit the about-to-be-removed signal before erasing the
    // unique_ptr so listeners can flush state keyed by the doc
    // pointer (cancel any pending MlScheduler tasks, drop cache
    // entries) while the pointer is still valid to compare against.
    IDocument *doc = m_documents[static_cast<size_t>(index)].get();
    emit documentAboutToBeRemoved(doc);
    if (view) {
        view->deleteLater();
    }
    m_documents.erase(m_documents.begin() + index);
    if (m_documents.empty()) {
        emit allTabsClosed();
    }
}

} // namespace trailer
