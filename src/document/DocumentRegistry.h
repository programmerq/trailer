#pragma once

#include "IDocument.h"
#include "IFormatAdapter.h"
#include "StubAdapter.h"

#include <QString>
#include <memory>
#include <vector>

namespace trailer {

class DocumentRegistry {
public:
    DocumentRegistry();

    void registerAdapter(std::unique_ptr<IFormatAdapter> adapter);

    std::unique_ptr<IDocument> open(const QString& path) const;

private:
    std::vector<std::unique_ptr<IFormatAdapter>> m_adapters;
    StubAdapter m_fallback;
};

}  // namespace trailer
