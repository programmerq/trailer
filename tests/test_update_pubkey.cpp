// Guards the generated update-channel public key
// (cmake/UpdatePublicKey.h.in, rendered by CMakeLists.txt's
// TRAILER_UPDATE_PUBKEY block) and the behaviour that hangs off it.
//
// These assertions hold in BOTH configurations — a build with a key and
// a build without one — so the same test binary is meaningful on a
// developer's machine, on a PR runner, and on the nightly lanes that
// pass a real key. What it pins is the CONSISTENCY between the flag and
// the bytes, plus the refuse-before-the-network contract; it deliberately
// does not hardcode any particular key, which would just be the old
// committed-literal problem wearing a test's clothes.

#include "UpdatePublicKey.h"
#include "settings/Settings.h"
#include "update/UpdateFeedParser.h"
#include "update/UpdateManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>

using namespace trailer;

namespace {

// Run a program, return true on a clean exit(0). stdout is captured for
// the caller; stderr is folded into the failure message. `env`, when
// given, replaces the child's environment wholesale.
bool run(const QString &program, const QStringList &args, QByteArray *stdOut = nullptr,
         QString *errorOut = nullptr, const QProcessEnvironment *env = nullptr) {
    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    if (env)
        p.setProcessEnvironment(*env);
    p.start();
    if (!p.waitForStarted(10000) || !p.waitForFinished(30000)) {
        if (errorOut)
            *errorOut = QStringLiteral("%1 did not run: %2").arg(program, p.errorString());
        return false;
    }
    if (stdOut)
        *stdOut = p.readAllStandardOutput();
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (errorOut)
            *errorOut = QStringLiteral("%1 exited %2: %3")
                            .arg(program)
                            .arg(p.exitCode())
                            .arg(QString::fromLocal8Bit(p.readAllStandardError()));
        return false;
    }
    return true;
}

bool haveTool(const QString &program, const QStringList &probeArgs) {
    return run(program, probeArgs);
}

// Resolve an openssl that can actually do ed25519, via the shared probe
// in scripts/find-openssl-ed25519.sh. Returns an empty string and fills
// `whyNot` when this host has none.
//
// The guard this replaces asked `openssl version` — "does *an* openssl
// exist", not "can it do the thing". macOS answers that question with
// LibreSSL, whose genpkey has no ed25519, so the guard passed and the
// test then failed on its first real invocation: nightly-20260804 (run
// 30907188776, macOS job 91985006187) got `Algorithm ed25519 not found`
// plus a usage dump, and the DMG was never built. Capability, not
// existence — and probed rather than version-sniffed, so it stays
// correct whatever a given fork calls itself.
//
// The resolution is delegated to the script rather than reimplemented
// here because scripts/test-derive-update-pubkey.sh needs the identical
// answer; two copies of "which openssl" would drift.
QString resolveOpenssl(const QString &repoRoot, QString *whyNot) {
    const QString finder = repoRoot + QStringLiteral("/scripts/find-openssl-ed25519.sh");
    if (!QFile::exists(finder)) {
        *whyNot = QStringLiteral("%1 not found next to this build").arg(finder);
        return {};
    }
    QByteArray out;
    if (!run(QStringLiteral("bash"), {finder}, &out, whyNot))
        return {};
    const QString path = QString::fromLocal8Bit(out).trimmed();
    if (path.isEmpty())
        *whyNot = QStringLiteral("%1 exited 0 but printed no path").arg(finder);
    return path;
}

} // namespace

class TestUpdatePubkey : public QObject {
    Q_OBJECT
  private slots:
    void flagAgreesWithBytes();
    void managerReportsTheSameFlag();
    void unprovisionedCheckNowFailsWithoutNetwork();
    void opensslSignedFeedVerifiesAgainstDerivedKey();

  private:
    static bool keyIsAllZero() {
        return std::all_of(Update::kNightlyPublicKey.begin(), Update::kNightlyPublicKey.end(),
                           [](unsigned char b) { return b == 0; });
    }
};

// The one way the generator can betray us silently: emit a `true` flag
// with a zero key (build claims it can verify, then rejects everything),
// or a `false` flag with real bytes (build refuses to use a key it has).
// Either would compile and ship.
void TestUpdatePubkey::flagAgreesWithBytes() {
    QCOMPARE(Update::kUpdateChannelProvisioned, !keyIsAllZero());
}

void TestUpdatePubkey::managerReportsTheSameFlag() {
    QCOMPARE(Update::UpdateManager::isChannelProvisioned(), Update::kUpdateChannelProvisioned);
}

// An unprovisioned build must fail IMMEDIATELY and locally. The
// synchronous state transition is the observable proxy for "no request
// was issued": UpdateChecker's real path cannot reach State::Error
// without at least one event-loop turn for the QNetworkReply, so an
// Error that is already set when checkNow() returns could only have come
// from the pre-network guard.
void TestUpdatePubkey::unprovisionedCheckNowFailsWithoutNetwork() {
    if (Update::kUpdateChannelProvisioned)
        QSKIP("This build has a signing key; the unprovisioned guard is not reachable here.");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings settings(dir.filePath(QStringLiteral("settings.toml")));
    Update::UpdateManager manager(settings);

    QCOMPARE(manager.state(), Update::UpdateManager::State::Idle);
    manager.checkNow();
    QCOMPARE(manager.state(), Update::UpdateManager::State::Error);
    QVERIFY(!manager.lastError().isEmpty());
}

// The interop link nothing else covers. tests/test_update_feed_parser.cpp
// signs its fixtures with Ed25519::signForTesting — tweetnacl verifying
// tweetnacl — so it cannot catch a mismatch between the PRODUCTION
// signer (scripts/sign-update-feed.sh, i.e. `openssl pkeyutl -rawin`) and
// the shipped verifier. A disagreement there — a digest prefix, a
// different signature encoding, base64 padding — would produce a feed
// that signs cleanly in CI and is rejected by every user's build, with
// nothing red anywhere until someone tried to update.
//
// This test drives the REAL scripts against a keypair it generates
// itself, so it needs no secret and is meaningful on any lane that has
// python3 and an openssl that can actually do ed25519 — which is not the
// same as "has openssl": see resolveOpenssl(). It deliberately goes
// through scripts/derive-update-pubkey.sh for the public key too, which
// makes it a guard on that script's 44-vs-32-byte trimming as well.
void TestUpdatePubkey::opensslSignedFeedVerifiesAgainstDerivedKey() {
    const QString repoRoot = QStringLiteral(TRAILER_REPO_ROOT);
    const QString derive = repoRoot + QStringLiteral("/scripts/derive-update-pubkey.sh");
    const QString sign = repoRoot + QStringLiteral("/scripts/sign-update-feed.sh");

    if (!QFile::exists(derive) || !QFile::exists(sign))
        QSKIP("Signing scripts not found next to this build; skipping interop check.");

    // Capability, not existence — see resolveOpenssl(). A lane without a
    // usable openssl (macOS's LibreSSL, or Wine, where nothing spawns)
    // SKIPS and says why; it does not fail, and it does not pass quietly
    // either. The interop property this test guards is a property of the
    // Linux publish runner, which is where nightly.yml actually signs the
    // feed, and it stays gated there on every PR.
    QString whyNot;
    const QString openssl = resolveOpenssl(repoRoot, &whyNot);
    if (openssl.isEmpty()) {
        const QByteArray msg =
            QStringLiteral("No ed25519-capable openssl here, so the production-signer interop "
                           "check is UNCHECKED on this lane (it still gates every PR on Linux "
                           "CI, which is where the feed is really signed). Reason: %1")
                .arg(whyNot)
                .toLocal8Bit();
        QSKIP(msg.constData());
    }
    if (!haveTool(QStringLiteral("python3"), {QStringLiteral("--version")}))
        QSKIP("python3 not runnable here; sign-update-feed.sh cannot run.");

    // Both scripts invoke a bare `openssl`, so put the binary this test
    // just probed first on their PATH. Without this, a Mac would probe
    // Homebrew's openssl@3 (keg-only, deliberately not symlinked into
    // /opt/homebrew/bin) and then hand the scripts LibreSSL anyway.
    QProcessEnvironment scriptEnv = QProcessEnvironment::systemEnvironment();
    const QString opensslDir = QFileInfo(openssl).absolutePath();
    if (!opensslDir.isEmpty()) {
        const QString key = QStringLiteral("PATH");
        scriptEnv.insert(key, opensslDir + QDir::listSeparator() + scriptEnv.value(key));
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString keyPath = dir.filePath(QStringLiteral("key.pem"));
    const QString payloadPath = dir.filePath(QStringLiteral("payload.json"));
    const QString feedPath = dir.filePath(QStringLiteral("feed.json"));
    QString err;

    QVERIFY2(run(openssl,
                 {QStringLiteral("genpkey"), QStringLiteral("-algorithm"),
                  QStringLiteral("ed25519"), QStringLiteral("-out"), keyPath},
                 nullptr, &err),
             qPrintable(err));

    // Public key, via the same script CI uses to configure the build.
    QByteArray hexOut;
    QVERIFY2(run(QStringLiteral("bash"), {derive, keyPath}, &hexOut, &err, &scriptEnv),
             qPrintable(err));
    const QByteArray hex = hexOut.trimmed();
    QCOMPARE(hex.size(), 64);

    std::array<unsigned char, 32> pubKey{};
    for (int i = 0; i < 32; ++i)
        pubKey[static_cast<size_t>(i)] =
            static_cast<unsigned char>(hex.mid(i * 2, 2).toUShort(nullptr, 16));

    // A payload shaped like the real one (see UpdateFeedParser.h). The
    // exact fields matter less than that the bytes signed are the bytes
    // verified.
    const QByteArray payload = QByteArrayLiteral(
        R"({"channel":"nightly","entries":[{"tag":"nightly-20260802",)"
        R"("build_number":4900,"published_at":"2026-08-02T11:36:42Z",)"
        R"("notes":"interop fixture","assets":{"macos":{"url":)"
        R"("https://example.invalid/Trailer.dmg","sha256":)"
        R"("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}}}]})");
    {
        QFile f(payloadPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(payload), static_cast<qint64>(payload.size()));
    }

    QVERIFY2(run(QStringLiteral("bash"), {sign, payloadPath, keyPath, feedPath}, nullptr, &err,
                 &scriptEnv),
             qPrintable(err));

    QFile feed(feedPath);
    QVERIFY(feed.open(QIODevice::ReadOnly));
    const QByteArray feedBytes = feed.readAll();
    QVERIFY(!feedBytes.isEmpty());

    // The actual assertion: our shipped verifier accepts what our shipped
    // signer produced, using the key our shipped derivation extracted.
    const Update::ParsedFeed parsed = Update::parseAndVerifyFeed(feedBytes, pubKey);
    QVERIFY2(parsed.ok, qPrintable(parsed.error));

    // ...and rejects the same feed under a different key, so the check
    // above cannot be passing because verification is a no-op.
    std::array<unsigned char, 32> wrongKey = pubKey;
    wrongKey[0] = static_cast<unsigned char>(wrongKey[0] ^ 0xffu);
    const Update::ParsedFeed bad = Update::parseAndVerifyFeed(feedBytes, wrongKey);
    QVERIFY(!bad.ok);
}

QTEST_MAIN(TestUpdatePubkey)
#include "test_update_pubkey.moc"
