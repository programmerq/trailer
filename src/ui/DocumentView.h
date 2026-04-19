#pragma once

#include "document/IDocument.h"

#include <QTabWidget>
#include <memory>
#include <vector>

namespace trailer {

class DocumentView : public QTabWidget {
    Q_OBJECT

public:
    explicit DocumentView(QWidget* parent = nullptr);

    void addDocument(std::unique_ptr<IDocument> document);

    int documentCount() const { return static_cast<int>(m_documents.size()); }

signals:
    void allTabsClosed();

private slots:
    void onTabCloseRequested(int index);

private:
    std::vector<std::unique_ptr<IDocument>> m_documents;
};

}  // namespace trailer
