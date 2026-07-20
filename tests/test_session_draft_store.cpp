// Unit test — SessionDraftStore round-trip (macOS "Quit and Keep Windows"
// draft store; docs/decision-records/2026-07-16-quit-and-keep-windows.md).
//
// The decisive threshold from that record (clause 2): an unsaved/untitled
// document's BYTES survive a serialize→restore round-trip byte-for-byte,
// and its untitled flag is preserved. This exercises the store in
// isolation — no GUI, no Application — feeding it known non-trivial bytes
// across multiple documents and windows and asserting exact equality on
// the way back out, plus hasSession()/clear() lifecycle.
//
// Guileless main (QCoreApplication): the store deals only in JSON + byte
// blobs, so no QPA platform is required.

#include "settings/SessionDraftStore.h"

#include <QByteArray>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

// Non-trivial, deterministic bytes: a run that includes NULs, high bytes,
// and a recognizable header, so a truncation / re-encoding / off-by-one in
// the store would be caught. Not valid PNG on purpose — the store must be
// format-agnostic and byte-exact.
QByteArray knownBytes(char seed, int len) {
    QByteArray b;
    b.reserve(len);
    for (int i = 0; i < len; ++i)
        b.append(static_cast<char>((seed + i * 7) & 0xFF));
    return b;
}

} // namespace

class TestSessionDraftStore : public QObject {
    Q_OBJECT
  private slots:
    // A single untitled draft round-trips byte-identical with its flag.
    void untitledDraftRoundTripsByteIdentical();
    // Multiple documents across multiple windows, mixing draft and path
    // descriptors, round-trip in order with every field intact.
    void multiWindowMultiDocRoundTrip();
    // hasSession()/clear() lifecycle: empty dir → no session; after save →
    // session; after clear → gone.
    void hasSessionAndClearLifecycle();
    // A version mismatch OR corrupt/truncated JSON in the manifest yields no
    // session (honest-restore: better to restore nothing than to mis-parse a
    // foreign or damaged store).
    void unreadableStoreYieldsNoSession();
    // A draft doc's devicePixelRatio + capture-origin flag survive the
    // round-trip (MAJOR 2). A PNG blob does not carry Qt's dpr, so the store
    // must persist it explicitly — asserted directly, NOT via QImage::==.
    void dprAndCaptureOriginRoundTrip();
    // A save that fails partway must NOT wipe an existing valid session
    // (MAJOR 3a): the store swaps a fully-staged session into place
    // atomically, so a failed new save leaves the prior one intact.
    void failedSaveKeepsPriorSessionIntact();
    // A failure DURING the promote/swap phase (after staging is fully built)
    // must also leave the prior session intact — the two-rename swap never
    // leaves the live session absent from disk in a way that could lose it.
    void failedSwapKeepsPriorSessionIntact();
};

void TestSessionDraftStore::untitledDraftRoundTripsByteIdentical() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SessionDraftStore store(dir.path() + "/session-drafts");

    const QByteArray payload = knownBytes(0x11, 4096);

    SessionDocDescriptor doc;
    doc.kind = SessionDocDescriptor::Kind::Draft;
    doc.bytes = payload;
    doc.format = QStringLiteral("png");
    doc.untitled = true;

    SessionWindowDescriptor win;
    win.docs.append(doc);

    QVERIFY(store.save({win}));
    QVERIFY(store.hasSession());

    // A FRESH store object pointed at the same directory — the "relaunch"
    // seam a real second app instance would use.
    SessionDraftStore reopened(dir.path() + "/session-drafts");
    const QList<SessionWindowDescriptor> restored = reopened.restore();

    QCOMPARE(restored.size(), 1);
    QCOMPARE(restored[0].docs.size(), 1);
    const SessionDocDescriptor &out = restored[0].docs[0];
    QCOMPARE(out.kind, SessionDocDescriptor::Kind::Draft);
    QVERIFY(out.untitled);
    // The byte-identity threshold.
    QCOMPARE(out.bytes, payload);
    QCOMPARE(out.bytes.size(), payload.size());
}

void TestSessionDraftStore::multiWindowMultiDocRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SessionDraftStore store(dir.path() + "/session-drafts");

    const QByteArray blobA = knownBytes(0x20, 1500);
    const QByteArray blobB = knownBytes(0x55, 9001);

    // Window 1: an untitled draft + a saved path.
    SessionWindowDescriptor w1;
    SessionDocDescriptor d1;
    d1.kind = SessionDocDescriptor::Kind::Draft;
    d1.bytes = blobA;
    d1.untitled = true;
    w1.docs.append(d1);
    SessionDocDescriptor d2;
    d2.kind = SessionDocDescriptor::Kind::Path;
    d2.path = QStringLiteral("/tmp/some/report.pdf");
    w1.docs.append(d2);

    // Window 2: a titled-but-dirty draft (untitled=false, originalPath set).
    SessionWindowDescriptor w2;
    SessionDocDescriptor d3;
    d3.kind = SessionDocDescriptor::Kind::Draft;
    d3.bytes = blobB;
    d3.untitled = false;
    d3.originalPath = QStringLiteral("/tmp/photos/scan.png");
    w2.docs.append(d3);

    QVERIFY(store.save({w1, w2}));

    SessionDraftStore reopened(dir.path() + "/session-drafts");
    const QList<SessionWindowDescriptor> r = reopened.restore();

    QCOMPARE(r.size(), 2);
    QCOMPARE(r[0].docs.size(), 2);
    QCOMPARE(r[1].docs.size(), 1);

    // Window 1, doc 0: untitled draft, bytes byte-identical.
    QCOMPARE(r[0].docs[0].kind, SessionDocDescriptor::Kind::Draft);
    QVERIFY(r[0].docs[0].untitled);
    QCOMPARE(r[0].docs[0].bytes, blobA);

    // Window 1, doc 1: path reference preserved in order.
    QCOMPARE(r[0].docs[1].kind, SessionDocDescriptor::Kind::Path);
    QCOMPARE(r[0].docs[1].path, QStringLiteral("/tmp/some/report.pdf"));

    // Window 2, doc 0: titled-but-dirty draft, flag + originalPath intact,
    // bytes byte-identical.
    QCOMPARE(r[1].docs[0].kind, SessionDocDescriptor::Kind::Draft);
    QVERIFY(!r[1].docs[0].untitled);
    QCOMPARE(r[1].docs[0].originalPath, QStringLiteral("/tmp/photos/scan.png"));
    QCOMPARE(r[1].docs[0].bytes, blobB);
}

void TestSessionDraftStore::hasSessionAndClearLifecycle() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SessionDraftStore store(dir.path() + "/session-drafts");

    QVERIFY(!store.hasSession()); // nothing written yet

    SessionWindowDescriptor win;
    SessionDocDescriptor doc;
    doc.kind = SessionDocDescriptor::Kind::Draft;
    doc.bytes = knownBytes(0x01, 64);
    doc.untitled = true;
    win.docs.append(doc);
    QVERIFY(store.save({win}));
    QVERIFY(store.hasSession());

    store.clear();
    QVERIFY(!store.hasSession());
    QVERIFY(store.restore().isEmpty());
}

void TestSessionDraftStore::unreadableStoreYieldsNoSession() {
    // Case A: a manifest with a bogus version.
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString sub = dir.path() + "/session-drafts";
        QDir().mkpath(sub);
        QFile f(sub + "/manifest.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"version\": 9999, \"windows\": [{\"docs\": []}]}");
        f.close();

        SessionDraftStore store(sub);
        QVERIFY(!store.hasSession());
        QVERIFY(store.restore().isEmpty());
    }

    // Case B: corrupt / truncated JSON — the manifest is present but not
    // parseable. Honest-restore: yield nothing rather than mis-parse. Guards
    // against a crash-truncated or partially-written manifest.
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString sub = dir.path() + "/session-drafts";
        QDir().mkpath(sub);
        QFile f(sub + "/manifest.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        // Valid opening, then abruptly cut off (as a crash mid-write leaves it).
        f.write("{\"version\": 1, \"windows\": [{\"docs\": [{\"kind\": \"dra");
        f.close();

        SessionDraftStore store(sub);
        QVERIFY(!store.hasSession());
        QVERIFY(store.restore().isEmpty());
    }
}

void TestSessionDraftStore::dprAndCaptureOriginRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SessionDraftStore store(dir.path() + "/session-drafts");

    SessionDocDescriptor doc;
    doc.kind = SessionDocDescriptor::Kind::Draft;
    doc.bytes = knownBytes(0x33, 512);
    doc.untitled = true;
    doc.devicePixelRatio = 2.0; // a Retina screenshot
    doc.captureOrigin = true;

    SessionWindowDescriptor win;
    win.docs.append(doc);
    QVERIFY(store.save({win}));

    SessionDraftStore reopened(dir.path() + "/session-drafts");
    const QList<SessionWindowDescriptor> r = reopened.restore();
    QCOMPARE(r.size(), 1);
    QCOMPARE(r[0].docs.size(), 1);
    const SessionDocDescriptor &out = r[0].docs[0];
    // Asserted DIRECTLY — QImage::operator== (used by the pixel round-trip
    // test) ignores devicePixelRatio, so the manifest field is the only
    // honest witness that dpr survived.
    QCOMPARE(out.devicePixelRatio, 2.0);
    QVERIFY(out.captureOrigin);
    // A default (no-capture) draft round-trips as dpr 1 / not-capture.
    QCOMPARE(out.bytes, doc.bytes);
}

void TestSessionDraftStore::failedSaveKeepsPriorSessionIntact() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sub = dir.path() + "/session-drafts";
    SessionDraftStore store(sub);

    // Write a valid PRIOR session.
    const QByteArray priorBytes = knownBytes(0x44, 777);
    SessionWindowDescriptor prior;
    SessionDocDescriptor pd;
    pd.kind = SessionDocDescriptor::Kind::Draft;
    pd.bytes = priorBytes;
    pd.untitled = true;
    prior.docs.append(pd);
    QVERIFY(store.save({prior}));
    QVERIFY(store.hasSession());

    // Sabotage the NEXT save: park a regular FILE where the staging
    // directory must be created, so mkpath(staging) fails and save() bails
    // out before ever touching the live session. (Works regardless of the
    // test uid — you cannot mkdir underneath a file even as root.)
    QFile block(sub + ".staging");
    QVERIFY(block.open(QIODevice::WriteOnly));
    block.write("x");
    block.close();

    SessionWindowDescriptor next;
    SessionDocDescriptor nd;
    nd.kind = SessionDocDescriptor::Kind::Draft;
    nd.bytes = knownBytes(0x66, 111);
    nd.untitled = true;
    next.docs.append(nd);
    QVERIFY(!store.save({next})); // the save fails

    // The PRIOR session must still be intact and readable — NOT wiped.
    QVERIFY(store.hasSession());
    const QList<SessionWindowDescriptor> r = store.restore();
    QCOMPARE(r.size(), 1);
    QCOMPARE(r[0].docs.size(), 1);
    QCOMPARE(r[0].docs[0].bytes, priorBytes);
}

void TestSessionDraftStore::failedSwapKeepsPriorSessionIntact() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sub = dir.path() + "/session-drafts";
    SessionDraftStore store(sub);

    // Write a valid PRIOR session.
    const QByteArray priorBytes = knownBytes(0x22, 999);
    SessionWindowDescriptor prior;
    SessionDocDescriptor pd;
    pd.kind = SessionDocDescriptor::Kind::Draft;
    pd.bytes = priorBytes;
    pd.untitled = true;
    prior.docs.append(pd);
    QVERIFY(store.save({prior}));
    QVERIFY(store.hasSession());

    // Sabotage the SWAP itself (not staging creation): park a regular FILE at
    // the `.backup` path so `rename(live -> .backup)` — the first step of the
    // two-rename promote — fails. Staging is fully built by then, so this
    // exercises the promote phase specifically. The live session is renamed
    // only if that first rename succeeds, so it must remain untouched here.
    // (A plain file is not a directory, so save()'s stale-.backup cleanup —
    // which only removes an existing directory — leaves it in place.)
    QFile block(sub + ".backup");
    QVERIFY(block.open(QIODevice::WriteOnly));
    block.write("x");
    block.close();

    SessionWindowDescriptor next;
    SessionDocDescriptor nd;
    nd.kind = SessionDocDescriptor::Kind::Draft;
    nd.bytes = knownBytes(0x77, 222);
    nd.untitled = true;
    next.docs.append(nd);
    QVERIFY(!store.save({next})); // the swap fails

    // The PRIOR session must still be intact and readable — NOT lost in the
    // gap between retiring the old session and publishing the new one.
    QVERIFY(store.hasSession());
    const QList<SessionWindowDescriptor> r = store.restore();
    QCOMPARE(r.size(), 1);
    QCOMPARE(r[0].docs.size(), 1);
    QCOMPARE(r[0].docs[0].bytes, priorBytes);
}

QTEST_GUILESS_MAIN(TestSessionDraftStore)
#include "test_session_draft_store.moc"
