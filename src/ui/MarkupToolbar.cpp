#include "MarkupToolbar.h"

#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>

namespace trailer {

MarkupToolbar::MarkupToolbar(QWidget* parent) : QToolBar(parent) {
    setWindowTitle(tr("Markup"));
    setObjectName(QStringLiteral("MarkupToolbar"));
    setMovable(true);

    m_group = new QActionGroup(this);
    m_group->setExclusive(true);

    auto* selectAction = makeToolAction(tr("Select"), AnnotationTool::Select);
    selectAction->setChecked(true);
    addSeparator();
    makeToolAction(tr("Rectangle"), AnnotationTool::Rectangle);
    makeToolAction(tr("Ellipse"), AnnotationTool::Ellipse);
    makeToolAction(tr("Line"), AnnotationTool::Line);
    makeToolAction(tr("Arrow"), AnnotationTool::Arrow);
    makeToolAction(tr("Freehand"), AnnotationTool::Ink);
    makeToolAction(tr("Text"), AnnotationTool::Text);
    makeToolAction(tr("Note"), AnnotationTool::Note);

    addSeparator();

    makeToolAction(tr("Highlight"), AnnotationTool::Highlight);
    makeToolAction(tr("Underline"), AnnotationTool::Underline);
    makeToolAction(tr("Strikeout"), AnnotationTool::StrikeOut);

    addSeparator();

    auto* strokeBtn = new QToolButton(this);
    strokeBtn->setText(tr("Stroke"));
    strokeBtn->setAutoRaise(true);
    auto refreshStrokeSwatch = [this, strokeBtn]() {
        QPixmap swatch(18, 18);
        swatch.fill(m_style.stroke);
        strokeBtn->setIcon(QIcon(swatch));
    };
    refreshStrokeSwatch();
    connect(strokeBtn, &QToolButton::clicked, this, [this, refreshStrokeSwatch]() {
        const QColor c = QColorDialog::getColor(m_style.stroke, this,
            tr("Stroke Colour"));
        if (!c.isValid()) return;
        m_style.stroke = c;
        refreshStrokeSwatch();
        emit styleChanged(m_style);
    });
    addWidget(strokeBtn);

    auto* fillBtn = new QToolButton(this);
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
        const QColor c = QColorDialog::getColor(m_style.fill, this,
            tr("Fill Colour"), QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        m_style.fill = c;
        refreshFillSwatch();
        emit styleChanged(m_style);
    });
    addWidget(fillBtn);

    auto* widthSpin = new QDoubleSpinBox(this);
    widthSpin->setRange(0.5, 20.0);
    widthSpin->setSingleStep(0.5);
    widthSpin->setValue(m_style.strokeWidth);
    widthSpin->setSuffix(tr(" px"));
    connect(widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                m_style.strokeWidth = v;
                emit styleChanged(m_style);
            });
    addWidget(new QLabel(tr("Width "), this));
    addWidget(widthSpin);

    m_tool = AnnotationTool::Select;
}

AnnotationStyle MarkupToolbar::style() const { return m_style; }

QAction* MarkupToolbar::makeToolAction(const QString& label, AnnotationTool tool) {
    auto* action = addAction(label);
    action->setCheckable(true);
    m_group->addAction(action);
    connect(action, &QAction::toggled, this, [this, tool](bool on) {
        if (!on) return;
        m_tool = tool;
        emit activeToolChanged(tool);
    });
    return action;
}

}  // namespace trailer
