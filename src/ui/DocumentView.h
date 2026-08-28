#pragma once

#include "document/IDocument.h"

#include <QTabWidget>
#include <memory>
#include <vector>

namespace trailer {

class DocumentView : public QTabWidget {
    Q_OBJECT

  public:
    explicit DocumentView(QWidget *parent = nullptr);

    void addDocument(std::unique_ptr<IDocument> document);

    int documentCount() const { return static_cast<int>(m_documents.size()); }

    IDocument *currentDocument() const;

    // Iterate every document this view holds. The returned span is
    // valid until the next addDocument / onTabCloseRequested call;
    // callers must not retain the pointers across those operations.
    // Used by MainWindow's close prompt to walk the dirty docs and
    // by the auto-save timer to flush them all.
    int documentAt(int index, IDocument **out) const;

  signals:
    void allTabsClosed();
    void currentDocumentChanged(IDocument *document);
    // Synchronous close veto. Emitted from onTabCloseRequested BEFORE
    // the tab / document is torn down, giving a listener (MainWindow's
    // unsaved-changes prompt) the chance to abort the close. The
    // listener sets *veto = true to keep the tab and document fully
    // intact. DocumentView deliberately owns no QMessageBox: the
    // policy of whether to prompt lives in MainWindow, which reuses
    // the same Save/Discard/Cancel flow as the window-close path.
    // Single-listener assumption: the bool* veto is only ever SET to
    // true (never cleared), so if multiple slots connect it is
    // last-writer-wins — the current design wires exactly one listener.
    void documentCloseRequested(IDocument *document, bool *veto);
    // Emitted just before a document is destroyed (after its tab is
    // removed). Listeners that hold raw IDocument* keys (e.g.
    // MainWindow's background-candidate score cache, MlScheduler-
    // bound work tagged with the doc pointer) use this to flush
    // entries and cancel pending tasks so a recycled allocator
    // address can't collide with a stale key.
    void documentAboutToBeRemoved(IDocument *document);

  private slots:
    void onTabCloseRequested(int index);

  private:
    std::vector<std::unique_ptr<IDocument>> m_documents;

    // Set while onTabCloseRequested() is mid-removal. removeTab() fires
    // QTabWidget::currentChanged before m_documents has been erased, so
    // currentDocument() would map currentIndex() into a vector that still
    // holds the closed document — an emission listeners must not see.
    // onTabCloseRequested emits currentDocumentChanged exactly once,
    // itself, after the erase. See its comment for why this matters.
    bool m_suppressCurrentDocumentChanged = false;
};

} // namespace trailer
