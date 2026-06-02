#include "AnnotationStore.h"

#include <algorithm>
#include <unordered_set>

namespace trailer {

AnnotationStore::AnnotationStore(QObject *parent) : QObject(parent) {}

int AnnotationStore::add(Annotation annotation) {
    pushHistory();
    annotation.id = m_nextId++;
    m_annotations.push_back(std::move(annotation));
    emit changed();
    return m_annotations.back().id;
}

bool AnnotationStore::remove(int id) {
    auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
                           [id](const Annotation &a) { return a.id == id; });
    if (it == m_annotations.end())
        return false;
    pushHistory();
    m_annotations.erase(it);
    emit changed();
    return true;
}

bool AnnotationStore::removeMultiple(const std::vector<int> &ids) {
    if (ids.empty())
        return false;
    const std::unordered_set<int> toRemove(ids.begin(), ids.end());
    const bool anyFound =
        std::any_of(m_annotations.begin(), m_annotations.end(),
                    [&](const Annotation &a) { return toRemove.count(a.id) > 0; });
    if (!anyFound)
        return false;
    pushHistory();
    m_annotations.erase(
        std::remove_if(m_annotations.begin(), m_annotations.end(),
                       [&](const Annotation &a) { return toRemove.count(a.id) > 0; }),
        m_annotations.end());
    emit changed();
    return true;
}

bool AnnotationStore::update(const Annotation &annotation) {
    auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
                           [&](const Annotation &a) { return a.id == annotation.id; });
    if (it == m_annotations.end())
        return false;
    pushHistory();
    *it = annotation;
    emit changed();
    return true;
}

const Annotation *AnnotationStore::find(int id) const {
    auto it = std::find_if(m_annotations.begin(), m_annotations.end(),
                           [id](const Annotation &a) { return a.id == id; });
    return it == m_annotations.end() ? nullptr : &*it;
}

std::vector<Annotation> AnnotationStore::annotationsOnPage(int page) const {
    std::vector<Annotation> out;
    out.reserve(m_annotations.size());
    for (const Annotation &a : m_annotations) {
        if (a.page == page)
            out.push_back(a);
    }
    return out;
}

void AnnotationStore::clear() {
    if (m_annotations.empty())
        return;
    pushHistory();
    m_annotations.clear();
    emit changed();
}

void AnnotationStore::pushHistory() {
    // While a compound gesture is in flight, callers (e.g. the drag
    // path in mouseMoveEvent) mutate the store every frame. We only
    // want one undo frame for the whole gesture, captured from the
    // first mutation's pre-state — so the first call inside the
    // compound pushes, and the rest are no-ops.
    if (m_compoundDepth > 0) {
        if (m_compoundSnapshotPushed)
            return;
        m_compoundSnapshotPushed = true;
        // Fall through to the push below — the first mutation's
        // pre-state is the right thing to revert to.
    }
    m_undoStack.push_back({m_annotations, m_nextId});
    if (m_undoStack.size() > kMaxUndo) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
    emit historyPushed();
}

void AnnotationStore::undo() {
    if (m_undoStack.empty())
        return;
    m_redoStack.push_back({std::move(m_annotations), m_nextId});
    m_annotations = std::move(m_undoStack.back().annotations);
    // m_nextId must stay monotonically non-decreasing: an undo that
    // removes an annotation must NOT make the next add reuse the
    // removed id. (UAT-ANN-064 / redoStackClearsOnNewMutation pins
    // this — after undo + new add, the new annotation must have a
    // fresh id that find()s of the old id return nullptr for.)
    m_nextId = std::max(m_nextId, m_undoStack.back().nextId);
    m_undoStack.pop_back();
    emit changed();
}

void AnnotationStore::redo() {
    if (m_redoStack.empty())
        return;
    m_undoStack.push_back({std::move(m_annotations), m_nextId});
    m_annotations = std::move(m_redoStack.back().annotations);
    m_nextId = std::max(m_nextId, m_redoStack.back().nextId);
    m_redoStack.pop_back();
    emit changed();
}

void AnnotationStore::restore(std::vector<Annotation> snapshot) {
    m_annotations = std::move(snapshot);
    int maxId = 0;
    for (const Annotation &a : m_annotations) {
        maxId = std::max(maxId, a.id);
    }
    m_nextId = maxId + 1;
    emit changed();
}

void AnnotationStore::beginCompound() {
    // Lazy-push design: nothing is appended to the undo stack here.
    // The first pushHistory() call inside the compound captures the
    // pre-mutation snapshot; if no mutation happens before
    // endCompound(), the undo stack is left untouched and the user
    // sees no phantom "no-op" undo frame.
    //
    // Nested begin/end is collapsed: only the outermost pair gates
    // the lazy-push flag. This matters for callers that compose
    // update()+update() inside a higher-level gesture; nesting must
    // not split the one gesture into two history frames.
    if (m_compoundDepth == 0) {
        m_compoundSnapshotPushed = false;
    }
    ++m_compoundDepth;
}

void AnnotationStore::endCompound() {
    if (m_compoundDepth == 0)
        return;
    --m_compoundDepth;
    if (m_compoundDepth == 0) {
        m_compoundSnapshotPushed = false;
    }
    // Intentionally do not emit changed() here — every mutation
    // inside the compound already emitted on its own. The single
    // pre-gesture frame on the undo stack (if any) is the right
    // thing to revert to.
}

} // namespace trailer
