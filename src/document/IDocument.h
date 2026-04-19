#pragma once

#include <QString>
#include <QWidget>

namespace trailer {

class IDocument {
public:
    virtual ~IDocument() = default;

    virtual QString displayName() const = 0;
    virtual QString filePath() const = 0;
    virtual QWidget* createView(QWidget* parent) = 0;
};

}  // namespace trailer
