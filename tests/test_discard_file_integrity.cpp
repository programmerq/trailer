// P0 data-integrity regression guard (owner HITL, 2026-07-19):
//
//   "I closed the window and chose 'don't save', but it absolutely
//    changed the file. We don't want to lose data, but we don't want
//    to overwrite data either."
//
// This is the WRITE-SIDE twin of the never-worry-save invariant. The dual
// invariant (docs/decision-records/2026-07-19-autosave-recovery-sidecar.md,
// amending ADR 0004):
//   (1) never silently LOSE in-memory work, and
//   (2) never silently WRITE the backing file.
// No path writes the backing file except an explicit Save/Save-As; auto-save
// persists only to a recovery SIDECAR in app-data; and an explicit Discard
// leaves the on-disk file byte-identical.
//
// See src/ui/MainWindow.cpp autoSaveDirtyDocs() / confirmCloseDirtyDoc(),
// src/app/Application.cpp openFiles() (recovery hook), and
// src/document/RecoveryStore.*.
//
// TEMPORARY Wine diagnostics: this suite crashes only under the Windows/Wine
// CI job (non-zero exit, no captured stdout) while Linux (incl. ASAN/UBSAN/
// LSAN) is clean. Unbuffered stderr CHK() checkpoints below survive a hard
// crash so the CI log's last printed checkpoint localizes the failing step.

#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "document/PdfAdapter.h"
#include "document/PdfEditor.h"
#include "document/RecoveryStore.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QFile>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cstdio>

// Unbuffered stderr checkpoint: flushed immediately so it survives a hard
// crash under Wine (where buffered QtTest stdout is lost). See file header.
#define CHK(label)                                                                                 \
    do {                                                                                           \
        std::fprintf(stderr, "[chk] %s\n", (label));                                               \
        std::fflush(stderr);                                                                       \
    } while (0)

using namespace trailer;

namespace {

QByteArray sha256Of(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
}

// A freehand stroke, built the way AnnotationOverlay commits an Ink gesture
// into the AnnotationStore: an AnnotationType::Ink with the captured polyline
// in `points` and the stroke bbox in `bounds` (Annotation.h:51,94-95).
Annotation makeFreehandStroke() {
    Annotation ink;
    ink.page = 0;
    ink.type = AnnotationType::Ink;
    ink.points = {QPointF(72, 72), QPointF(120, 90), QPointF(160, 140), QPointF(200, 96)};
    ink.bounds = QRectF(72, 72, 128, 68);
    ink.style.stroke = QColor(20, 20, 200);
    ink.style.strokeWidth = 2.5;
    return ink;
}

QString writeFixturePdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter painter(&writer);
    painter.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter, "Original contents");
    painter.end();
    return path;
}

} // namespace

class TestDiscardFileIntegrity : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase();
    void discardAfterAutoSaveLeavesSourceFileByteIdentical();
    void explicitSaveWritesBackingFile();
    void reopenRecoveryRestoresAnnotationDirtyButSourcePristine();
    void recoveryOfPreviouslyAnnotatedPdfDoesNotDuplicateViaBackgroundSweep();
    void recoveredDocSurvivesAnotherAutoSaveTickThenExplicitSave();
};

void TestDiscardFileIntegrity::initTestCase() {
    // Make stderr unbuffered so CHK() checkpoints are not lost on a hard crash.
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    CHK("initTestCase");
}

// INVARIANT (2): auto-save must not write the source, and an explicit Discard
// leaves the on-disk file byte-identical.
void TestDiscardFileIntegrity::discardAfterAutoSaveLeavesSourceFileByteIdentical() {
    CHK("case1:enter");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("source.pdf");
    writeFixturePdf(path);
    QVERIFY(QFile::exists(path));
    CHK("case1:fixture-written");

    const QByteArray originalDigest = sha256Of(path);
    QVERIFY(!originalDigest.isEmpty());

    RecoveryStore store(dir.filePath("recovery"));
    CHK("case1:store-ctor");

    {
        PdfDocument doc(path);
        QVERIFY(doc.isValid());
        QVERIFY(!doc.isDirty());
        CHK("case1:doc-open");
        doc.annotations()->add(makeFreehandStroke());
        QVERIFY2(doc.isDirty(), "adding a freehand stroke must mark the document dirty");
        CHK("case1:annotation-added");

        const QString sidecar = store.sidecarPathFor(path);
        CHK("case1:before-writeRecoverySnapshot");
        QVERIFY(doc.writeRecoverySnapshot(sidecar));
        CHK("case1:after-writeRecoverySnapshot");
        store.recordSnapshot(path, sidecar);
        QVERIFY2(QFile::exists(sidecar), "auto-save must produce a recovery sidecar");
        QVERIFY2(sha256Of(path) == originalDigest, "auto-save must NOT modify the backing file");
        CHK("case1:snapshot-recorded");

        store.clear(path);
        CHK("case1:before-doc-destruct");
    }
    CHK("case1:after-doc-destruct");

    QVERIFY2(sha256Of(path) == originalDigest, "Explicit Discard changed the on-disk source PDF");
    QVERIFY(!store.pendingRecovery(path).has_value());
    CHK("case1:done");
}

// Guard that we did not "fix" the P0 by disabling saving.
void TestDiscardFileIntegrity::explicitSaveWritesBackingFile() {
    CHK("case2:enter");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("source.pdf");
    writeFixturePdf(path);

    const QByteArray originalDigest = sha256Of(path);
    QVERIFY(!originalDigest.isEmpty());

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    doc.annotations()->add(makeFreehandStroke());
    QVERIFY(doc.isDirty());
    CHK("case2:before-save");
    QVERIFY2(doc.save(), "explicit Save must succeed");
    CHK("case2:after-save");
    QVERIFY(!doc.isDirty());
    QVERIFY2(sha256Of(path) != originalDigest,
             "explicit Save must write the freehand annotation into the backing file");

    PdfEditor editor;
    QVERIFY(editor.load(path));
    QVERIFY2(editor.readAnnotations().size() >= 1,
             "explicit Save must persist the freehand annotation into the file");
    CHK("case2:done");
}

// INVARIANT (1): a crash after an auto-save tick must not lose in-memory work.
void TestDiscardFileIntegrity::reopenRecoveryRestoresAnnotationDirtyButSourcePristine() {
    CHK("case3:enter");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("source.pdf");
    writeFixturePdf(path);

    const QByteArray originalDigest = sha256Of(path);
    QVERIFY(!originalDigest.isEmpty());

    RecoveryStore store(dir.filePath("recovery"));

    {
        PdfDocument doc(path);
        QVERIFY(doc.isValid());
        doc.annotations()->add(makeFreehandStroke());
        QVERIFY(doc.isDirty());
        const QString sidecar = store.sidecarPathFor(path);
        QVERIFY(doc.writeRecoverySnapshot(sidecar));
        store.recordSnapshot(path, sidecar);
    }
    CHK("case3:session1-done");
    QVERIFY2(sha256Of(path) == originalDigest, "session with only auto-save must not write source");

    const auto pending = store.pendingRecovery(path);
    QVERIFY2(pending.has_value(), "a newer sidecar over an unchanged source must be recoverable");
    CHK("case3:pending-found");

    PdfDocument reopened(path);
    QVERIFY(reopened.isValid());
    QVERIFY2(reopened.annotations()->count() == 0,
             "sanity: a plain reopen of the untouched source has no annotations");
    CHK("case3:before-recoverFrom");
    QVERIFY2(reopened.recoverFrom(*pending), "recoverFrom must restore the snapshot");
    CHK("case3:after-recoverFrom");
    QVERIFY2(reopened.annotations()->count() >= 1,
             "recovery must restore the freehand annotation (invariant 1: no silent loss)");
    QVERIFY2(reopened.isDirty(), "recovered edits are unsaved — the doc must be dirty");
    QCOMPARE(reopened.filePath(), path);
    QVERIFY2(sha256Of(path) == originalDigest,
             "recovery must not write the backing file (invariant 2)");

    CHK("case3:before-recovered-save");
    QVERIFY(reopened.save());
    CHK("case3:after-recovered-save");
    {
        PdfEditor editor;
        QVERIFY(editor.load(path));
        QCOMPARE(static_cast<int>(editor.readAnnotations().size()), 1);
    }

    const QString path2 = dir.filePath("source2.pdf");
    writeFixturePdf(path2);
    const QByteArray original2 = sha256Of(path2);
    RecoveryStore store2(dir.filePath("recovery2"));
    {
        PdfDocument doc(path2);
        doc.annotations()->add(makeFreehandStroke());
        const QString sc = store2.sidecarPathFor(path2);
        QVERIFY(doc.writeRecoverySnapshot(sc));
        store2.recordSnapshot(path2, sc);
    }
    store2.clear(path2);
    QVERIFY2(sha256Of(path2) == original2, "Discard after recovery leaves the source pristine");
    QVERIFY(!store2.pendingRecovery(path2).has_value());
    CHK("case3:done");
}

// Blocker regression guard: recovering a backing PDF that ALREADY carries a
// saved annotation must not double it via the view-attach background sweep.
void TestDiscardFileIntegrity::
    recoveryOfPreviouslyAnnotatedPdfDoesNotDuplicateViaBackgroundSweep() {
    CHK("case4:enter");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("annotated.pdf");
    writeFixturePdf(path);

    RecoveryStore store(dir.filePath("recovery"));

    QString sidecar;
    {
        PdfDocument doc(path);
        QVERIFY(doc.isValid());
        doc.annotations()->add(makeFreehandStroke()); // A
        CHK("case4:before-save-A");
        QVERIFY(doc.save());                           // A now saved in the file
        CHK("case4:after-save-A");
        {
            PdfEditor e;
            QVERIFY(e.load(path));
            QCOMPARE(static_cast<int>(e.readAnnotations().size()), 1);
        }
        doc.annotations()->add(makeFreehandStroke()); // B
        QCOMPARE(doc.annotations()->count(), 2);
        sidecar = store.sidecarPathFor(path);
        CHK("case4:before-writeRecoverySnapshot");
        QVERIFY(doc.writeRecoverySnapshot(sidecar));
        CHK("case4:after-writeRecoverySnapshot");
        store.recordSnapshot(path, sidecar);
    }
    CHK("case4:session1-done");

    const auto pending = store.pendingRecovery(path);
    QVERIFY(pending.has_value());

    PdfDocument reopened(path);
    QVERIFY(reopened.isValid());
    CHK("case4:before-recoverFrom");
    QVERIFY(reopened.recoverFrom(*pending));
    CHK("case4:after-recoverFrom");
    QCOMPARE(reopened.annotations()->count(), 2);

    (void)reopened.annotations();
    CHK("case4:before-processEvents");
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
    CHK("case4:after-processEvents");
    QCOMPARE(reopened.annotations()->count(), 2);
    CHK("case4:done");
}

// Blocker regression guard: a recovered document must survive a SECOND
// auto-save tick and a subsequent explicit Save.
void TestDiscardFileIntegrity::recoveredDocSurvivesAnotherAutoSaveTickThenExplicitSave() {
    CHK("case5:enter");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("source.pdf");
    writeFixturePdf(path);

    RecoveryStore store(dir.filePath("recovery"));
    const QString sidecar = store.sidecarPathFor(path);

    {
        PdfDocument doc(path);
        QVERIFY(doc.isValid());
        doc.annotations()->add(makeFreehandStroke());
        QVERIFY(doc.writeRecoverySnapshot(sidecar));
        store.recordSnapshot(path, sidecar);
    }
    CHK("case5:session1-done");

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    CHK("case5:before-recoverFrom");
    QVERIFY(doc.recoverFrom(sidecar));
    CHK("case5:after-recoverFrom");
    QVERIFY(doc.isDirty());
    QCOMPARE(doc.annotations()->count(), 1);

    CHK("case5:before-second-tick");
    QVERIFY2(doc.writeRecoverySnapshot(sidecar),
             "a recovered doc's own auto-save tick must not fail on the shared sidecar path");
    CHK("case5:after-second-tick");
    QVERIFY(doc.annotations()->count() == 1);

    CHK("case5:before-save");
    QVERIFY2(doc.save(), "explicit Save of a recovered doc must succeed after a tick");
    CHK("case5:after-save");
    QVERIFY(!doc.isDirty());

    PdfDocument reopened(path);
    QVERIFY2(reopened.isValid(), "the saved backing file must be a valid PDF (not corrupted)");
    PdfEditor editor;
    QVERIFY(editor.load(path));
    QCOMPARE(static_cast<int>(editor.readAnnotations().size()), 1);
    CHK("case5:done");
}

QTEST_MAIN(TestDiscardFileIntegrity)
#include "test_discard_file_integrity.moc"
