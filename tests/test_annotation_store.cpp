#include "annotation/AnnotationStore.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace trailer;

class TestAnnotationStore : public QObject {
    Q_OBJECT
  private slots:
    void addAssignsMonotonicIds();
    void findLocatesById();
    void removeErasesEntry();
    void removeMultipleErasesAllInOnce();
    void updateReplacesInPlace();
    void annotationsOnPageFiltersByPage();
    void restoreBringsBackPriorSnapshot();
    void changedSignalFiresOnMutations();
    void undoRedoReversesAddRemoveUpdate();
    void redoStackClearsOnNewMutation();
    void compoundCollapsesMultipleUpdatesIntoOneUndo();
    void compoundWithoutMutationsLeavesUndoStackUntouched();
    void compoundNestedBeginEndBehavesAsSingle();
    void compoundAbortLeavesStoreSaneOnNextMutation();
    void setMaxUndoDepthTrimsExistingFramesInLockstep();
    void undoIsFastForLargeStore();
};

namespace {
Annotation makeRect(int page, QRectF bounds) {
    Annotation a;
    a.page = page;
    a.type = AnnotationType::Rectangle;
    a.bounds = bounds;
    return a;
}
} // namespace

void TestAnnotationStore::addAssignsMonotonicIds() {
    AnnotationStore store;
    const int id1 = store.add(makeRect(0, QRectF(0, 0, 10, 10)));
    const int id2 = store.add(makeRect(0, QRectF(5, 5, 10, 10)));
    QVERIFY(id1 > 0);
    QVERIFY(id2 > id1);
    QCOMPARE(store.count(), 2);
}

void TestAnnotationStore::findLocatesById() {
    AnnotationStore store;
    const int id = store.add(makeRect(1, QRectF(1, 2, 3, 4)));
    const Annotation *hit = store.find(id);
    QVERIFY(hit != nullptr);
    QCOMPARE(hit->page, 1);
    QCOMPARE(hit->bounds, QRectF(1, 2, 3, 4));
    QCOMPARE(store.find(9999), nullptr);
}

void TestAnnotationStore::removeErasesEntry() {
    AnnotationStore store;
    const int id = store.add(makeRect(0, QRectF()));
    QVERIFY(store.remove(id));
    QCOMPARE(store.count(), 0);
    QVERIFY(!store.remove(id));
}

void TestAnnotationStore::removeMultipleErasesAllInOnce() {
    AnnotationStore store;
    const int id1 = store.add(makeRect(0, QRectF(0, 0, 10, 10)));
    const int id2 = store.add(makeRect(0, QRectF(5, 5, 10, 10)));
    const int id3 = store.add(makeRect(0, QRectF(20, 20, 5, 5)));
    QCOMPARE(store.count(), 3);

    // Remove two of the three in a single call.
    QVERIFY(store.removeMultiple({id1, id3}));
    QCOMPARE(store.count(), 1);
    QVERIFY(store.find(id1) == nullptr);
    QVERIFY(store.find(id2) != nullptr);
    QVERIFY(store.find(id3) == nullptr);

    // The whole batch is one undo step — a single undo restores both.
    QVERIFY(store.canUndo());
    store.undo();
    QCOMPARE(store.count(), 3);
    QVERIFY(store.find(id1) != nullptr);
    QVERIFY(store.find(id3) != nullptr);

    // Empty id list is a no-op and returns false.
    QVERIFY(!store.removeMultiple({}));
    // All-missing ids is also a no-op.
    QVERIFY(!store.removeMultiple({9998, 9999}));
}

void TestAnnotationStore::updateReplacesInPlace() {
    AnnotationStore store;
    Annotation a = makeRect(0, QRectF(0, 0, 10, 10));
    const int id = store.add(a);
    a.id = id;
    a.bounds = QRectF(10, 10, 20, 20);
    a.text = "annotated";
    QVERIFY(store.update(a));
    const Annotation *hit = store.find(id);
    QVERIFY(hit);
    QCOMPARE(hit->bounds, QRectF(10, 10, 20, 20));
    QCOMPARE(hit->text, QStringLiteral("annotated"));

    Annotation missing = makeRect(0, QRectF());
    missing.id = 9999;
    QVERIFY(!store.update(missing));
}

void TestAnnotationStore::annotationsOnPageFiltersByPage() {
    AnnotationStore store;
    store.add(makeRect(0, QRectF()));
    store.add(makeRect(2, QRectF()));
    store.add(makeRect(2, QRectF()));
    QCOMPARE(store.annotationsOnPage(0).size(), size_t{1});
    QCOMPARE(store.annotationsOnPage(2).size(), size_t{2});
    QCOMPARE(store.annotationsOnPage(5).size(), size_t{0});
}

void TestAnnotationStore::restoreBringsBackPriorSnapshot() {
    AnnotationStore store;
    const int id1 = store.add(makeRect(0, QRectF(0, 0, 10, 10)));
    const auto snap = store.snapshot();
    store.add(makeRect(1, QRectF(0, 0, 5, 5)));
    QCOMPARE(store.count(), 2);
    store.restore(snap);
    QCOMPARE(store.count(), 1);
    QVERIFY(store.find(id1) != nullptr);

    const int idAfter = store.add(makeRect(0, QRectF()));
    QVERIFY(idAfter > id1);
}

void TestAnnotationStore::changedSignalFiresOnMutations() {
    AnnotationStore store;
    QSignalSpy spy(&store, &AnnotationStore::changed);
    const int id = store.add(makeRect(0, QRectF()));
    QCOMPARE(spy.count(), 1);
    store.remove(id);
    QCOMPARE(spy.count(), 2);
    store.clear();
    QCOMPARE(spy.count(), 2); // no-op when already empty
}

void TestAnnotationStore::undoRedoReversesAddRemoveUpdate() {
    AnnotationStore store;
    QVERIFY(!store.canUndo());
    QVERIFY(!store.canRedo());

    const int id = store.add(makeRect(0, QRectF(0, 0, 10, 10)));
    QVERIFY(store.canUndo());
    QCOMPARE(store.count(), 1);

    Annotation updated = *store.find(id);
    updated.bounds = QRectF(1, 2, 3, 4);
    QVERIFY(store.update(updated));
    QCOMPARE(store.find(id)->bounds, QRectF(1, 2, 3, 4));

    store.undo(); // revert update
    QCOMPARE(store.find(id)->bounds, QRectF(0, 0, 10, 10));
    QVERIFY(store.canRedo());

    store.undo(); // revert add
    QCOMPARE(store.count(), 0);

    store.redo(); // re-add
    QCOMPARE(store.count(), 1);
    QVERIFY(store.find(id) != nullptr);

    store.redo(); // re-apply update
    QCOMPARE(store.find(id)->bounds, QRectF(1, 2, 3, 4));

    store.undo();
    store.undo();
    QVERIFY(store.remove(id) == false); // already gone
}

void TestAnnotationStore::redoStackClearsOnNewMutation() {
    AnnotationStore store;
    const int id = store.add(makeRect(0, QRectF(0, 0, 5, 5)));
    store.undo();
    QVERIFY(store.canRedo());
    store.add(makeRect(0, QRectF(5, 5, 5, 5)));
    QVERIFY(!store.canRedo());
    QCOMPARE(store.count(), 1);
    QVERIFY(store.find(id) == nullptr);
}

// Workstream D3 — a "user gesture" wrapped in beginCompound/endCompound
// produces exactly one undo step regardless of how many update() calls
// fired during the gesture. Mirrors the 60-frame drag case in the
// AnnotationOverlay: 60 update()s should collapse into one Ctrl+Z.
void TestAnnotationStore::compoundCollapsesMultipleUpdatesIntoOneUndo() {
    AnnotationStore store;
    const int id = store.add(makeRect(0, QRectF(0, 0, 10, 10)));
    const QRectF original = store.find(id)->bounds;
    QCOMPARE(store.count(), 1);

    store.beginCompound();
    // Simulate a 50-frame drag.
    for (int i = 0; i < 50; ++i) {
        Annotation a = *store.find(id);
        a.bounds = QRectF(QPointF(double(i), double(i)), QSizeF(10, 10));
        QVERIFY(store.update(a));
    }
    store.endCompound();

    QCOMPARE(store.find(id)->bounds, QRectF(QPointF(49.0, 49.0), QSizeF(10, 10)));
    QVERIFY(store.canUndo());

    // Exactly ONE undo step reverts the whole drag to the pre-gesture
    // state.  This is the user-visible behaviour the workstream is
    // pinning: pressing Ctrl+Z after dragging should not unwind 50
    // individual sub-steps.
    store.undo();
    QCOMPARE(store.find(id)->bounds, original);
    // The add() that created the annotation still has its own undo
    // step; canUndo() is therefore still true, but the in-place
    // edits collapse into one revertible frame, not 50.
    QVERIFY(store.canUndo());
    QVERIFY(store.find(id) != nullptr);
}

// A compound that begins and ends without any mutation leaves the
// history exactly as it was — no phantom no-op undo frame appears.
// Drives the click-without-drag case: the overlay begins a compound
// at mousePress to be ready for a move, but if the user releases
// without moving there should be nothing to undo.
void TestAnnotationStore::compoundWithoutMutationsLeavesUndoStackUntouched() {
    AnnotationStore store;
    store.add(makeRect(0, QRectF()));
    QVERIFY(store.canUndo());

    // Drain the undo stack — only the add() should be on it.
    store.beginCompound();
    store.endCompound();
    QVERIFY(store.canUndo()); // still revertible (the add())

    store.undo();
    QCOMPARE(store.count(), 0);
    // No second undo step exists — the empty compound did not push.
    QVERIFY(!store.canUndo());
}

// Nested beginCompound/endCompound pairs collapse into a single
// gesture — the outer pair is the one that gates lazy-push. Models
// composite handlers that internally compose update() chains.
void TestAnnotationStore::compoundNestedBeginEndBehavesAsSingle() {
    AnnotationStore store;
    const int id = store.add(makeRect(0, QRectF(0, 0, 10, 10)));

    store.beginCompound();
    {
        store.beginCompound();
        Annotation a = *store.find(id);
        a.bounds = QRectF(5, 5, 10, 10);
        QVERIFY(store.update(a));
        store.endCompound();
    }
    // Second mutation outside the inner pair but inside the outer.
    {
        Annotation a = *store.find(id);
        a.bounds = QRectF(10, 10, 10, 10);
        QVERIFY(store.update(a));
    }
    store.endCompound();

    // Exactly one undo step covers BOTH mutations.
    QCOMPARE(store.find(id)->bounds, QRectF(10, 10, 10, 10));
    store.undo();
    QCOMPARE(store.find(id)->bounds, QRectF(0, 0, 10, 10));
}

// Simulates a Cmd-Tab mid-drag: beginCompound, a couple of mutations,
// then endCompound (NOT another beginCompound). Subsequent mutations
// outside the abandoned gesture must push their own undo step so the
// user's next action is still individually revertible.
void TestAnnotationStore::compoundAbortLeavesStoreSaneOnNextMutation() {
    AnnotationStore store;
    const int id = store.add(makeRect(0, QRectF(0, 0, 10, 10)));
    QCOMPARE(store.count(), 1);

    store.beginCompound();
    Annotation a = *store.find(id);
    a.bounds = QRectF(5, 5, 10, 10);
    QVERIFY(store.update(a));
    // Drag aborted by app-deactivate: overlay calls endCompound()
    // from its abortInFlightDrag path.
    store.endCompound();

    // The next user action must push its own history frame.
    Annotation b = *store.find(id);
    b.bounds = QRectF(50, 50, 10, 10);
    QVERIFY(store.update(b));

    // Two undo steps: revert the post-abort update, then revert the
    // compound (drag) to the pre-drag state.
    store.undo();
    QCOMPARE(store.find(id)->bounds, QRectF(5, 5, 10, 10));
    store.undo();
    QCOMPARE(store.find(id)->bounds, QRectF(0, 0, 10, 10));
}

// setMaxUndoDepth() must enforce the cap it sets, whenever it is
// called: lowering the cap below the current stack size trims the
// oldest frames immediately, emitting historyEvicted() once per
// dropped frame so an owner mirroring the store into a chronological
// log stays in lockstep. (Pre-fix, the setter never trimmed and
// pushHistory() evicted at most one frame per push, so an oversized
// stack never converged to the cap.)
void TestAnnotationStore::setMaxUndoDepthTrimsExistingFramesInLockstep() {
    AnnotationStore store;
    for (int i = 0; i < 10; ++i) {
        store.add(makeRect(0, QRectF(double(i), 0, 5, 5)));
    }
    QCOMPARE(store.count(), 10);

    QSignalSpy evictedSpy(&store, &AnnotationStore::historyEvicted);
    store.setMaxUndoDepth(4);
    // 10 frames, cap 4 → 6 dropped, each announced.
    QCOMPARE(evictedSpy.count(), 6);

    // Exactly 4 undos remain; the oldest reachable state has the 6
    // evicted-frame annotations still present.
    int undos = 0;
    while (store.canUndo()) {
        store.undo();
        ++undos;
        QVERIFY2(undos <= 4, "cap not enforced: more frames than maxUndoDepth survived");
    }
    QCOMPARE(undos, 4);
    QCOMPARE(store.count(), 6);

    // A depth of 0 clamps to 1 (documented on the seam) rather than
    // producing an unusable zero-frame history.
    store.setMaxUndoDepth(0);
    QCOMPARE(store.maxUndoDepth(), size_t{1});
}

// Perf sanity for Workstream E.1 — undo on a large store must not be
// O(N * undoSteps). Pre-fix, every undo rescanned the annotation list
// to recompute m_nextId; with 200 annotations and 50 undos that's
// 10k iterations purely for id bookkeeping, plus the vector copy.
// Track nextId per snapshot so the rescan disappears.
//
// The threshold below is loose (1.5s on a 5-year-old laptop is fine);
// the goal is to catch a regression that re-introduces the O(N)
// rescan, not to measure absolute speed. Adjust the threshold up if
// CI emulation comes in slower than this leaves headroom for.
void TestAnnotationStore::undoIsFastForLargeStore() {
    AnnotationStore store;
    // Build 200 distinct annotations, recording 200 history frames.
    constexpr int kCount = 200;
    for (int i = 0; i < kCount; ++i) {
        store.add(makeRect(0, QRectF(double(i), double(i), 5, 5)));
    }
    QCOMPARE(store.count(), kCount);
    // AnnotationStore caps undo history at 64 frames (kMaxUndo). Run
    // 50 undos so we have headroom inside the cap; if kMaxUndo were
    // ever raised this still passes.
    constexpr int kUndos = 50;
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < kUndos; ++i) {
        store.undo();
    }
    const qint64 elapsedMs = timer.elapsed();
    // Loose threshold for slow CI emulation: native macOS hits this
    // in well under 50ms, but Docker-on-macOS amd64 emulation can
    // run 5–10× slower, so 500ms keeps headroom while still failing
    // loudly on a real regression (the O(N*S) variant would still
    // pass on native but starts pushing against this bound on CI).
    QVERIFY2(elapsedMs < 500,
             qPrintable(QStringLiteral("50 undos on 200-annotation store took %1ms (>500ms)")
                            .arg(elapsedMs)));
}

QTEST_MAIN(TestAnnotationStore)
#include "test_annotation_store.moc"
