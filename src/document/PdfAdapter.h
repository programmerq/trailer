#pragma once

#include "IDocument.h"
#include "IFormatAdapter.h"

#include <QString>
#include <QStringList>
#include <memory>

class QPdfDocument;

namespace trailer {

class PdfDocument : public IDocument {
public:
    explicit PdfDocument(QString path);
    ~PdfDocument() override;

    QString displayName() const override;
    QString filePath() const override;
    QWidget* createView(QWidget* parent) override;

    int pageCount() const;
    bool isValid() const { return m_valid; }

private:
    QString m_path;
    std::unique_ptr<QPdfDocument> m_doc;
    bool m_valid = false;
};

class PdfAdapter : public IFormatAdapter {
public:
    QStringList mimeTypes() const override;
    QStringList extensions() const override;
    std::unique_ptr<IDocument> open(const QString& path) override;
};

}  // namespace trailer
