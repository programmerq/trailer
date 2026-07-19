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
// These tests drive the exact methods MainWindow now uses — a document's
// writeRecoverySnapshot()/recoverFrom() and the RecoveryStore — so they pin
// the invariant without spinning the real 30 s auto-save QTimer:
//   - autosave tick     == doc.writeRecoverySnapshot(sidecar) + store.record
//   - explicit Discard   == store.clear(backing) + drop in-memory doc
//   - explicit Save      == doc.save()
//   - reopen-recovery    == store.pendingRecovery(backing) + doc.recoverFrom
//
// See src/ui/MainWindow.cpp autoSaveDirtyDocs() / confirmCloseDirtyDoc(),
// src/app/Application.cpp openFiles() (recovery hook), and
// src/document/RecoveryStore.*.

#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "document/PdfAdapter.h"
#include "document/PdfEditor.h"
#include "document/RecoveryStore.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

QByteArray sha256Of(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return h.result();
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
    void discardAfterAutoSaveLeavesSourceFileByteIdentical();
    void explicitSaveWritesBackingFile();
    void reopenRecoveryRestoresAnnotationDirtyButSourcePristine();
    void recoveryOfPreviouslyAnnotatedPdfDoesNotDuplicateViaBackgroundSweep();
    void recoveredDocSurvivesAnotherAutoSaveTickThenExplicitSave();
};

// INVARIANT (2): auto-save must not write the source, and an explicit Discard
// leaves the on-disk file byte-identical. Was RED before the fix (auto-save
// called doc->save(), rewriting the source in place); GREEN now that auto-save
// writes only a sidecar.
void TestDiscardFileIntegrity::discardAfterAutoSaveLeavesSourceFileByteIdentical() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("source.pdf");
    writeFixturePdf(path);
    QVERIFY(QFile::exists(path));

    const QByteArray originalDigest = sha256Of(path);
    QVERIFY(!originalDigest.isEmpty());

    RecoveryStore store(dir.filePath("recovery"));

    {
        PdfDocument doc(path);
        QVERIFY(doc.isValid());
        QVERIFY(!doc.isDirty());
        doc.annotations()->add(makeFreehandStroke());
        QVERIFY2(doc.isDirty(), "adding a freehand stroke must mark the document dirty");

        // Auto-save tick: writes a recovery sidecar, NEVER the source.
        const QString sidecar = store.sidecarPathFor(path);
        QVERIFY(doc.writeRecoverySnapshot(sidecar));
        store.recordSnapshot(path, sidecar);
        QVERIFY2(QFile::exists(sidecar), "auto-save must produce a recovery sidecar");
        QVERIFY2(sha256Of(path) == originalDigest,
                 "auto-save must NOT modify the backing file");

        // The user closes and clicks Discard: drop the sidecar + in-memory
        // state (the doc is destroyed at scope exit). The source is not
        // written or restored — it must simply be untouched.
        store.clear(path);
    }

    QVERIFY2(sha256Of(path) == originalDigest,
             qPrintable(QStringLiteral(
                            "Explicit Discard changed the on-disk source PDF. "
                            "original sha256=%1; after sha256=%2.")
                            .arg(QString::fromLatin1(originalDigest.toHex()))
                            .arg(QString::fromLatin1(sha256Of(path).toHex()))));
    // Discard also removes the sidecar — nothing lingers to falsely recover.
    QVERIFY(!store.pendingRecovery(path).has_value());
}

// Guard that we did not "fix" the P0 by disabling saving: an explicit Save
// still writes the annotation into the backing file.
void TestDiscardFileIntegrity::explicitSaveWritesBackingFile() {
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

    // Explicit Save (⌘S) — the ONLY path allowed to write the backing file.
    QVERIFY2(doc.save(), "explicit Save must succeed");
    QVERIFY(!doc.isDirty());
    QVERIFY2(sha256Of(path) != originalDigest,
             "explicit Save must write the freehand annotation into the backing file");

    // The saved annotation round-trips: read it straight back from the file
    // with a fresh editor (synchronous; independent of the viewer's async
    // annotation sweep) to prove the ink actually landed in the backing file.
    PdfEditor editor;
    QVERIFY(editor.load(path));
    QVERIFY2(editor.readAnnotations().size() >= 1,
             "explicit Save must persist the freehand annotation into the file");
}

// INVARIANT (1): a crash after an auto-save tick (no explicit Save/Discard)
// must not lose the in-memory work — reopen silently restores it as a dirty
// document — while the source stays byte-identical until the user Saves. If
// the user then Discards, the source is still pristine.
void TestDiscardFileIntegrity::reopenRecoveryRestoresAnnotationDirtyButSourcePristine() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("source.pdf");
    writeFixturePdf(path);

    const QByteArray originalDigest = sha256Of(path);
    QVERIFY(!originalDigest.isEmpty());

    RecoveryStore store(dir.filePath("recovery"));

    // Session 1: draw, auto-save tick, then "crash" (drop the doc with no
    // explicit Save/Discard so the sidecar survives).
    {
        PdfDocument doc(path);
        QVERIFY(doc.isValid());
        doc.annotations()->add(makeFreehandStroke());
        QVERIFY(doc.isDirty());
        const QString sidecar = store.sidecarPathFor(path);
        QVERIFY(doc.writeRecoverySnapshot(sidecar));
        store.recordSnapshot(path, sidecar);
    }
    // Backing file untouched across the whole first session.
    QVERIFY2(sha256Of(path) == originalDigest, "session with only auto-save must not write source");

    // Session 2: reopen. The recovery hook finds a pending sidecar and
    // restores the annotation as a dirty document, source still pristine.
    const auto pending = store.pendingRecovery(path);
    QVERIFY2(pending.has_value(), "a newer sidecar over an unchanged source must be recoverable");

    PdfDocument reopened(path);
    QVERIFY(reopened.isValid());
    QVERIFY2(reopened.annotations()->count() == 0,
             "sanity: a plain reopen of the untouched source has no annotations");
    QVERIFY2(reopened.recoverFrom(*pending), "recoverFrom must restore the snapshot");
    QVERIFY2(reopened.annotations()->count() >= 1,
             "recovery must restore the freehand annotation (invariant 1: no silent loss)");
    QVERIFY2(reopened.isDirty(), "recovered edits are unsaved — the doc must be dirty");
    QCOMPARE(reopened.filePath(), path); // Save still targets the user's file
    QVERIFY2(sha256Of(path) == originalDigest,
             "recovery must not write the backing file (invariant 2)");

    // If the user now explicitly Saves the recovered document, the backing
    // file must carry the recovered stroke EXACTLY ONCE — recovery must not
    // duplicate annotations (regression guard: recoverFrom loads the editor
    // from a sidecar whose /Annots already hold the stroke, so a naive Save
    // would append it a second time).
    QVERIFY(reopened.save());
    {
        PdfEditor editor;
        QVERIFY(editor.load(path));
        QCOMPARE(static_cast<int>(editor.readAnnotations().size()), 1);
    }

    // Re-open a fresh copy of the ORIGINAL bytes to exercise the Discard-after-
    // recovery path independently (the save above intentionally wrote the file).
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
    // The user Discards after a recovery-eligible session: drop the sidecar and
    // in-memory state. Source is still byte-identical to before the ordeal.
    store2.clear(path2);
    QVERIFY2(sha256Of(path2) == original2, "Discard after recovery leaves the source pristine");
    QVERIFY(!store2.pendingRecovery(path2).has_value());
}

// Blocker regression guard: recovering a backing PDF that ALREADY carries a
// saved annotation must not double that annotation. recoverFrom populates the
// store from the sidecar; the view-attach background annotation sweep must not
// then read the backing file and append its annotations on top. (The
// no-annotation fixture in the test above hides this — this case uses a backing
// file with a real saved annotation and drives the sweep with an event loop.)
void TestDiscardFileIntegrity::
    recoveryOfPreviouslyAnnotatedPdfDoesNotDuplicateViaBackgroundSweep() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("annotated.pdf");
    writeFixturePdf(path);

    RecoveryStore store(dir.filePath("recovery"));

    // Bake ONE annotation (A) into the backing file via an explicit Save, then
    // in the same session add a second stroke (B) and take a recovery snapshot
    // (sidecar carries A + B).
    QString sidecar;
    {
        PdfDocument doc(path);
        QVERIFY(doc.isValid());
        doc.annotations()->add(makeFreehandStroke()); // A
        QVERIFY(doc.save());                           // A now saved in the file
        {
            PdfEditor e;
            QVERIFY(e.load(path));
            QCOMPARE(static_cast<int>(e.readAnnotations().size()), 1); // file has A
        }
        // add() flushes the deferred load of A first (pre-edit hook), so the
        // store holds A + B here.
        doc.annotations()->add(makeFreehandStroke()); // B
        QCOMPARE(doc.annotations()->count(), 2);
        sidecar = store.sidecarPathFor(path);
        QVERIFY(doc.writeRecoverySnapshot(sidecar)); // sidecar carries A + B
        store.recordSnapshot(path, sidecar);
    }

    // Reopen and recover. The store must hold exactly A + B (2), and triggering
    // the background sweep (annotations() kicks it) with an event loop must NOT
    // append the backing file's A a second time.
    const auto pending = store.pendingRecovery(path);
    QVERIFY(pending.has_value());

    PdfDocument reopened(path);
    QVERIFY(reopened.isValid());
    QVERIFY(reopened.recoverFrom(*pending));
    QCOMPARE(reopened.annotations()->count(), 2); // A + B, not doubled

    // Kick the deferred sweep (annotations() calls startBackgroundLoad) and let
    // any worker run. With the fix it short-circuits; without it, it would read
    // the backing file and append A → count 3.
    (void)reopened.annotations();
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
    QCOMPARE(reopened.annotations()->count(), 2);
}

// Blocker regression guard: a recovered document must survive a SECOND
// auto-save tick and a subsequent explicit Save. The tick writes the
// deterministic sidecar path — which is the very path the recovered content
// came from — so if the live editor/viewer were backed by that path, the tick
// would truncate the file out from under them and Save would fail or corrupt
// the backing file. The fix backs the recovered doc with a private copy.
void TestDiscardFileIntegrity::recoveredDocSurvivesAnotherAutoSaveTickThenExplicitSave() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("source.pdf");
    writeFixturePdf(path);

    RecoveryStore store(dir.filePath("recovery"));
    const QString sidecar = store.sidecarPathFor(path);

    // Session 1: draw + snapshot, then "crash".
    {
        PdfDocument doc(path);
        QVERIFY(doc.isValid());
        doc.annotations()->add(makeFreehandStroke());
        QVERIFY(doc.writeRecoverySnapshot(sidecar));
        store.recordSnapshot(path, sidecar);
    }

    // Session 2: recover.
    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    QVERIFY(doc.recoverFrom(sidecar));
    QVERIFY(doc.isDirty());
    QCOMPARE(doc.annotations()->count(), 1);

    // Another auto-save tick fires — writes the SAME deterministic sidecar path
    // the recovered content came from. This must succeed and must NOT corrupt
    // the live document.
    QVERIFY2(doc.writeRecoverySnapshot(sidecar),
             "a recovered doc's own auto-save tick must not fail on the shared sidecar path");
    QVERIFY(doc.annotations()->count() == 1);

    // The user reviews and Saves. Must succeed and write a valid file with the
    // recovered stroke exactly once — no corruption, no loss.
    QVERIFY2(doc.save(), "explicit Save of a recovered doc must succeed after a tick");
    QVERIFY(!doc.isDirty());

    PdfDocument reopened(path);
    QVERIFY2(reopened.isValid(), "the saved backing file must be a valid PDF (not corrupted)");
    PdfEditor editor;
    QVERIFY(editor.load(path));
    QCOMPARE(static_cast<int>(editor.readAnnotations().size()), 1);
}

QTEST_MAIN(TestDiscardFileIntegrity)
#include "test_discard_file_integrity.moc"
