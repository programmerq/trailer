#pragma once

#include <QDialog>

#include <memory>

class QCheckBox;
class QPlainTextEdit;
class QPushButton;

namespace trailer {

class Application;

// The "Feedback / Diagnostic Report" dialog (Help menu). Gathers an
// AppSnapshot (src/diagnostics/FeedbackReport.h) once at construction
// and shows the rendered markdown in a read-only, fully-visible text
// view — never clipboard-only, the user must be able to read
// everything before they paste it anywhere (see PHILOSOPHY.md privacy-
// by-construction). A "Copy to Clipboard" button and an "Include full
// file paths" checkbox (default off — see FeedbackReport.h's privacy
// note on formatMarkdown) re-render the same already-gathered snapshot;
// neither ever re-queries live app state or touches the network.
//
// A QDialog subclass (rather than a bare free function) so a UAT slot
// can construct one directly and inspect its widgets without running
// the modal event loop — see showFeedbackReportDialog() below for the
// production entry point that does run it.
class FeedbackDialog : public QDialog {
    Q_OBJECT

  public:
    explicit FeedbackDialog(Application *app, QWidget *parent = nullptr);
    // Declared (not defaulted) here and defined in the .cpp, after Impl
    // is complete — the classic pimpl requirement for std::unique_ptr<Impl>.
    ~FeedbackDialog() override;

    // Test/introspection seams.
    QString reportText() const;
    QCheckBox *includeFullPathsCheckBox() const { return m_pathsCheck; }
    QPushButton *copyButton() const { return m_copyButton; }

  private:
    void refresh();

    QPlainTextEdit *m_text = nullptr;
    QCheckBox *m_pathsCheck = nullptr;
    QPushButton *m_copyButton = nullptr;
    class Impl; // owns the immutable AppSnapshot (pimpl keeps the Qt
                // moc header free of the diagnostics/ include)
    std::unique_ptr<Impl> m_impl;
};

// Constructs a FeedbackDialog and runs it modally. Synchronous; returns
// when the user closes the dialog. This is what the Help menu calls.
void showFeedbackReportDialog(QWidget *parent, Application *app);

} // namespace trailer
