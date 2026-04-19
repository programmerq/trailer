#pragma once

#include "IDocument.h"
#include "IFormatAdapter.h"

#include <QString>
#include <QStringList>
#include <memory>

namespace trailer {

class ImageDocument : public IDocument {
public:
    explicit ImageDocument(QString path);

    QString displayName() const override;
    QString filePath() const override;
    QWidget* createView(QWidget* parent) override;

private:
    QString m_path;
    bool m_animated = false;
};

class ImageAdapter : public IFormatAdapter {
public:
    QStringList mimeTypes() const override;
    QStringList extensions() const override;
    std::unique_ptr<IDocument> open(const QString& path) override;
};

}  // namespace trailer
