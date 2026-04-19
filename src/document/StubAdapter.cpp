#include "StubAdapter.h"

#include <QFileInfo>
#include <QLabel>
#include <QVBoxLayout>

namespace trailer {

StubDocument::StubDocument(QString path) : m_path(std::move(path)) {}

QString StubDocument::displayName() const {
    if (m_path.isEmpty()) {
        return QStringLiteral("Untitled");
    }
    return QFileInfo(m_path).fileName();
}

QString StubDocument::filePath() const {
    return m_path;
}

QWidget* StubDocument::createView(QWidget* parent) {
    auto* container = new QWidget(parent);
    auto* layout = new QVBoxLayout(container);

    const QString suffix = QFileInfo(m_path).suffix();
    const QString text = suffix.isEmpty()
        ? QStringLiteral("Unsupported file\n%1").arg(m_path)
        : QStringLiteral("Unsupported file type: .%1\n%2").arg(suffix, m_path);

    auto* label = new QLabel(text, container);
    label->setAlignment(Qt::AlignCenter);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(label);
    return container;
}

QStringList StubAdapter::mimeTypes() const {
    return {};
}

QStringList StubAdapter::extensions() const {
    return {};
}

std::unique_ptr<IDocument> StubAdapter::open(const QString& path) {
    return std::make_unique<StubDocument>(path);
}

}  // namespace trailer
