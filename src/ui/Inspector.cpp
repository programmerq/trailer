#include "Inspector.h"

#include "annotation/AnnotationStore.h"

#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace trailer {

namespace {

QString typeLabel(AnnotationType type) {
    switch (type) {
        case AnnotationType::Rectangle: return Inspector::tr("Rectangle");
        case AnnotationType::Ellipse:   return Inspector::tr("Ellipse");
        case AnnotationType::Line:      return Inspector::tr("Line");
        case AnnotationType::Arrow:     return Inspector::tr("Arrow");
        case AnnotationType::Ink:       return Inspector::tr("Freehand");
        case AnnotationType::Text:      return Inspector::tr("Text");
        case AnnotationType::Note:      return Inspector::tr("Note");
        case AnnotationType::Highlight: return Inspector::tr("Highlight");
        case AnnotationType::Underline: return Inspector::tr("Underline");
        case AnnotationType::StrikeOut: return Inspector::tr("Strikeout");
    }
    return {};
}

void applySwatch(QToolButton* btn, QColor c) {
    QPixmap swatch(18, 18);
    if (c.alpha() == 0) {
        swatch.fill(Qt::transparent);
        QPainter p(&swatch);
        p.setPen(QPen(Qt::darkGray, 1));
        p.drawLine(0, 17, 17, 0);
    } else {
        swatch.fill(c);
    }
    btn->setIcon(QIcon(swatch));
}

}  // namespace

Inspector::Inspector(QWidget* parent) : QDockWidget(tr("Inspector"), parent) {
    setObjectName(QStringLiteral("trailer.inspector"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_stack = new QStackedWidget(this);

    auto* empty = new QWidget(m_stack);
    auto* emptyLayout = new QVBoxLayout(empty);
    auto* emptyLabel = new QLabel(tr("Select an annotation to inspect."), empty);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setWordWrap(true);
    emptyLayout->addWidget(emptyLabel);
    emptyLayout->addStretch();
    m_emptyIndex = m_stack->addWidget(empty);

    auto* form = new QWidget(m_stack);
    auto* layout = new QFormLayout(form);
    m_pageLabel = new QLabel(form);
    m_typeLabel = new QLabel(form);
    layout->addRow(tr("Page:"), m_pageLabel);
    layout->addRow(tr("Type:"), m_typeLabel);

    m_strokeButton = new QToolButton(form);
    m_strokeButton->setText(tr("Stroke"));
    m_strokeButton->setAutoRaise(true);
    layout->addRow(tr("Stroke:"), m_strokeButton);
    connect(m_strokeButton, &QToolButton::clicked, this, [this]() {
        if (!m_store || m_id == 0) return;
        const Annotation* a = m_store->find(m_id);
        if (!a) return;
        const QColor c = QColorDialog::getColor(a->style.stroke, this,
            tr("Stroke Colour"));
        if (!c.isValid()) return;
        Annotation updated = *a;
        updated.style.stroke = c;
        m_store->update(updated);
        applySwatch(m_strokeButton, c);
    });

    m_fillButton = new QToolButton(form);
    m_fillButton->setText(tr("Fill"));
    m_fillButton->setAutoRaise(true);
    layout->addRow(tr("Fill:"), m_fillButton);
    connect(m_fillButton, &QToolButton::clicked, this, [this]() {
        if (!m_store || m_id == 0) return;
        const Annotation* a = m_store->find(m_id);
        if (!a) return;
        const QColor c = QColorDialog::getColor(a->style.fill, this,
            tr("Fill Colour"), QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        Annotation updated = *a;
        updated.style.fill = c;
        m_store->update(updated);
        applySwatch(m_fillButton, c);
    });

    m_strokeWidth = new QDoubleSpinBox(form);
    m_strokeWidth->setRange(0.5, 20.0);
    m_strokeWidth->setSingleStep(0.5);
    m_strokeWidth->setSuffix(tr(" px"));
    layout->addRow(tr("Stroke width:"), m_strokeWidth);
    connect(m_strokeWidth, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                if (m_loading) return;
                if (!m_store || m_id == 0) return;
                const Annotation* a = m_store->find(m_id);
                if (!a) return;
                Annotation updated = *a;
                updated.style.strokeWidth = v;
                m_store->update(updated);
            });

    m_fontSize = new QSpinBox(form);
    m_fontSize->setRange(6, 72);
    m_fontSize->setSuffix(tr(" pt"));
    layout->addRow(tr("Font size:"), m_fontSize);
    connect(m_fontSize, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
                if (m_loading) return;
                if (!m_store || m_id == 0) return;
                const Annotation* a = m_store->find(m_id);
                if (!a) return;
                Annotation updated = *a;
                updated.style.fontPointSize = v;
                m_store->update(updated);
            });

    m_text = new QPlainTextEdit(form);
    m_text->setPlaceholderText(tr("Body text"));
    m_text->setMaximumHeight(120);
    layout->addRow(tr("Text:"), m_text);
    connect(m_text, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_loading) return;
        if (!m_store || m_id == 0) return;
        const Annotation* a = m_store->find(m_id);
        if (!a) return;
        Annotation updated = *a;
        updated.text = m_text->toPlainText();
        m_store->update(updated);
    });

    m_formIndex = m_stack->addWidget(form);
    m_stack->setCurrentIndex(m_emptyIndex);

    setWidget(m_stack);
}

void Inspector::setAnnotation(AnnotationStore* store, int id) {
    if (m_store && m_store != store) {
        disconnect(m_store, nullptr, this, nullptr);
    }
    m_store = store;
    m_id = id;
    if (m_store) {
        connect(m_store, &AnnotationStore::changed, this,
                &Inspector::rebuildFromStore, Qt::UniqueConnection);
    }
    rebuildFromStore();
}

void Inspector::clearSelection() {
    m_id = 0;
    m_stack->setCurrentIndex(m_emptyIndex);
}

void Inspector::rebuildFromStore() {
    if (!m_store || m_id == 0) {
        m_stack->setCurrentIndex(m_emptyIndex);
        return;
    }
    const Annotation* a = m_store->find(m_id);
    if (!a) {
        m_stack->setCurrentIndex(m_emptyIndex);
        return;
    }
    m_loading = true;
    m_pageLabel->setText(QString::number(a->page + 1));
    m_typeLabel->setText(typeLabel(a->type));
    applySwatch(m_strokeButton, a->style.stroke);
    applySwatch(m_fillButton, a->style.fill);
    m_strokeWidth->setValue(a->style.strokeWidth);
    m_fontSize->setValue(a->style.fontPointSize > 0 ? a->style.fontPointSize : 12);
    const bool hasText = a->type == AnnotationType::Text
                      || a->type == AnnotationType::Note;
    m_text->setEnabled(hasText);
    m_fontSize->setEnabled(a->type == AnnotationType::Text);
    if (m_text->toPlainText() != a->text) {
        m_text->setPlainText(a->text);
    }
    m_stack->setCurrentIndex(m_formIndex);
    m_loading = false;
}

}  // namespace trailer
