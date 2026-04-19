#include "AnnotationStore.h"

#include <algorithm>

namespace trailer {

AnnotationStore::AnnotationStore(QObject* parent) : QObject(parent) {}

int AnnotationStore::add(Annotation annotation) {
    annotation.id = m_nextId++;
    m_annotations.push_back(std::move(annotation));
    emit changed();
    return m_annotations.back().id;
}

bool AnnotationStore::remove(int id) {
    auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
        [id](const Annotation& a) { return a.id == id; });
    if (it == m_annotations.end()) return false;
    m_annotations.erase(it);
    emit changed();
    return true;
}

bool AnnotationStore::update(const Annotation& annotation) {
    auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
        [&](const Annotation& a) { return a.id == annotation.id; });
    if (it == m_annotations.end()) return false;
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
    m_annotations.clear();
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
