#include "document/ExternalChangeState.h"
#include "document/ImageAdapter.h"
#include "document/PdfAdapter.h"

#include <QFile>
#include <QImage>
#include <QObject>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QTest>

using namespace trailer;

// Integration test for the save-time conflict guard on a real adapter
// (ImageDocument — its save() is synchronous, so no event loop needed). This
// is the must-have that plugs the ADR-0004 silent-clobber hole: a save must
// NOT overwrite a file that changed under us unless the user explicitly
// forces it.
class TestExternalChangeGuard : public QObject {
    Q_OBJECT

  private slots:
    void init() {
        m_dir = new QTemporaryDir;
        QVERIFY(m_dir->isValid());
    }
    void cleanup() { delete m_dir; }

    // A save over a file that changed externally is BLOCKED (returns false)
    // and leaves the on-disk bytes untouched.
    void saveBlockedWhenFileChangedExternally() {
        const QString path = writeImage(QSize(8, 8), Qt::red);
        ImageDocument doc(path);
        QVERIFY(doc.externalChangeState() == ExternalChangeState::NoChange);

        // Make an edit so the document is dirty (realistic conflict).
        doc.rotatePage(0, 90);
        QVERIFY(doc.isDirty());

        // Simulate another program overwriting the file: different content +
        // size, so the guard trips even if the mtime rounds to the same second.
        overwriteImageOnDisk(path, QSize(32, 24), Qt::blue);
        QVERIFY(doc.externalChangeState() == ExternalChangeState::DirtyConflict);

        const QByteArray before = readAll(path);
        QCOMPARE(doc.save(), false); // guard refuses
        const QByteArray after = readAll(path);
        QCOMPARE(after, before); // no clobber — the on-disk copy is intact
    }

    // "Keep mine": forcing the save clobbers on purpose and refreshes the
    // baseline so a subsequent clean save succeeds.
    void forceSaveClobbersAndRefreshesBaseline() {
        const QString path = writeImage(QSize(8, 8), Qt::red);
        ImageDocument doc(path);
        doc.rotatePage(0, 90);
        overwriteImageOnDisk(path, QSize(32, 24), Qt::blue);
        QVERIFY(doc.externalChangeState() == ExternalChangeState::DirtyConflict);

        doc.setForceSaveOverExternalChange(true);
        QCOMPARE(doc.save(), true); // deliberate clobber goes through

        // Baseline refreshed to what we just wrote: no lingering conflict, and
        // a follow-up save is not blocked.
        QVERIFY(doc.externalChangeState() == ExternalChangeState::NoChange);
        doc.rotatePage(0, 90);
        QCOMPARE(doc.save(), true);
    }

    // The force flag is one-shot: it is consumed by a single save attempt and
    // does not silently disarm the guard for later saves.
    void forceFlagIsOneShot() {
        const QString path = writeImage(QSize(8, 8), Qt::red);
        ImageDocument doc(path);
        doc.rotatePage(0, 90);

        // Force is set but consumed even though this save isn't a conflict yet.
        doc.setForceSaveOverExternalChange(true);
        QCOMPARE(doc.save(), true);

        // Now an external change lands; the (already-consumed) force must NOT
        // let this save through.
        doc.rotatePage(0, 90);
        overwriteImageOnDisk(path, QSize(40, 40), Qt::green);
        QCOMPARE(doc.save(), false);
    }

    // Save-As to a DIFFERENT path is never a clobber of the baselined file,
    // even when that original changed externally.
    void saveAsToNewPathNotBlocked() {
        const QString path = writeImage(QSize(8, 8), Qt::red);
        ImageDocument doc(path);
        doc.rotatePage(0, 90);
        overwriteImageOnDisk(path, QSize(32, 24), Qt::blue);

        const QString other = m_dir->filePath(QStringLiteral("copy.png"));
        QCOMPARE(doc.save(other), true);
        QVERIFY(QFile::exists(other));
    }

    // F1 regression: the PDF two-phase save must ALSO guard at commit time, not
    // only at begin time. The begin-phase re-stat runs on a worker thread and
    // the destructive remove+rename in saveCommitOnUi lands seconds later, so
    // an external write that arrives in that window must abort the commit
    // WITHOUT clobbering the (now newer) on-disk file. Here we drive the two
    // phases directly and slip an external overwrite between them: the commit
    // must refuse (return false) and the externally-written bytes must survive.
    void pdfCommitGuardBlocksMidFlightExternalChange() {
        const QString path = writePdf(QStringLiteral("Original"));
        PdfDocument doc(path);
        QVERIFY(doc.isValid());

        // Dirty edit so this is a realistic same-file overwrite save.
        doc.rotatePage(0, 90);
        QVERIFY(doc.isDirty());
        QVERIFY(doc.externalChangeState() == ExternalChangeState::NoChange);

        // Phase 1 (worker-safe) succeeds — no external change yet. It stages
        // the new bytes to a temp file; the target is not touched.
        auto ctx = doc.saveBeginQpdfPhase(QString());
        QVERIFY(ctx.has_value());

        // Another program replaces the file AFTER begin but BEFORE commit.
        // Different byte length, so the classifier's size signal trips even if
        // the mtime rounds to the same second.
        const QByteArray external = "%PDF-1.4 externally-rewritten between phases\n";
        overwriteRawBytes(path, external);
        QVERIFY(doc.externalChangeState() == ExternalChangeState::DirtyConflict);

        // Phase 2 must ABORT: no remove/rename, the external bytes survive.
        QCOMPARE(doc.saveCommitOnUi(*ctx), false);
        QCOMPARE(readAll(path), external);
    }

    // Counterpart: with the one-shot force flag armed ("Keep mine"), the same
    // mid-flight external change does NOT block the commit — the deliberate
    // clobber goes through and the on-disk bytes become ours.
    void pdfCommitGuardForceClobbersMidFlightExternalChange() {
        const QString path = writePdf(QStringLiteral("Original"));
        PdfDocument doc(path);
        QVERIFY(doc.isValid());
        doc.rotatePage(0, 90);

        doc.setForceSaveOverExternalChange(true);
        auto ctx = doc.saveBeginQpdfPhase(QString());
        QVERIFY(ctx.has_value());

        const QByteArray external = "%PDF-1.4 externally-rewritten between phases\n";
        overwriteRawBytes(path, external);

        // Forced: commit proceeds despite the external change.
        QCOMPARE(doc.saveCommitOnUi(*ctx), true);
        // The on-disk file is now a real PDF we wrote, not the external stub.
        QVERIFY(readAll(path) != external);
        QVERIFY(readAll(path).startsWith("%PDF"));
    }

    // A clean reload from disk picks up the new bytes and clears the conflict.
    void reloadFromDiskPicksUpNewContent() {
        const QString path = writeImage(QSize(8, 8), Qt::red);
        ImageDocument doc(path);
        overwriteImageOnDisk(path, QSize(32, 24), Qt::blue);
        QVERIFY(doc.externalChangeState() == ExternalChangeState::CleanExternalChange);

        QCOMPARE(doc.reloadFromDisk(), true);
        QCOMPARE(doc.imagePixelSize(), QSize(32, 24));
        QVERIFY(doc.externalChangeState() == ExternalChangeState::NoChange);
    }

  private:
    QString writeImage(QSize size, Qt::GlobalColor color) {
        const QString path = m_dir->filePath(QStringLiteral("doc.png"));
        QImage img(size, QImage::Format_ARGB32);
        img.fill(color);
        [&] { QVERIFY(img.save(path, "PNG")); }();
        return path;
    }
    void overwriteImageOnDisk(const QString &path, QSize size, Qt::GlobalColor color) {
        // The replacement is a different pixel size, so the encoded byte size
        // always differs — the classifier's secondary (size) signal catches
        // the change even when the mtime rounds to the same second.
        QImage img(size, QImage::Format_ARGB32);
        img.fill(color);
        [&] { QVERIFY(img.save(path, "PNG")); }();
    }
    QString writePdf(const QString &text) {
        const QString path = m_dir->filePath(QStringLiteral("doc.pdf"));
        {
            QPdfWriter writer(path);
            writer.setPageSize(QPageSize(QPageSize::A4));
            QPainter painter(&writer);
            painter.drawText(QRect(100, 100, 400, 100), Qt::AlignCenter, text);
            painter.end();
        }
        return path;
    }
    static void overwriteRawBytes(const QString &path, const QByteArray &bytes) {
        // Replace the file wholesale with different-length content. The
        // save-time guard stats mtime/size only (it never parses), so raw
        // bytes are a faithful stand-in for another program's write.
        QFile f(path);
        [&] { QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate)); }();
        f.write(bytes);
        f.close();
    }
    static QByteArray readAll(const QString &path) {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        return f.readAll();
    }

    QTemporaryDir *m_dir = nullptr;
};

QTEST_MAIN(TestExternalChangeGuard)
#include "test_external_change_guard.moc"
