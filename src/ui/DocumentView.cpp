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
        // Focus still follows the tab during a close; only the
        // announcement is deferred (see m_suppressCurrentDocumentChanged).
        if (m_suppressCurrentDocumentChanged)
            return;
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
    // Read the doc pointer BEFORE any teardown so the veto listener can
    // inspect it (dirty check, save prompt) while the tab and document
    // are still fully intact. If the listener vetoes, we bail without
    // touching the tab bar or the unique_ptr — the unsaved work stays.
    IDocument *doc = m_documents[static_cast<size_t>(index)].get();
    bool veto = false;
    emit documentCloseRequested(doc, &veto);
    if (veto) {
        return;
    }
    // index is reused below after the synchronous emit above. The emit
    // may run a nested modal loop (MainWindow's Save/Discard/Cancel
    // prompt), but QMessageBox::exec is app-modal and doc-adds only
    // append to m_documents, so no re-entrant close can shift or
    // invalidate this index while the modal is up — index stays valid.
    QWidget *view = widget(index);
    // Hold back the currentChanged-driven announcement until m_documents
    // is consistent again — see the emit after the erase below.
    m_suppressCurrentDocumentChanged = true;
    removeTab(index);
    m_suppressCurrentDocumentChanged = false;
    // Emit the about-to-be-removed signal before erasing the
    // unique_ptr so listeners can flush state keyed by the doc
    // pointer (cancel any pending MlScheduler tasks, drop cache
    // entries) while the pointer is still valid to compare against.
    emit documentAboutToBeRemoved(doc);
    if (view) {
        view->deleteLater();
    }
    m_documents.erase(m_documents.begin() + index);

    // Announce the current document now that m_documents is consistent.
    //
    // removeTab() above fires QTabWidget::currentChanged, whose handler
    // would emit currentDocumentChanged(currentDocument()) — but that runs
    // BEFORE this erase, while m_documents still holds the closed document.
    // currentDocument() maps currentIndex() into m_documents, so at that
    // moment the mapping is off by one for every tab after `index`, and can
    // hand listeners the very document being closed. Nothing re-announced
    // afterwards, so listeners that follow the current document were left
    // synced to a stale vector: the sidebar kept showing — and holding a
    // raw pointer to — the closed document. That is how closing one tab of
    // two produced a dangling-pointer SIGSEGV, and, once the pointer was
    // guarded, a blank sidebar instead.
    //
    // So the emission is SUPPRESSED across removeTab() and issued exactly
    // once here. Once, not twice, is load-bearing:
    // MainWindow::onCurrentDocumentChanged retargets the external-change
    // monitor and re-runs onExternalFileChanged(), which can reload the
    // file or raise a conflict banner — firing it a second time, on a
    // stale mapping, is not a harmless duplicate.
    emit currentDocumentChanged(currentDocument());

    if (m_documents.empty()) {
        emit allTabsClosed();
    }
}

} // namespace trailer
