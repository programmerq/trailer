#pragma once

#include <QDockWidget>
#include <QPointer>

class QColorDialog;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QToolButton;

namespace trailer {

class AnnotationStore;
class IDocument;

class Inspector : public QDockWidget {
    Q_OBJECT

  public:
    explicit Inspector(QWidget *parent = nullptr);

    void setDocument(IDocument *doc);
    void setAnnotation(AnnotationStore *store, int id);
    void clearSelection();

  signals:
    void annotationSelected(int id);

  private:
    void rebuildFromStore();
    void rebuildDocumentInfo();
    void rebuildAnnotationList();

    QPointer<AnnotationStore> m_store;
    IDocument *m_doc = nullptr;
    int m_id = 0;
    bool m_loading = false;

    QTabWidget *m_tabs = nullptr;

    QLabel *m_docNameLabel = nullptr;
    QLabel *m_docPathLabel = nullptr;
    QLabel *m_docPagesLabel = nullptr;
    QLabel *m_docSizeLabel = nullptr;
    QLabel *m_docDirtyLabel = nullptr;

    QStackedWidget *m_stack = nullptr;
    int m_emptyIndex = 0;
    int m_formIndex = 0;

    QLabel *m_pageLabel = nullptr;
    QLabel *m_typeLabel = nullptr;
    QToolButton *m_strokeButton = nullptr;
    QToolButton *m_fillButton = nullptr;
    QDoubleSpinBox *m_strokeWidth = nullptr;
    QComboBox *m_dashCombo = nullptr;
    QSpinBox *m_fontSize = nullptr;
    QFontComboBox *m_fontFamily = nullptr;
    QComboBox *m_fontWeight = nullptr;
    QPlainTextEdit *m_text = nullptr;

    QListWidget *m_annotationList = nullptr;
};

} // namespace trailer
