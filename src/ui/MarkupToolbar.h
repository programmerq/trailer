#pragma once

#include "annotation/Annotation.h"
#include "ui/IconHelper.h"

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

    // Enable or disable a single tool's QAction, in place. Used by
    // MainWindow to gate text-aware tools (Underline / Highlight /
    // StrikeOut) on documents without a text layer, and the SAM tools
    // (Instant Alpha / Smart Lasso) on non-image documents or a blocked
    // model policy. `disabledTooltip` is required whenever `enabled` is
    // false and states why the tool can't act right now (G3 — a control
    // that can't act is disabled with a tooltip, never silently absent);
    // it is ignored when `enabled` is true, since the action reverts to
    // its plain label tooltip.
    //
    // G10 (spatial constancy, AGENTS.md; SC-CRIT-2,
    // docs/audit-2026-07-31-g10-deference.md) is why this disables rather
    // than hides: every tool action keeps its on-screen slot regardless of
    // document capability, so Redact / Stroke / Fill / Width / Dash never
    // shift when the current document changes. This supersedes the prior
    // hide-based design — see
    // docs/decision-records/2026-08-01-markup-toolbar-disable-not-hide.md
    // for why G10 (a later, accepted, binding gate) outweighs the earlier
    // "hidden, not greyed" call for tool actions specifically. If the
    // currently-active tool is disabled, the toolbar automatically falls
    // back to Select so the overlay isn't stuck consuming click-drags for
    // a tool the user can no longer engage.
    //
    // Because every action stays visible, a separator between two groups
    // is never left bounding an empty region — the "two adjacent dividers
    // around nothing" concern the old hide-based design solved for no
    // longer arises, so there is no separate separator-visibility step.
    void setToolEnabled(AnnotationTool tool, bool enabled,
                        const QString &disabledTooltip = QString());

    // Re-tint the toolbar's themed tool icons from the current palette,
    // called by MainWindow after a live theme (colour-scheme) change.
    void refreshThemedIcons() { m_themedIcons.refresh(); }

  signals:
    void activeToolChanged(AnnotationTool tool);
    void styleChanged(const AnnotationStyle &style);

  private:
    QAction *makeToolAction(const QString &label, AnnotationTool tool,
                            const QString &iconResource = QString());

    ThemedIconBinder m_themedIcons;
    QActionGroup *m_group = nullptr;
    AnnotationTool m_tool = AnnotationTool::None;
    AnnotationStyle m_style;
    QHash<AnnotationTool, QAction *> m_toolActions;
};

} // namespace trailer
