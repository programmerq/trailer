#include "MarkupToolbar.h"

#include "IconHelper.h"

#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>

namespace trailer {

MarkupToolbar::MarkupToolbar(QWidget *parent) : QToolBar(parent) {
    setWindowTitle(tr("Markup"));
    setObjectName(QStringLiteral("MarkupToolbar"));
    // Toolbar placement is intentional, not user-configurable. Letting
    // users drag the bar invariably ends with it docked somewhere
    // obscured or floating off-window with no obvious way back. The
    // right-click "hide toolbar" context menu is suppressed for the
    // same reason — View → Toggle Markup Toolbar is the single source
    // of truth for visibility. Overflow is handled by Qt's built-in
    // extension chevron.
    setMovable(false);
    setFloatable(false);
    setContextMenuPolicy(Qt::PreventContextMenu);
    // Icon-only at 18 px is the screen-real-estate win the user
    // asked for. Each action keeps its text label (used as tooltip
    // and as test fixture by findToolAction); the toolbar just
    // doesn't render it next to the glyph.
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setIconSize(QSize(18, 18));

    m_group = new QActionGroup(this);
    m_group->setExclusive(true);

    auto *selectAction = makeToolAction(tr("Select"), AnnotationTool::Select,
                                        QStringLiteral(":/icons/actions/tool-select.svg"));
    selectAction->setChecked(true);
    addSeparator();
    makeToolAction(tr("Rectangle"), AnnotationTool::Rectangle,
                   QStringLiteral(":/icons/actions/tool-rectangle.svg"));
    makeToolAction(tr("Ellipse"), AnnotationTool::Ellipse,
                   QStringLiteral(":/icons/actions/tool-ellipse.svg"));
    makeToolAction(tr("Line"), AnnotationTool::Line,
                   QStringLiteral(":/icons/actions/tool-line.svg"));
    makeToolAction(tr("Arrow"), AnnotationTool::Arrow,
                   QStringLiteral(":/icons/actions/tool-arrow.svg"));
    makeToolAction(tr("Freehand"), AnnotationTool::Ink,
                   QStringLiteral(":/icons/actions/tool-freehand.svg"));
    makeToolAction(tr("Text"), AnnotationTool::Text,
                   QStringLiteral(":/icons/actions/tool-text.svg"));
    makeToolAction(tr("Note"), AnnotationTool::Note,
                   QStringLiteral(":/icons/actions/tool-note.svg"));
    makeToolAction(tr("Bubble"), AnnotationTool::SpeechBubble,
                   QStringLiteral(":/icons/actions/tool-speech-bubble.svg"));
    makeToolAction(tr("Hl Shape"), AnnotationTool::HighlightShape,
                   QStringLiteral(":/icons/actions/tool-highlight-shape.svg"));
    makeToolAction(tr("Zoom Lens"), AnnotationTool::ZoomLens,
                   QStringLiteral(":/icons/actions/tool-zoom-lens.svg"));

    // Captured so we can hide it when the entire text-aware group is
    // hidden (e.g. on a bare image with no OCR results — the three
    // tools below are disabled there). Avoids two adjacent separators
    // around nothing on those documents.
    m_textAwareSeparator = addSeparator();

    makeToolAction(tr("Highlight"), AnnotationTool::Highlight,
                   QStringLiteral(":/icons/actions/tool-highlight.svg"));
    makeToolAction(tr("Underline"), AnnotationTool::Underline,
                   QStringLiteral(":/icons/actions/tool-underline.svg"));
    makeToolAction(tr("Strikeout"), AnnotationTool::StrikeOut,
                   QStringLiteral(":/icons/actions/tool-strikeout.svg"));

    addSeparator();

    auto *redactAction = makeToolAction(tr("Redact"), AnnotationTool::Redaction,
                                        QStringLiteral(":/icons/actions/tool-redact.svg"));
    redactAction->setToolTip(tr("Redact — paint a permanent black block. Content is rasterised "
                                "on save. Not a defence-grade redaction tool."));

    // Captured so we can hide the group on documents the SAM tools
    // can't run against (PDFs, animated GIFs, missing models).
    m_samSeparator = addSeparator();

    auto *instantAlphaAction =
        makeToolAction(tr("Instant Alpha"), AnnotationTool::InstantAlpha,
                       QStringLiteral(":/icons/actions/tool-instant-alpha.svg"));
    instantAlphaAction->setToolTip(tr("Instant Alpha — click on a region to make it transparent. "
                                      "Drag to refine; release to apply."));

    auto *smartLassoAction =
        makeToolAction(tr("Smart Lasso"), AnnotationTool::SmartLasso,
                       QStringLiteral(":/icons/actions/tool-smart-lasso.svg"));
    smartLassoAction->setToolTip(tr("Smart Lasso — click on an object to select it. Shift- or "
                                    "right-click adds exclusions. Press Enter or double-click to "
                                    "commit."));

    addSeparator();

    auto *strokeBtn = new QToolButton(this);
    strokeBtn->setText(tr("Stroke"));
    strokeBtn->setAutoRaise(true);
    auto refreshStrokeSwatch = [this, strokeBtn]() {
        QPixmap swatch(18, 18);
        swatch.fill(m_style.stroke);
        strokeBtn->setIcon(QIcon(swatch));
    };
    refreshStrokeSwatch();
    connect(strokeBtn, &QToolButton::clicked, this, [this, refreshStrokeSwatch]() {
        const QColor c = QColorDialog::getColor(m_style.stroke, this, tr("Stroke Colour"));
        if (!c.isValid())
            return;
        m_style.stroke = c;
        refreshStrokeSwatch();
        emit styleChanged(m_style);
    });
    addWidget(strokeBtn);

    auto *fillBtn = new QToolButton(this);
    fillBtn->setText(tr("Fill"));
    fillBtn->setAutoRaise(true);
    auto refreshFillSwatch = [this, fillBtn]() {
        QPixmap swatch(18, 18);
        if (m_style.fill.alpha() == 0) {
            swatch.fill(Qt::transparent);
            QPainter p(&swatch);
            p.setPen(QPen(Qt::darkGray, 1));
            p.drawLine(0, 17, 17, 0);
        } else {
            swatch.fill(m_style.fill);
        }
        fillBtn->setIcon(QIcon(swatch));
    };
    refreshFillSwatch();
    connect(fillBtn, &QToolButton::clicked, this, [this, refreshFillSwatch]() {
        const QColor c = QColorDialog::getColor(m_style.fill, this, tr("Fill Colour"),
                                                QColorDialog::ShowAlphaChannel);
        if (!c.isValid())
            return;
        m_style.fill = c;
        refreshFillSwatch();
        emit styleChanged(m_style);
    });
    addWidget(fillBtn);

    auto *widthSpin = new QDoubleSpinBox(this);
    widthSpin->setRange(0.5, 20.0);
    widthSpin->setSingleStep(0.5);
    widthSpin->setValue(m_style.strokeWidth);
    widthSpin->setSuffix(tr(" px"));
    connect(widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
                m_style.strokeWidth = v;
                emit styleChanged(m_style);
            });
    addWidget(new QLabel(tr("Width "), this));
    addWidget(widthSpin);

    auto *dashCombo = new QComboBox(this);
    dashCombo->addItem(tr("Solid"), static_cast<int>(DashStyle::Solid));
    dashCombo->addItem(tr("Dashed"), static_cast<int>(DashStyle::Dashed));
    dashCombo->addItem(tr("Dotted"), static_cast<int>(DashStyle::Dotted));
    connect(dashCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, dashCombo](int) {
                m_style.dash = static_cast<DashStyle>(dashCombo->currentData().toInt());
                emit styleChanged(m_style);
            });
    addWidget(new QLabel(tr("Dash "), this));
    addWidget(dashCombo);

    m_tool = AnnotationTool::Select;
}

AnnotationStyle MarkupToolbar::style() const {
    return m_style;
}

QAction *MarkupToolbar::makeToolAction(const QString &label, AnnotationTool tool,
                                       const QString &iconResource) {
    QAction *action = nullptr;
    if (!iconResource.isEmpty()) {
        const QIcon icon = themedActionIcon(iconResource, this);
        action = addAction(icon, label);
    } else {
        action = addAction(label);
    }
    action->setToolTip(label);
    action->setCheckable(true);
    m_group->addAction(action);
    connect(action, &QAction::toggled, this, [this, tool](bool on) {
        if (!on)
            return;
        m_tool = tool;
        emit activeToolChanged(tool);
    });
    m_toolActions.insert(tool, action);
    return action;
}

void MarkupToolbar::setActiveTool(AnnotationTool tool) {
    if (tool == m_tool)
        return;
    auto it = m_toolActions.find(tool);
    if (it == m_toolActions.end())
        return;
    // Flipping the action's checked state fires the toggled slot,
    // which updates m_tool and re-emits activeToolChanged.
    it.value()->setChecked(true);
}

void MarkupToolbar::setToolVisible(AnnotationTool tool, bool visible) {
    auto it = m_toolActions.find(tool);
    if (it == m_toolActions.end())
        return;
    QAction *action = it.value();
    if (action->isVisible() == visible)
        return;
    action->setVisible(visible);
    // If we just hid the active tool, fall back to Select so the
    // overlay isn't stuck consuming click-drags for a tool whose
    // button is no longer reachable.
    if (!visible && tool == m_tool) {
        setActiveTool(AnnotationTool::Select);
    }
    // Text-aware group: when every tool in it is hidden, also hide
    // the preceding separator. Otherwise we leave two adjacent
    // dividers wrapping an empty region. The check runs on every
    // change so re-showing one tool brings the separator back.
    auto visibleByTool = [this](AnnotationTool t) {
        auto i = m_toolActions.find(t);
        return i != m_toolActions.end() && i.value()->isVisible();
    };
    const bool isTextAware = tool == AnnotationTool::Highlight ||
                             tool == AnnotationTool::Underline || tool == AnnotationTool::StrikeOut;
    if (isTextAware && m_textAwareSeparator) {
        const bool anyVisible = visibleByTool(AnnotationTool::Highlight) ||
                                visibleByTool(AnnotationTool::Underline) ||
                                visibleByTool(AnnotationTool::StrikeOut);
        m_textAwareSeparator->setVisible(anyVisible);
    }
    // SAM group: identical pattern. The MainWindow hides both on
    // PDFs / animated docs / when the SAM models policy is "Never
    // Download" so a non-actionable button never appears.
    const bool isSam =
        tool == AnnotationTool::InstantAlpha || tool == AnnotationTool::SmartLasso;
    if (isSam && m_samSeparator) {
        const bool anyVisible = visibleByTool(AnnotationTool::InstantAlpha) ||
                                visibleByTool(AnnotationTool::SmartLasso);
        m_samSeparator->setVisible(anyVisible);
    }
}

} // namespace trailer
