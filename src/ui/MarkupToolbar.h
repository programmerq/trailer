#pragma once

#include "annotation/Annotation.h"

#include <QToolBar>

class QAction;
class QActionGroup;

namespace trailer {

class MarkupToolbar : public QToolBar {
    Q_OBJECT

public:
    explicit MarkupToolbar(QWidget* parent = nullptr);

    AnnotationTool activeTool() const { return m_tool; }
    AnnotationStyle style() const;

signals:
    void activeToolChanged(AnnotationTool tool);
    void styleChanged(const AnnotationStyle& style);

private:
    QAction* makeToolAction(const QString& label, AnnotationTool tool);

    QActionGroup* m_group = nullptr;
    AnnotationTool m_tool = AnnotationTool::None;
    AnnotationStyle m_style;
};

}  // namespace trailer
