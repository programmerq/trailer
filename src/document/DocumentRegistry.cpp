#include "DocumentRegistry.h"

#include <QFileInfo>
#include <QMimeDatabase>

namespace trailer {

DocumentRegistry::DocumentRegistry() = default;

void DocumentRegistry::registerAdapter(std::unique_ptr<IFormatAdapter> adapter) {
    m_adapters.push_back(std::move(adapter));
}

std::unique_ptr<IDocument> DocumentRegistry::open(const QString &path) const {
    const QString suffix = QFileInfo(path).suffix().toLower();
    for (const auto &adapter : m_adapters) {
        if (adapter->extensions().contains(suffix, Qt::CaseInsensitive)) {
            return adapter->open(path);
        }
    }

    QMimeDatabase db;
    const QString mime = db.mimeTypeForFile(path).name();
    for (const auto &adapter : m_adapters) {
        if (adapter->mimeTypes().contains(mime, Qt::CaseInsensitive)) {
            return adapter->open(path);
        }
    }

    return const_cast<StubAdapter &>(m_fallback).open(path);
}

} // namespace trailer
