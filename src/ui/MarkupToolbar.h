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
    explicit MarkupToolbar(QWidget *parent = nullptr);

    AnnotationTool activeTool() const { return m_tool; }
    AnnotationStyle style() const;

    // Programmatically switch the active tool. Used by MainWindow to
    // bounce back to Select when the redaction first-use warning is
    // declined. The corresponding action is checked (matching what a
    // user click would have done) and activeToolChanged is re-emitted
    // only if the tool actually changed.
    void setActiveTool(AnnotationTool tool);

    // Hide or show a single tool's QAction. Used by MainWindow to
    // gate text-aware tools (Underline / Highlight / StrikeOut) on
    // documents without a text layer. Hiding (rather than disabling)
    // visibly shrinks the toolbar — at the 18 px icon size a disabled
    // glyph is more noise than information. If the currently-active
    // tool is hidden, the toolbar automatically falls back to Select.
    //
    // If hiding leaves a separator-bounded group with no visible
    // tools, the preceding separator is hidden too so the user doesn't
    // see a pair of adjacent dividers around nothing. (Currently the
    // text-aware trio is the only group with this treatment — other
    // groups stay populated for every document type.)
    void setToolVisible(AnnotationTool tool, bool visible);

  signals:
    void activeToolChanged(AnnotationTool tool);
    void styleChanged(const AnnotationStyle &style);

  private:
    QAction *makeToolAction(const QString &label, AnnotationTool tool,
                            const QString &iconResource = QString());

    QActionGroup *m_group = nullptr;
    AnnotationTool m_tool = AnnotationTool::None;
    AnnotationStyle m_style;
    QHash<AnnotationTool, QAction *> m_toolActions;
    // Separator immediately before the text-aware tool group
    // (Underline / Highlight / StrikeOut). Tracked so we can hide it
    // when every tool in that group is hidden — see setToolVisible.
    QAction *m_textAwareSeparator = nullptr;
};

} // namespace trailer
