#include "FormToolbar.h"

#include "IconHelper.h"

#include <QAction>
#include <QActionGroup>

namespace trailer {

FormToolbar::FormToolbar(QWidget* parent) : QToolBar(parent) {
    setWindowTitle(tr("Form Filling"));
    setObjectName(QStringLiteral("FormToolbar"));
    // Locked placement; see MarkupToolbar for the rationale.
    setMovable(false);
    setFloatable(false);
    setContextMenuPolicy(Qt::PreventContextMenu);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setIconSize(QSize(18, 18));

    m_group = new QActionGroup(this);
    m_group->setExclusive(true);

    auto* selectAction = makeToolAction(tr("Select"), AnnotationTool::Select,
                                        QString(),
                                        QStringLiteral(":/icons/actions/tool-select.svg"));
    selectAction->setChecked(true);

    addSeparator();
    makeToolAction(tr("Text Box"), AnnotationTool::Text, QString(),
                   QStringLiteral(":/icons/actions/tool-text.svg"));
    // ✓ and ✗ as pre-seeded text content. When the user next clicks the
    // canvas, PdfDocument/AnnotationOverlay drops a Text annotation
    // containing this glyph instead of opening the inline editor.
    makeToolAction(tr("Checkmark"), AnnotationTool::Text,
                   QStringLiteral(u"✓"),
                   QStringLiteral(":/icons/actions/tool-checkmark.svg"));
    makeToolAction(tr("X Mark"), AnnotationTool::Text,
                   QStringLiteral(u"✗"),
                   QStringLiteral(":/icons/actions/tool-xmark.svg"));

    addSeparator();

    m_autoFillAction = addAction(
        themedActionIcon(QStringLiteral(":/icons/actions/tool-autofill.svg"), this),
        tr("AutoFill"));
    m_autoFillAction->setToolTip(
        tr("AutoFill — fill form fields from your saved card"));
    connect(m_autoFillAction, &QAction::triggered,
            this, &FormToolbar::autoFillRequested);

    m_signHereAction = addAction(
        themedActionIcon(QStringLiteral(":/icons/actions/tool-sign-here.svg"), this),
        tr("Sign Here"));
    m_signHereAction->setToolTip(
        tr("Sign Here — place a saved signature on the page"));
    connect(m_signHereAction, &QAction::triggered,
            this, &FormToolbar::signHereRequested);

    m_tool = AnnotationTool::Select;
}

QAction* FormToolbar::makeToolAction(const QString& label,
                                     AnnotationTool tool,
                                     const QString& pendingText,
                                     const QString& iconResource) {
    QAction* action = nullptr;
    if (!iconResource.isEmpty()) {
        action = addAction(themedActionIcon(iconResource, this), label);
    } else {
        action = addAction(label);
    }
    action->setToolTip(label);
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
