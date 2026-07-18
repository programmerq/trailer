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
    // A version mismatch in the manifest yields no session (honest-restore:
    // better to restore nothing than to mis-parse a foreign store).
    void unreadableStoreYieldsNoSession();
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
    doc.displayName = QStringLiteral("Untitled");

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
    QCOMPARE(out.displayName, QStringLiteral("Untitled"));
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
    d1.displayName = QStringLiteral("Untitled");
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
    d3.displayName = QStringLiteral("scan.png");
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
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sub = dir.path() + "/session-drafts";
    QDir().mkpath(sub);
    // Write a manifest with a bogus version.
    QFile f(sub + "/manifest.json");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{\"version\": 9999, \"windows\": [{\"docs\": []}]}");
    f.close();

    SessionDraftStore store(sub);
    QVERIFY(!store.hasSession());
    QVERIFY(store.restore().isEmpty());
}

QTEST_GUILESS_MAIN(TestSessionDraftStore)
#include "test_session_draft_store.moc"
