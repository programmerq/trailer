#pragma once

#include "Annotation.h"

#include <QObject>

#include <vector>

namespace trailer {

class AnnotationStore : public QObject {
    Q_OBJECT

public:
    explicit AnnotationStore(QObject* parent = nullptr);

    int add(Annotation annotation);
    bool remove(int id);
    // Remove all annotations whose id appears in `ids` in a single
    // undo-history step. Returns true if at least one was removed.
    bool removeMultiple(const std::vector<int>& ids);
    bool update(const Annotation& annotation);
    const Annotation* find(int id) const;

    const std::vector<Annotation>& annotations() const { return m_annotations; }
    std::vector<Annotation> annotationsOnPage(int page) const;
    int count() const { return static_cast<int>(m_annotations.size()); }
    bool isEmpty() const { return m_annotations.empty(); }
    void clear();

    // Snapshot support for undo/redo. Callers take an opaque snapshot before
    // mutating, and restore on undo.
    std::vector<Annotation> snapshot() const { return m_annotations; }
    void restore(std::vector<Annotation> snapshot);

    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }
    void undo();
    void redo();
    void clearHistory() { m_undoStack.clear(); m_redoStack.clear(); }

signals:
    void changed();

private:
    void pushHistory();

    std::vector<Annotation> m_annotations;
    std::vector<std::vector<Annotation>> m_undoStack;
    std::vector<std::vector<Annotation>> m_redoStack;
    int m_nextId = 1;
    static constexpr size_t kMaxUndo = 64;
};

}  // namespace trailer
