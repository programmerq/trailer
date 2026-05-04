#include "annotation/AnnotationStore.h"

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
};

namespace {
Annotation makeRect(int page, QRectF bounds) {
    Annotation a;
    a.page = page;
    a.type = AnnotationType::Rectangle;
    a.bounds = bounds;
    return a;
}
}  // namespace

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
    const Annotation* hit = store.find(id);
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
    const Annotation* hit = store.find(id);
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
    QCOMPARE(spy.count(), 2);  // no-op when already empty
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

    store.undo();  // revert update
    QCOMPARE(store.find(id)->bounds, QRectF(0, 0, 10, 10));
    QVERIFY(store.canRedo());

    store.undo();  // revert add
    QCOMPARE(store.count(), 0);

    store.redo();  // re-add
    QCOMPARE(store.count(), 1);
    QVERIFY(store.find(id) != nullptr);

    store.redo();  // re-apply update
    QCOMPARE(store.find(id)->bounds, QRectF(1, 2, 3, 4));

    store.undo();
    store.undo();
    QVERIFY(store.remove(id) == false);  // already gone
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

QTEST_MAIN(TestAnnotationStore)
#include "test_annotation_store.moc"
