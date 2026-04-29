#pragma once

#include "annotation/Annotation.h"

#include <QHash>
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

    // Programmatically switch the active tool. Used by MainWindow to
    // bounce back to Select when the redaction first-use warning is
    // declined. The corresponding action is checked (matching what a
    // user click would have done) and activeToolChanged is re-emitted
    // only if the tool actually changed.
    void setActiveTool(AnnotationTool tool);

    // Enable or disable a single tool's QAction. Used by MainWindow to
    // gate text-aware tools (Underline / Highlight / StrikeOut) on
    // documents without a text layer. If the currently-active tool is
    // disabled, the toolbar automatically falls back to Select so the
    // user is not stranded in a now-greyed-out mode.
    void setToolEnabled(AnnotationTool tool, bool enabled);

signals:
    void activeToolChanged(AnnotationTool tool);
    void styleChanged(const AnnotationStyle& style);

private:
    QAction* makeToolAction(const QString& label, AnnotationTool tool);

    QActionGroup* m_group = nullptr;
    AnnotationTool m_tool = AnnotationTool::None;
    AnnotationStyle m_style;
    QHash<AnnotationTool, QAction*> m_toolActions;
};

}  // namespace trailer
