#include "Inspector.h"

#include "annotation/AnnotationStore.h"
#include "document/IDocument.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace trailer {

namespace {

QString typeLabel(AnnotationType type) {
    switch (type) {
    case AnnotationType::Rectangle:
        return Inspector::tr("Rectangle");
    case AnnotationType::Ellipse:
        return Inspector::tr("Ellipse");
    case AnnotationType::Line:
        return Inspector::tr("Line");
    case AnnotationType::Arrow:
        return Inspector::tr("Arrow");
    case AnnotationType::Ink:
        return Inspector::tr("Freehand");
    case AnnotationType::Text:
        return Inspector::tr("Text");
    case AnnotationType::Note:
        return Inspector::tr("Note");
    case AnnotationType::Highlight:
        return Inspector::tr("Highlight");
    case AnnotationType::Underline:
        return Inspector::tr("Underline");
    case AnnotationType::StrikeOut:
        return Inspector::tr("Strikeout");
    case AnnotationType::HighlightShape:
        return Inspector::tr("Highlight Shape");
    case AnnotationType::SpeechBubble:
        return Inspector::tr("Speech Bubble");
    case AnnotationType::ZoomLens:
        return Inspector::tr("Zoom Lens");
    case AnnotationType::Signature:
        return Inspector::tr("Signature");
    case AnnotationType::Redaction:
        return Inspector::tr("Redaction");
    }
    return {};
}

void applySwatch(QToolButton *btn, QColor c) {
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

} // namespace

Inspector::Inspector(QWidget *parent) : QDockWidget(tr("Inspector"), parent) {
    setObjectName(QStringLiteral("trailer.inspector"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_tabs = new QTabWidget(this);

    auto *docTab = new QWidget(m_tabs);
    auto *docLayout = new QFormLayout(docTab);
    m_docNameLabel = new QLabel(docTab);
    m_docPathLabel = new QLabel(docTab);
    m_docPathLabel->setWordWrap(true);
    m_docPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_docPagesLabel = new QLabel(docTab);
    m_docSizeLabel = new QLabel(docTab);
    m_docDirtyLabel = new QLabel(docTab);
    docLayout->addRow(tr("Name:"), m_docNameLabel);
    docLayout->addRow(tr("Path:"), m_docPathLabel);
    docLayout->addRow(tr("Pages:"), m_docPagesLabel);
    docLayout->addRow(tr("Dimensions:"), m_docSizeLabel);
    docLayout->addRow(tr("Status:"), m_docDirtyLabel);
    m_tabs->addTab(docTab, tr("Document"));

    m_stack = new QStackedWidget(m_tabs);

    auto *empty = new QWidget(m_stack);
    auto *emptyLayout = new QVBoxLayout(empty);
    auto *emptyLabel = new QLabel(tr("Select an annotation to inspect."), empty);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setWordWrap(true);
    emptyLayout->addWidget(emptyLabel);
    emptyLayout->addStretch();
    m_emptyIndex = m_stack->addWidget(empty);

    auto *form = new QWidget(m_stack);
    auto *layout = new QFormLayout(form);
    m_pageLabel = new QLabel(form);
    m_typeLabel = new QLabel(form);
    layout->addRow(tr("Page:"), m_pageLabel);
    layout->addRow(tr("Type:"), m_typeLabel);

    m_strokeButton = new QToolButton(form);
    m_strokeButton->setObjectName(QStringLiteral("trailer.inspector.strokeButton"));
    m_strokeButton->setText(tr("Stroke"));
    m_strokeButton->setAutoRaise(true);
    layout->addRow(tr("Stroke:"), m_strokeButton);
    connect(m_strokeButton, &QToolButton::clicked, this, [this]() {
        if (!m_store || m_id == 0)
            return;
        // Snapshot the initial colour BEFORE the modal. `find()` returns a
        // pointer into m_store's std::vector<Annotation>; QColorDialog::getColor
        // spins the Qt event loop, and any store mutation that fires during
        // the dialog (auto-save, queued changed-slot, undo coalescing, etc.)
        // can reallocate the vector and dangle the pointer. Reading initial
        // up front and re-fetching after the dialog closes is the safe
        // pattern. UAT-ANN-130 pins this; the 2026-05-20 HITL pass surfaced
        // the symptom (rectangle vanishes after a Stroke colour pick).
        const int id = m_id;
        QColor initial;
        if (const Annotation *a = m_store->find(id))
            initial = a->style.stroke;
        else
            return;
        const QColor c = QColorDialog::getColor(initial, this, tr("Stroke Colour"));
        if (!c.isValid())
            return;
        const Annotation *a = m_store->find(id);
        if (!a)
            return;
        Annotation updated = *a;
        updated.style.stroke = c;
        m_store->update(updated);
        applySwatch(m_strokeButton, c);
    });

    m_fillButton = new QToolButton(form);
    m_fillButton->setObjectName(QStringLiteral("trailer.inspector.fillButton"));
    m_fillButton->setText(tr("Fill"));
    m_fillButton->setAutoRaise(true);
    layout->addRow(tr("Fill:"), m_fillButton);
    connect(m_fillButton, &QToolButton::clicked, this, [this]() {
        if (!m_store || m_id == 0)
            return;
        // Same pointer-across-modal hazard as the Stroke handler — see
        // the comment above. Re-fetch after the dialog returns.
        const int id = m_id;
        QColor initial;
        if (const Annotation *a = m_store->find(id))
            initial = a->style.fill;
        else
            return;
        const QColor c = QColorDialog::getColor(initial, this, tr("Fill Colour"),
                                                QColorDialog::ShowAlphaChannel);
        if (!c.isValid())
            return;
        const Annotation *a = m_store->find(id);
        if (!a)
            return;
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
    connect(m_strokeWidth, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
                if (m_loading)
                    return;
                if (!m_store || m_id == 0)
                    return;
                const Annotation *a = m_store->find(m_id);
                if (!a)
                    return;
                Annotation updated = *a;
                updated.style.strokeWidth = v;
                m_store->update(updated);
            });

    m_dashCombo = new QComboBox(form);
    m_dashCombo->addItem(tr("Solid"), static_cast<int>(DashStyle::Solid));
    m_dashCombo->addItem(tr("Dashed"), static_cast<int>(DashStyle::Dashed));
    m_dashCombo->addItem(tr("Dotted"), static_cast<int>(DashStyle::Dotted));
    layout->addRow(tr("Dash:"), m_dashCombo);
    connect(m_dashCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_loading)
            return;
        if (!m_store || m_id == 0)
            return;
        const Annotation *a = m_store->find(m_id);
        if (!a)
            return;
        Annotation updated = *a;
        updated.style.dash = static_cast<DashStyle>(m_dashCombo->currentData().toInt());
        m_store->update(updated);
    });

    m_fontSize = new QSpinBox(form);
    m_fontSize->setRange(6, 72);
    m_fontSize->setSuffix(tr(" pt"));
    layout->addRow(tr("Font size:"), m_fontSize);
    connect(m_fontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_loading)
            return;
        if (!m_store || m_id == 0)
            return;
        const Annotation *a = m_store->find(m_id);
        if (!a)
            return;
        Annotation updated = *a;
        updated.style.fontPointSize = v;
        m_store->update(updated);
    });

    m_fontFamily = new QFontComboBox(form);
    layout->addRow(tr("Font:"), m_fontFamily);
    connect(m_fontFamily, &QFontComboBox::currentFontChanged, this, [this](const QFont &f) {
        if (m_loading)
            return;
        if (!m_store || m_id == 0)
            return;
        const Annotation *a = m_store->find(m_id);
        if (!a)
            return;
        Annotation updated = *a;
        updated.style.fontFamily = f.family();
        m_store->update(updated);
    });

    m_fontWeight = new QComboBox(form);
    m_fontWeight->addItem(tr("Light"), static_cast<int>(QFont::Light));
    m_fontWeight->addItem(tr("Regular"), static_cast<int>(QFont::Normal));
    m_fontWeight->addItem(tr("Medium"), static_cast<int>(QFont::Medium));
    m_fontWeight->addItem(tr("Bold"), static_cast<int>(QFont::Bold));
    m_fontWeight->addItem(tr("Black"), static_cast<int>(QFont::Black));
    layout->addRow(tr("Weight:"), m_fontWeight);
    connect(m_fontWeight, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_loading)
            return;
        if (!m_store || m_id == 0)
            return;
        const Annotation *a = m_store->find(m_id);
        if (!a)
            return;
        Annotation updated = *a;
        updated.style.fontWeight = m_fontWeight->currentData().toInt();
        m_store->update(updated);
    });

    m_text = new QPlainTextEdit(form);
    m_text->setPlaceholderText(tr("Body text"));
    m_text->setMaximumHeight(120);
    layout->addRow(tr("Text:"), m_text);
    connect(m_text, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_loading)
            return;
        if (!m_store || m_id == 0)
            return;
        const Annotation *a = m_store->find(m_id);
        if (!a)
            return;
        Annotation updated = *a;
        updated.text = m_text->toPlainText();
        m_store->update(updated);
    });

    m_formIndex = m_stack->addWidget(form);
    m_stack->setCurrentIndex(m_emptyIndex);

    m_tabs->addTab(m_stack, tr("Properties"));

    auto *listTab = new QWidget(m_tabs);
    auto *listLayout = new QVBoxLayout(listTab);
    listLayout->setContentsMargins(4, 4, 4, 4);
    m_annotationList = new QListWidget(listTab);
    listLayout->addWidget(m_annotationList);
    m_tabs->addTab(listTab, tr("Annotations"));
    connect(m_annotationList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (!item || !m_store)
            return;
        const int id = item->data(Qt::UserRole).toInt();
        if (id <= 0)
            return;
        setAnnotation(m_store, id);
        m_tabs->setCurrentIndex(1);
        emit annotationSelected(id);
    });
    connect(m_annotationList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item || !m_store)
            return;
        const int id = item->data(Qt::UserRole).toInt();
        if (id <= 0)
            return;
        emit annotationSelected(id);
    });

    setWidget(m_tabs);
}

void Inspector::setDocument(IDocument *doc) {
    if (m_doc == doc) {
        rebuildDocumentInfo();
        rebuildAnnotationList();
        return;
    }
    if (m_store)
        disconnect(m_store, nullptr, this, nullptr);
    m_doc = doc;
    m_store = doc ? doc->annotations() : nullptr;
    m_id = 0;
    if (m_store) {
        // Qt::UniqueConnection is a no-op for lambdas (Qt rejects the
        // call with a warning), so we rely on the explicit disconnect
        // above to keep this connection unique.
        connect(m_store, &AnnotationStore::changed, this, [this]() {
            rebuildFromStore();
            rebuildAnnotationList();
            rebuildDocumentInfo();
        });
    }
    rebuildDocumentInfo();
    rebuildAnnotationList();
    rebuildFromStore();
}

void Inspector::setAnnotation(AnnotationStore *store, int id) {
    if (store && store != m_store) {
        if (m_store)
            disconnect(m_store, nullptr, this, nullptr);
        m_store = store;
        // See setDocument() for why this isn't Qt::UniqueConnection.
        connect(m_store, &AnnotationStore::changed, this, [this]() {
            rebuildFromStore();
            rebuildAnnotationList();
            rebuildDocumentInfo();
        });
    }
    m_id = id;
    rebuildFromStore();
    rebuildAnnotationList();
}

void Inspector::clearSelection() {
    m_id = 0;
    m_stack->setCurrentIndex(m_emptyIndex);
    rebuildAnnotationList();
}

namespace {
QString typeShortLabel(AnnotationType type) {
    switch (type) {
    case AnnotationType::Rectangle:
        return Inspector::tr("Rect");
    case AnnotationType::Ellipse:
        return Inspector::tr("Ellipse");
    case AnnotationType::Line:
        return Inspector::tr("Line");
    case AnnotationType::Arrow:
        return Inspector::tr("Arrow");
    case AnnotationType::Ink:
        return Inspector::tr("Ink");
    case AnnotationType::Text:
        return Inspector::tr("Text");
    case AnnotationType::Note:
        return Inspector::tr("Note");
    case AnnotationType::Highlight:
        return Inspector::tr("Highlight");
    case AnnotationType::Underline:
        return Inspector::tr("Underline");
    case AnnotationType::StrikeOut:
        return Inspector::tr("Strikeout");
    case AnnotationType::HighlightShape:
        return Inspector::tr("HlShape");
    case AnnotationType::SpeechBubble:
        return Inspector::tr("Bubble");
    case AnnotationType::ZoomLens:
        return Inspector::tr("Lens");
    case AnnotationType::Signature:
        return Inspector::tr("Sig");
    case AnnotationType::Redaction:
        return Inspector::tr("Redact");
    }
    return {};
}
} // namespace

void Inspector::rebuildDocumentInfo() {
    if (!m_doc) {
        m_docNameLabel->setText(QString());
        m_docPathLabel->setText(QString());
        m_docPagesLabel->setText(QString());
        m_docSizeLabel->setText(QString());
        m_docDirtyLabel->setText(QString());
        return;
    }
    m_docNameLabel->setText(m_doc->displayName());
    m_docPathLabel->setText(m_doc->filePath());
    const int pages = m_doc->pageCount();
    m_docPagesLabel->setText(pages > 0 ? QString::number(pages) : tr("—"));
    const QSize px = m_doc->imagePixelSize();
    if (px.isValid() && !px.isEmpty()) {
        m_docSizeLabel->setText(tr("%1 × %2 px").arg(px.width()).arg(px.height()));
    } else {
        m_docSizeLabel->setText(tr("—"));
    }
    m_docDirtyLabel->setText(m_doc->isDirty() ? tr("Modified") : tr("Clean"));
}

void Inspector::rebuildAnnotationList() {
    if (!m_annotationList)
        return;
    const QSignalBlocker blk(m_annotationList);
    m_annotationList->clear();
    if (!m_store)
        return;
    for (const Annotation &a : m_store->annotations()) {
        QString summary = tr("p%1  %2").arg(a.page + 1).arg(typeShortLabel(a.type));
        if (!a.text.isEmpty()) {
            QString t = a.text;
            t.replace(QChar('\n'), QChar(' '));
            if (t.size() > 40)
                t = t.left(40) + QStringLiteral("…");
            summary += QStringLiteral("  \u2014  ") + t;
        }
        auto *item = new QListWidgetItem(summary, m_annotationList);
        item->setData(Qt::UserRole, a.id);
        if (a.id == m_id) {
            m_annotationList->setCurrentItem(item);
        }
    }
}

void Inspector::rebuildFromStore() {
    if (!m_store || m_id == 0) {
        m_stack->setCurrentIndex(m_emptyIndex);
        return;
    }
    const Annotation *a = m_store->find(m_id);
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
    {
        const int idx = m_dashCombo->findData(static_cast<int>(a->style.dash));
        m_dashCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_fontSize->setValue(a->style.fontPointSize > 0 ? a->style.fontPointSize : 12);
    {
        QSignalBlocker blk(m_fontFamily);
        m_fontFamily->setCurrentFont(QFont(a->style.fontFamily));
    }
    {
        const int idx = m_fontWeight->findData(a->style.fontWeight);
        m_fontWeight->setCurrentIndex(idx >= 0 ? idx : 1);
    }
    const bool hasText = a->type == AnnotationType::Text || a->type == AnnotationType::Note ||
                         a->type == AnnotationType::SpeechBubble;
    const bool hasFont = a->type == AnnotationType::Text || a->type == AnnotationType::SpeechBubble;
    m_text->setEnabled(hasText);
    m_fontSize->setEnabled(hasFont);
    m_fontFamily->setEnabled(hasFont);
    m_fontWeight->setEnabled(hasFont);
    if (m_text->toPlainText() != a->text) {
        m_text->setPlainText(a->text);
    }
    m_stack->setCurrentIndex(m_formIndex);
    m_loading = false;
}

} // namespace trailer
