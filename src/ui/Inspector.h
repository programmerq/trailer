#pragma once

#include <QDockWidget>
#include <QPointer>

class QColorDialog;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QPlainTextEdit;
class QSpinBox;
class QStackedWidget;
class QToolButton;

namespace trailer {

class AnnotationStore;

class Inspector : public QDockWidget {
    Q_OBJECT

public:
    explicit Inspector(QWidget* parent = nullptr);

    void setAnnotation(AnnotationStore* store, int id);
    void clearSelection();

private:
    void rebuildFromStore();

    QPointer<AnnotationStore> m_store;
    int m_id = 0;
    bool m_loading = false;

    QStackedWidget* m_stack = nullptr;
    int m_emptyIndex = 0;
    int m_formIndex = 0;

    QLabel* m_pageLabel = nullptr;
    QLabel* m_typeLabel = nullptr;
    QToolButton* m_strokeButton = nullptr;
    QToolButton* m_fillButton = nullptr;
    QDoubleSpinBox* m_strokeWidth = nullptr;
    QComboBox* m_dashCombo = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QFontComboBox* m_fontFamily = nullptr;
    QComboBox* m_fontWeight = nullptr;
    QPlainTextEdit* m_text = nullptr;
};

}  // namespace trailer
