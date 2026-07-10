#pragma once

#include "Annotation.h"

#include <QObject>

#include <algorithm>
#include <vector>

namespace trailer {

class AnnotationStore : public QObject {
    Q_OBJECT

  public:
    explicit AnnotationStore(QObject *parent = nullptr);

    int add(Annotation annotation);
    bool remove(int id);
    // Remove all annotations whose id appears in `ids` in a single
    // undo-history step. Returns true if at least one was removed.
    bool removeMultiple(const std::vector<int> &ids);
    bool update(const Annotation &annotation);
    const Annotation *find(int id) const;

    const std::vector<Annotation> &annotations() const { return m_annotations; }
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
    // Drop the redo stack without touching undo. Used by an owner
    // coordinating a cross-stack undo log: a new edit on the *other*
    // stack must invalidate annotation redo too.
    void clearRedo() { m_redoStack.clear(); }
    void clearHistory() {
        m_undoStack.clear();
        m_redoStack.clear();
        // Reset the compound state defensively. Called on document
        // load before any drag is in flight, but a future caller
        // running this mid-drag should still leave a sane store.
        m_compoundDepth = 0;
        m_compoundSnapshotPushed = false;
    }

    // Compound gesture support. The overlay drives this from a drag
    // (move or resize): a 60-frame drag would otherwise push 60
    // history frames and a single Ctrl+Z would unwind the drag in
    // micro-steps.
    //
    // beginCompound() enters compound mode but does NOT push a
    // snapshot eagerly — that only happens on the first mutation
    // within the gesture (lazy push, so a "begin-then-end with no
    // mutations" leaves the undo stack unchanged).  Subsequent
    // mutations skip pushHistory().  endCompound() ends the gesture
    // so the next mutation pushes normally.
    //
    // Nested begin/end is collapsed (only the outermost pair counts).
    // applicationStateChanged → abortInFlightDrag in the overlay must
    // call endCompound() so a Cmd-Tab mid-drag leaves the store in a
    // sane state with one undo step (the pre-gesture snapshot, if any
    // mutation happened).
    void beginCompound();
    void endCompound();
    bool inCompound() const { return m_compoundDepth > 0; }

    // Test seam only: shrink the undo depth cap so eviction (and the
    // historyEvicted contract above) can be exercised without pushing
    // hundreds of frames. Production code must not call this — the
    // default depth is a deliberate memory/usability trade-off.
    void setMaxUndoDepth(size_t depth) { m_maxUndoDepth = std::max<size_t>(1, depth); }
    size_t maxUndoDepth() const { return m_maxUndoDepth; }

  signals:
    void changed();
    // Emitted exactly when a real undo frame is pushed — one per
    // gesture (compound-coalesced), and NOT by undo()/redo()/restore().
    // Lets an owner mirror annotation edits into a unified cross-stack
    // undo log without mistaking an undo for a new edit.
    void historyPushed();
    // Emitted (synchronously, before the matching historyPushed) when a
    // push forces the oldest undo frame out to stay within the depth
    // cap. An owner mirroring this store into a unified cross-stack
    // undo log MUST drop its oldest annotation entry on this signal —
    // otherwise the log claims more annotation undos than the store
    // can perform and undo silently no-ops at the tail.
    void historyEvicted();

  private:
    void pushHistory();

    std::vector<Annotation> m_annotations;
    // Each undo frame carries both the annotation list and the m_nextId
    // that was active when the frame was captured. Tracking nextId
    // alongside drops the O(N) rescan in undo()/redo() that otherwise
    // walked m_annotations to find the maximum id after every pop.
    struct HistoryFrame {
        std::vector<Annotation> annotations;
        int nextId;
    };
    std::vector<HistoryFrame> m_undoStack;
    std::vector<HistoryFrame> m_redoStack;
    int m_nextId = 1;
    // Compound depth: the number of beginCompound() calls not yet
    // matched by endCompound(). When > 0, update() / remove() etc.
    // skip pushHistory() so a multi-step gesture is one undo frame.
    int m_compoundDepth = 0;
    // The pre-gesture snapshot for the in-flight compound, captured
    // lazily on the first pushHistory() inside the compound. When
    // m_compoundDepth returns to 0 with m_compoundSnapshotPushed
    // still false, no mutations occurred and the undo stack is
    // untouched — the user gets no phantom "no-op" undo step.
    bool m_compoundSnapshotPushed = false;

    // Cap on retained snapshots per direction. Each snapshot is one
    // std::vector<Annotation> copy — Annotation is a small value
    // struct, so 128 frames of a busy 50-annotation document stay in
    // the low single-digit MB range. 128 (not 64) so a long markup
    // session doesn't hit eviction mid-flow; owners that mirror this
    // store into a unified log stay consistent past the cap via
    // historyEvicted(). Bump if a real workflow runs out of undo;
    // lower if memory profiling points at AnnotationStore.
    static constexpr size_t kDefaultMaxUndoDepth = 128;
    size_t m_maxUndoDepth = kDefaultMaxUndoDepth;
};

} // namespace trailer
