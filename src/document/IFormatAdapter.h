#pragma once

#include "IDocument.h"

#include <QString>
#include <QStringList>
#include <memory>

namespace trailer {

class IFormatAdapter {
  public:
    virtual ~IFormatAdapter() = default;

    virtual QStringList mimeTypes() const = 0;
    virtual QStringList extensions() const = 0;
    virtual std::unique_ptr<IDocument> open(const QString &path) = 0;
};

} // namespace trailer
