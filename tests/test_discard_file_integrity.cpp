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
// Wine note: the two open->annotate->same-file-Save cases are QSKIP-ped under
// Wine only (runningUnderWine()). annotations() kicks the background sweep
// whose worker-thread-opened qpdf editor holds the backing file; Wine does not
// release that cross-thread handle, so save()'s same-file QFile::remove(backing)
// fails. Real Windows (process-global FILE handles) and Linux release it fine,
// so this flow runs and is asserted in full on Linux; only the Wine emulator
// skips it. See docs/backlog/2026-07-19-wine-cross-thread-editor-save.md.

#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "document/PdfAdapter.h"
#include "document/PdfEditor.h"
#include "document/RecoveryStore.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace trailer;

namespace {

// True only when the process runs under the Wine emulator. Canonical detection:
// Wine exports ntdll!wine_get_version. Real Windows and Linux/macOS return
// false. Used to QSKIP the two open->annotate->same-file-Save cases under Wine
// only — see the file header and
// docs/backlog/2026-07-19-wine-cross-thread-editor-save.md.
bool runningUnderWine() {
#ifdef Q_OS_WIN
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
#else
    return false;
#endif
}

constexpr const char *kWineSameFileSaveSkip =
    "Wine-only: annotations() kicks the background sweep whose worker-thread-opened "
    "qpdf editor holds the backing file; Wine does not release that cross-thread handle, "
    "so the same-file Save's QFile::remove(backing) fails. Real Windows (process-global "
    "FILE handles) and Linux release it fine — this flow is covered fully on Linux. "
    "See docs/backlog/2026-07-19-wine-cross-thread-editor-save.md.";

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
    void discardAfterAutoSaveLeavesSourceFileByteIdentical();
    void explicitSaveWritesBackingFile();
    void reopenRecoveryRestoresAnnotationDirtyButSourcePristine();
    void recoveryOfPreviouslyAnnotatedPdfDoesNotDuplicateViaBackgroundSweep();
    void recoveredDocSurvivesAnotherAutoSaveTickThenExplicitSave();
};

// INVARIANT (2): auto-save must not write the source, and an explicit Discard
// leaves the on-disk file byte-identical.
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
        QVERIFY2(sha256Of(path) == originalDigest, "auto-save must NOT modify the backing file");

        // The user closes and clicks Discard: drop the sidecar + in-memory
        // state. The source is not written or restored — just untouched.
        store.clear(path);
    }

    QVERIFY2(sha256Of(path) == originalDigest, "Explicit Discard changed the on-disk source PDF");
    QVERIFY(!store.pendingRecovery(path).has_value());
}

// Guard that we did not "fix" the P0 by disabling saving.
void TestDiscardFileIntegrity::explicitSaveWritesBackingFile() {
    if (runningUnderWine())
        QSKIP(kWineSameFileSaveSkip);
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

    // Explicit Save (Ctrl/Cmd+S) — the ONLY path allowed to write the backing file.
    QVERIFY2(doc.save(), "explicit Save must succeed");
    QVERIFY(!doc.isDirty());
    QVERIFY2(sha256Of(path) != originalDigest,
             "explicit Save must write the freehand annotation into the backing file");

    // Read the ink straight back from the file with a fresh editor.
    PdfEditor editor;
    QVERIFY(editor.load(path));
    QVERIFY2(editor.readAnnotations().size() >= 1,
             "explicit Save must persist the freehand annotation into the file");
}

// INVARIANT (1): a crash after an auto-save tick must not lose in-memory work.
void TestDiscardFileIntegrity::reopenRecoveryRestoresAnnotationDirtyButSourcePristine() {
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
    QVERIFY2(sha256Of(path) == originalDigest, "session with only auto-save must not write source");

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
    QCOMPARE(reopened.filePath(), path);
    QVERIFY2(sha256Of(path) == originalDigest,
             "recovery must not write the backing file (invariant 2)");

    // Saving the recovered doc must carry the recovered stroke EXACTLY ONCE.
    // Recovered docs load the editor on the main thread (recoverFrom), so this
    // same-file save is Wine-safe.
    QVERIFY(reopened.save());
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
}

// Blocker regression guard: recovering a backing PDF that ALREADY carries a
// saved annotation must not double it via the view-attach background sweep.
void TestDiscardFileIntegrity::
    recoveryOfPreviouslyAnnotatedPdfDoesNotDuplicateViaBackgroundSweep() {
    if (runningUnderWine())
        QSKIP(kWineSameFileSaveSkip);
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
        QVERIFY(doc.save());                           // A now saved in the file
        {
            PdfEditor e;
            QVERIFY(e.load(path));
            QCOMPARE(static_cast<int>(e.readAnnotations().size()), 1);
        }
        doc.annotations()->add(makeFreehandStroke()); // B
        QCOMPARE(doc.annotations()->count(), 2);
        sidecar = store.sidecarPathFor(path);
        QVERIFY(doc.writeRecoverySnapshot(sidecar)); // sidecar carries A + B
        store.recordSnapshot(path, sidecar);
    }

    const auto pending = store.pendingRecovery(path);
    QVERIFY(pending.has_value());

    PdfDocument reopened(path);
    QVERIFY(reopened.isValid());
    QVERIFY(reopened.recoverFrom(*pending));
    QCOMPARE(reopened.annotations()->count(), 2); // A + B, not doubled

    (void)reopened.annotations();
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
    QCOMPARE(reopened.annotations()->count(), 2);
}

// Blocker regression guard: a recovered document must survive a SECOND
// auto-save tick and a subsequent explicit Save.
void TestDiscardFileIntegrity::recoveredDocSurvivesAnotherAutoSaveTickThenExplicitSave() {
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

    PdfDocument doc(path);
    QVERIFY(doc.isValid());
    QVERIFY(doc.recoverFrom(sidecar));
    QVERIFY(doc.isDirty());
    QCOMPARE(doc.annotations()->count(), 1);

    // Another auto-save tick writes the SAME deterministic sidecar path the
    // recovered content came from — must succeed, must not corrupt the doc.
    QVERIFY2(doc.writeRecoverySnapshot(sidecar),
             "a recovered doc's own auto-save tick must not fail on the shared sidecar path");
    QVERIFY(doc.annotations()->count() == 1);

    // Recovered docs load the editor on the main thread, so this same-file save
    // is Wine-safe.
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
