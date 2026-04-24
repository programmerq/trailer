#include "FormToolbar.h"

#include <QAction>
#include <QActionGroup>

namespace trailer {

FormToolbar::FormToolbar(QWidget* parent) : QToolBar(parent) {
    setWindowTitle(tr("Form Filling"));
    setObjectName(QStringLiteral("FormToolbar"));
    setMovable(true);

    m_group = new QActionGroup(this);
    m_group->setExclusive(true);

    auto* selectAction = makeToolAction(tr("Select"), AnnotationTool::Select);
    selectAction->setChecked(true);

    addSeparator();
    makeToolAction(tr("Text Box"), AnnotationTool::Text);
    // ✓ and ✗ as pre-seeded text content. When the user next clicks the
    // canvas, PdfDocument/AnnotationOverlay drops a Text annotation
    // containing this glyph instead of opening the inline editor.
    makeToolAction(tr("Checkmark"), AnnotationTool::Text,
                   QStringLiteral(u"\u2713"));
    makeToolAction(tr("X Mark"), AnnotationTool::Text,
                   QStringLiteral(u"\u2717"));

    addSeparator();

    m_autoFillAction = addAction(tr("AutoFill"));
    m_autoFillAction->setToolTip(
        tr("Fill form fields from your saved card"));
    connect(m_autoFillAction, &QAction::triggered,
            this, &FormToolbar::autoFillRequested);

    m_signHereAction = addAction(tr("Sign Here"));
    m_signHereAction->setToolTip(
        tr("Place a saved signature on the page"));
    connect(m_signHereAction, &QAction::triggered,
            this, &FormToolbar::signHereRequested);

    m_tool = AnnotationTool::Select;
}

QAction* FormToolbar::makeToolAction(const QString& label,
                                     AnnotationTool tool,
                                     const QString& pendingText) {
    auto* action = addAction(label);
    action->setCheckable(true);
    m_group->addAction(action);
    connect(action, &QAction::toggled, this,
            [this, tool, pendingText](bool on) {
                if (!on) return;
                m_tool = tool;
                m_pendingText = pendingText;
                emit toolChanged(tool, pendingText);
            });
    return action;
}

}  // namespace trailer
