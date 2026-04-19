#include "Sidebar.h"

#include <QLabel>
#include <QVBoxLayout>

namespace trailer {

Sidebar::Sidebar(QWidget* parent) : QDockWidget(tr("Sidebar"), parent) {
    setObjectName(QStringLiteral("trailer.sidebar"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);

    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    auto* placeholder = new QLabel(tr("Thumbnails, TOC, and notes will live here."), content);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setWordWrap(true);
    layout->addWidget(placeholder);
    layout->addStretch();
    setWidget(content);
}

}  // namespace trailer
