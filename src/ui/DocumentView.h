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

  private slots:
    void onTabCloseRequested(int index);

  private:
    std::vector<std::unique_ptr<IDocument>> m_documents;
};

} // namespace trailer
