#include "AnnotationStore.h"

#include <algorithm>

namespace trailer {

AnnotationStore::AnnotationStore(QObject* parent) : QObject(parent) {}

int AnnotationStore::add(Annotation annotation) {
    pushHistory();
    annotation.id = m_nextId++;
    m_annotations.push_back(std::move(annotation));
    emit changed();
    return m_annotations.back().id;
}

bool AnnotationStore::remove(int id) {
    auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
        [id](const Annotation& a) { return a.id == id; });
    if (it == m_annotations.end()) return false;
    pushHistory();
    m_annotations.erase(it);
    emit changed();
    return true;
}

bool AnnotationStore::update(const Annotation& annotation) {
    auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
        [&](const Annotation& a) { return a.id == annotation.id; });
    if (it == m_annotations.end()) return false;
    pushHistory();
    *it = annotation;
    emit changed();
    return true;
}

const Annotation* AnnotationStore::find(int id) const {
    auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
        [id](const Annotation& a) { return a.id == id; });
    return it == m_annotations.end() ? nullptr : &*it;
}

std::vector<Annotation> AnnotationStore::annotationsOnPage(int page) const {
    std::vector<Annotation> out;
    out.reserve(m_annotations.size());
    for (const Annotation& a : m_annotations) {
        if (a.page == page) out.push_back(a);
    }
    return out;
}

void AnnotationStore::clear() {
    if (m_annotations.empty()) return;
    pushHistory();
    m_annotations.clear();
    emit changed();
}

void AnnotationStore::pushHistory() {
    m_undoStack.push_back(m_annotations);
    if (m_undoStack.size() > kMaxUndo) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
}

void AnnotationStore::undo() {
    if (m_undoStack.empty()) return;
    m_redoStack.push_back(std::move(m_annotations));
    m_annotations = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    int maxId = 0;
    for (const Annotation& a : m_annotations) maxId = std::max(maxId, a.id);
    m_nextId = std::max(m_nextId, maxId + 1);
    emit changed();
}

void AnnotationStore::redo() {
    if (m_redoStack.empty()) return;
    m_undoStack.push_back(std::move(m_annotations));
    m_annotations = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    int maxId = 0;
    for (const Annotation& a : m_annotations) maxId = std::max(maxId, a.id);
    m_nextId = std::max(m_nextId, maxId + 1);
    emit changed();
}

void AnnotationStore::restore(std::vector<Annotation> snapshot) {
    m_annotations = std::move(snapshot);
    int maxId = 0;
    for (const Annotation& a : m_annotations) {
        maxId = std::max(maxId, a.id);
    }
    m_nextId = maxId + 1;
    emit changed();
}

}  // namespace trailer
