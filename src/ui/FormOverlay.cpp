#include "FormOverlay.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QResizeEvent>

namespace trailer {

FormOverlay::FormOverlay(QWidget *parent) : QWidget(parent) {
    // Transparent, non-intercepting background: mouse events that land
    // on a field widget go to that widget; events between fields pass
    // through to the viewport (scroll / pan).
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    // Faint blue outline on every field so the user can see what's
    // editable without having to hover. The transparent background
    // lets the underlying PDF appearance show through; on focus we
    // darken the outline so the active field is easy to track. The
    // selector list covers the only widget classes createWidgetForField
    // returns — extend it when adding new field types.
    setStyleSheet(QStringLiteral("QLineEdit, QComboBox {"
                                 "  border: 1px solid rgba(0, 100, 200, 80);"
                                 "  background: transparent;"
                                 "  selection-background-color: rgba(0, 100, 200, 80);"
                                 "}"
                                 "QLineEdit:focus, QComboBox:focus {"
                                 "  border: 1px solid rgba(0, 100, 200, 220);"
                                 "  background: rgba(255, 255, 255, 200);"
                                 "}"));
}

void FormOverlay::setDocumentToView(DocToView fn) {
    m_docToView = std::move(fn);
}

void FormOverlay::setPageSize(PageSizeFn fn) {
    m_pageSize = std::move(fn);
}

void FormOverlay::setFields(const std::vector<FormField> &fields) {
    clearWidgets();
    m_fields = fields;
    for (const FormField &ff : m_fields) {
        if (ff.readOnly)
            continue; // read-only fields are visible
                      // in the rendered PDF; skip them
        if (ff.type == FormFieldType::Unknown)
            continue;

        QWidget *w = createWidgetForField(ff);
        if (!w)
            continue;
        w->setParent(this);
        w->show();
        m_widgets.emplace_back(ff.id, w);
    }
    // Tab moves between fields in reading order: page asc, then top of
    // the page first (PDF y is bottom-left origin so "top" is the
    // larger y), then left to right. The order m_fields arrived in is
    // /AcroForm tree order, which is set by the form designer and
    // often does not match what a human sees scanning the page —
    // re-sort on visual position. The widgets themselves stay in
    // m_widgets by id; we only rewire the focus chain.
    std::vector<std::pair<int, QWidget *>> ordered = m_widgets;
    auto fieldFor = [this](int id) -> const FormField * {
        for (const auto &f : m_fields)
            if (f.id == id)
                return &f;
        return nullptr;
    };
    std::sort(ordered.begin(), ordered.end(), [&fieldFor](const auto &a, const auto &b) {
        const FormField *fa = fieldFor(a.first);
        const FormField *fb = fieldFor(b.first);
        if (!fa || !fb)
            return false;
        if (fa->page != fb->page)
            return fa->page < fb->page;
        const double ya = fa->rectPts.center().y();
        const double yb = fb->rectPts.center().y();
        // Higher PDF y = higher on the page; sort descending so the
        // top-most field is first in the tab cycle.
        if (ya != yb)
            return ya > yb;
        return fa->rectPts.center().x() < fb->rectPts.center().x();
    });
    for (size_t i = 1; i < ordered.size(); ++i) {
        QWidget::setTabOrder(ordered[i - 1].second, ordered[i].second);
    }

    relayout();
}

void FormOverlay::relayout() {
    for (auto &[id, w] : m_widgets) {
        // Find matching field by id.
        const FormField *ff = nullptr;
        for (const auto &f : m_fields) {
            if (f.id == id) {
                ff = &f;
                break;
            }
        }
        if (!ff)
            continue;
        const QRect rect = fieldRectToViewport(*ff);
        w->setGeometry(rect);
        // Keep widgets that map outside the current viewport hidden
        // so they don't linger at invalid positions.
        w->setVisible(this->rect().intersects(rect));
    }
}

void FormOverlay::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    relayout();
}

void FormOverlay::clearWidgets() {
    for (auto &[id, w] : m_widgets) {
        w->deleteLater();
    }
    m_widgets.clear();
    m_fields.clear();
}

QWidget *FormOverlay::createWidgetForField(const FormField &field) {
    switch (field.type) {
    case FormFieldType::Text: {
        auto *edit = new QLineEdit(this);
        edit->setText(field.value);
        if (field.isPassword)
            edit->setEchoMode(QLineEdit::Password);
        // Propagate changes back to the document.
        const int id = field.id;
        connect(edit, &QLineEdit::editingFinished, this,
                [this, id, edit]() { emit fieldValueChanged(id, edit->text()); });
        return edit;
    }
    case FormFieldType::Checkbox: {
        auto *cb = new QCheckBox(this);
        cb->setChecked(field.value == QStringLiteral("Yes"));
        // Make the checkbox background match the PDF; the actual
        // rendering shows the PDF's appearance stream around it.
        cb->setStyleSheet(QStringLiteral("QCheckBox { background: transparent; }"));
        const int id = field.id;
        connect(cb, &QCheckBox::toggled, this, [this, id](bool checked) {
            emit fieldValueChanged(id, checked ? QStringLiteral("Yes") : QStringLiteral("Off"));
        });
        return cb;
    }
    case FormFieldType::Dropdown: {
        auto *combo = new QComboBox(this);
        combo->addItems(field.options);
        const qsizetype idx = field.options.indexOf(field.value);
        if (idx >= 0)
            combo->setCurrentIndex(static_cast<int>(idx));
        const int id = field.id;
        connect(combo, &QComboBox::currentTextChanged, this,
                [this, id](const QString &text) { emit fieldValueChanged(id, text); });
        return combo;
    }
    case FormFieldType::RadioButton:
    case FormFieldType::Unknown:
    default:
        return nullptr;
    }
}

QRect FormOverlay::fieldRectToViewport(const FormField &field) const {
    if (!m_docToView || !m_pageSize)
        return {};
    const QSizeF pageSz = m_pageSize(field.page);
    if (pageSz.isEmpty())
        return {};

    // PDF rects use bottom-left origin. Convert to doc-native
    // (top-left origin) before passing through docToView.
    const QRectF &r = field.rectPts;
    const double docX = r.left();
    const double docY = pageSz.height() - r.top() - r.height();

    const QPointF tl = m_docToView({docX, docY}, field.page);
    const QPointF br = m_docToView({docX + r.width(), docY + r.height()}, field.page);
    return QRect(tl.toPoint(), br.toPoint()).normalized();
}

} // namespace trailer
