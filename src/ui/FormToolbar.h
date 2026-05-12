#pragma once

#include "annotation/Annotation.h"

#include <QToolBar>

class QAction;
class QActionGroup;

namespace trailer {

// Lightweight toolbar for filling PDFs that lack real AcroForm fields.
// Per DESIGN.md §6.4: "a 'Text Box' tool that lets the user place a
// text box for free-typing on PDFs that lack proper form fields."
//
// Separate from MarkupToolbar so "filling" and "marking up" stay
// conceptually distinct in the UI — a user can show only this toolbar
// when they're signing a printed-then-scanned form and don't want the
// full annotation palette in their way.
//
// Implementation is thin: each tool maps to a matching AnnotationTool.
// The Checkmark / X tools pre-seed the next-placed Text annotation's
// content with ✓ / ✗ glyphs (wired via the pendingText signal).
class FormToolbar : public QToolBar {
    Q_OBJECT

public:
    explicit FormToolbar(QWidget* parent = nullptr);

    AnnotationTool activeTool() const { return m_tool; }
    // Returns the text that should be pre-populated for the next Text
    // annotation when a Checkmark/X tool is active. Empty for plain
    // Text Box (prompts the user to type).
    QString pendingText() const { return m_pendingText; }

signals:
    // Emitted whenever the user switches tools. Subscribers call
    // doc->setAnnotationTool(tool) and prime doc->setPendingText(text)
    // so the next click on the canvas drops the correct glyph.
    void toolChanged(AnnotationTool tool, const QString& pendingText);
    // Emitted when the user clicks "AutoFill" — wired to the
    // AutoFill-cards feature. Handled in MainWindow.
    void autoFillRequested();
    // Emitted when the user clicks "Sign Here" — wired to the Sign
    // tool feature (pick saved signature, place, flatten).
    void signHereRequested();

private:
    QAction* makeToolAction(const QString& label, AnnotationTool tool,
                            const QString& pendingText = {},
                            const QString& iconResource = QString());

    QActionGroup* m_group = nullptr;
    AnnotationTool m_tool = AnnotationTool::None;
    QString m_pendingText;
    QAction* m_autoFillAction = nullptr;
    QAction* m_signHereAction = nullptr;
};

}  // namespace trailer
