#include "uxrecord/UxRecord.h"
#include "uxrecord/UxRecorder.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

QJsonObject readJsonFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QList<QJsonObject> readEvents(const QString &sessionDir) {
    QList<QJsonObject> events;
    QFile file(QDir(sessionDir).filePath(QStringLiteral("events.jsonl")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return events;
    }
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        events.append(QJsonDocument::fromJson(line).object());
    }
    return events;
}

bool hasEventOfType(const QList<QJsonObject> &events, const QString &type) {
    for (const QJsonObject &event : events) {
        if (event.value(QStringLiteral("type")).toString() == type) {
            return true;
        }
    }
    return false;
}

} // namespace

// Platform-neutral UxRecorder lifecycle. Constructed with
// withPlatformCapture=false so no screen/camera/input backend starts —
// everything here runs headless on any platform.
class TestUxRecorder : public QObject {
    Q_OBJECT
  private slots:
    void sessionDirectoryLayout();
    void facadeReflectsActiveSession();
    void markersAndSemanticEvents();
    void cleanStopMarksManifestComplete();
    void staleRecordingSessionIsMarkedCrashed();
    void degradedStreamEventMapping();
    void degradedStreamsRecordedInManifest();
};

void TestUxRecorder::sessionDirectoryLayout() {
    QTemporaryDir base;
    QVERIFY(base.isValid());

    UxRecorder recorder(base.path(), /*withPlatformCapture=*/false);
    QVERIFY(recorder.start());
    const QString sessionDir = recorder.sessionDir();
    QVERIFY(sessionDir.startsWith(base.path()));
    QVERIFY(QDir(sessionDir).exists(QStringLiteral("screen")));
    QVERIFY(QDir(sessionDir).exists(QStringLiteral("camera")));
    QVERIFY(QDir(sessionDir).exists(QStringLiteral("screenshots")));
    QVERIFY(QFileInfo::exists(QDir(sessionDir).filePath(QStringLiteral("metadata.json"))));
    QVERIFY(QFileInfo::exists(QDir(sessionDir).filePath(QStringLiteral("manifest.json"))));
    QVERIFY(QFileInfo::exists(QDir(sessionDir).filePath(QStringLiteral("events.jsonl"))));
    QVERIFY(QFileInfo::exists(QDir(sessionDir).filePath(QStringLiteral("trailer.log"))));

    const QJsonObject metadata =
        readJsonFile(QDir(sessionDir).filePath(QStringLiteral("metadata.json")));
    QCOMPARE(metadata.value(QStringLiteral("app")).toString(), QStringLiteral("Trailer"));
    QCOMPARE(metadata.value(QStringLiteral("session_id")).toString(), recorder.sessionId());
    QVERIFY(metadata.contains(QStringLiteral("qt_version")));
    QVERIFY(metadata.contains(QStringLiteral("started_utc")));
    QVERIFY(metadata.value(QStringLiteral("config")).isObject());

    const QJsonObject manifest =
        readJsonFile(QDir(sessionDir).filePath(QStringLiteral("manifest.json")));
    QCOMPARE(manifest.value(QStringLiteral("status")).toString(), QStringLiteral("recording"));

    recorder.stop();
}

void TestUxRecorder::facadeReflectsActiveSession() {
    QTemporaryDir base;
    QVERIFY(base.isValid());

    QVERIFY(!uxrecord::isActive());
    {
        UxRecorder recorder(base.path(), /*withPlatformCapture=*/false);
        QVERIFY(recorder.start());
        QVERIFY(uxrecord::isActive());
        QCOMPARE(uxrecord::recorder(), &recorder);

        // Facade-routed semantic event lands in the stream.
        uxrecord::recordEvent(QStringLiteral("tool_selected"),
                              QJsonObject{{QStringLiteral("tool"), QStringLiteral("ink")}});
        recorder.stop();
        QVERIFY(!uxrecord::isActive());

        const auto events = readEvents(recorder.sessionDir());
        QVERIFY(hasEventOfType(events, QStringLiteral("tool_selected")));
    }
    QVERIFY(uxrecord::recorder() == nullptr);
}

void TestUxRecorder::markersAndSemanticEvents() {
    QTemporaryDir base;
    QVERIFY(base.isValid());

    UxRecorder recorder(base.path(), /*withPlatformCapture=*/false);
    QVERIFY(recorder.start());

    recorder.insertMarker(QStringLiteral("frustration"));
    recorder.insertMarker(QStringLiteral("note"), QStringLiteral("zoom felt wrong"));

    // Markers flush synchronously (they must survive a crash right
    // after the user flags a moment), so the file is already current —
    // no stop() or timer tick needed for these.
    const auto flushedEvents = readEvents(recorder.sessionDir());
    QVERIFY(hasEventOfType(flushedEvents, QStringLiteral("manual_marker")));

    // Plain semantic events batch until the next flush; visible after
    // stop().
    recorder.recordTrailerEvent(
        QStringLiteral("preview_fallback_started"),
        QJsonObject{{QStringLiteral("document_kind"), QStringLiteral("pdf")},
                    {QStringLiteral("page"), 14},
                    {QStringLiteral("reason"), QStringLiteral("explicit_preview_fallback")}});
    QVERIFY(!hasEventOfType(readEvents(recorder.sessionDir()),
                            QStringLiteral("preview_fallback_started")));
    recorder.stop();

    const auto events = readEvents(recorder.sessionDir());
    int markers = 0;
    QString noteText;
    for (const QJsonObject &event : events) {
        if (event.value(QStringLiteral("type")).toString() == QLatin1String("manual_marker")) {
            ++markers;
            const QJsonObject data = event.value(QStringLiteral("data")).toObject();
            if (data.value(QStringLiteral("kind")).toString() == QLatin1String("note")) {
                noteText = data.value(QStringLiteral("note")).toString();
            }
        }
    }
    QCOMPARE(markers, 2);
    QCOMPARE(noteText, QStringLiteral("zoom felt wrong"));
    QVERIFY(hasEventOfType(events, QStringLiteral("preview_fallback_started")));
    QVERIFY(hasEventOfType(events, QStringLiteral("session_started")));
}

void TestUxRecorder::cleanStopMarksManifestComplete() {
    QTemporaryDir base;
    QVERIFY(base.isValid());

    UxRecorder recorder(base.path(), /*withPlatformCapture=*/false);
    QVERIFY(recorder.start());
    recorder.recordTrailerEvent(QStringLiteral("anything"));
    const QString sessionDir = recorder.sessionDir();
    recorder.stop();
    // Idempotent: a second stop (e.g. dtor after aboutToQuit) is fine.
    recorder.stop();

    const QJsonObject manifest =
        readJsonFile(QDir(sessionDir).filePath(QStringLiteral("manifest.json")));
    QCOMPARE(manifest.value(QStringLiteral("status")).toString(), QStringLiteral("complete"));
    QVERIFY(manifest.contains(QStringLiteral("stopped_utc")));
    QVERIFY(manifest.value(QStringLiteral("event_count")).toInt() > 0);

    const auto events = readEvents(sessionDir);
    QVERIFY(hasEventOfType(events, QStringLiteral("session_started")));
    QVERIFY(hasEventOfType(events, QStringLiteral("session_stopped")));
}

void TestUxRecorder::staleRecordingSessionIsMarkedCrashed() {
    QTemporaryDir base;
    QVERIFY(base.isValid());

    // Forge a session that "crashed": status still recording, pid long
    // gone. (Largest-possible pid keeps the liveness probe honest.)
    const QString staleDir =
        QDir(base.path()).filePath(QStringLiteral("2026-01-01T00-00-00Z-dead"));
    QVERIFY(QDir().mkpath(staleDir));
    QFile manifest(QDir(staleDir).filePath(QStringLiteral("manifest.json")));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    manifest.write(
        QJsonDocument(QJsonObject{{QStringLiteral("session_id"), QStringLiteral("dead")},
                                  {QStringLiteral("status"), QStringLiteral("recording")},
                                  {QStringLiteral("pid"), 1073741823}})
            .toJson());
    manifest.close();

    UxRecorder recorder(base.path(), /*withPlatformCapture=*/false);
    QVERIFY(recorder.start());
    recorder.stop();

    const QJsonObject updated =
        readJsonFile(QDir(staleDir).filePath(QStringLiteral("manifest.json")));
#if defined(Q_OS_WIN)
    // No pid-liveness probe on Windows: the sweep deliberately leaves
    // the session untouched rather than risk mislabelling one owned by
    // a concurrent instance.
    QCOMPARE(updated.value(QStringLiteral("status")).toString(), QStringLiteral("recording"));
#else
    QCOMPARE(updated.value(QStringLiteral("status")).toString(), QStringLiteral("crashed"));
    QVERIFY(updated.contains(QStringLiteral("crash_detected_utc")));
#endif
}

void TestUxRecorder::degradedStreamEventMapping() {
    // The pure classifier the start() chokepoint uses (UXR-002).
    QCOMPARE(uxDegradedStreamForEventType(QStringLiteral("screen_capture_failed")),
             QStringLiteral("screen"));
    QCOMPARE(uxDegradedStreamForEventType(QStringLiteral("screen_recording_permission_pending")),
             QStringLiteral("screen"));
    QCOMPARE(uxDegradedStreamForEventType(QStringLiteral("camera_permission_denied")),
             QStringLiteral("camera"));
    QCOMPARE(uxDegradedStreamForEventType(QStringLiteral("input_tap_unavailable")),
             QStringLiteral("input"));
    // Healthy / advisory events do NOT degrade a stream — notably the
    // input_monitoring_permission preflight (the tap often still
    // delivers pointer events when it reports not-granted; UXR-003).
    QVERIFY(uxDegradedStreamForEventType(QStringLiteral("screen_frame")).isEmpty());
    QVERIFY(uxDegradedStreamForEventType(QStringLiteral("camera_started")).isEmpty());
    QVERIFY(uxDegradedStreamForEventType(QStringLiteral("input_monitoring_permission")).isEmpty());
}

void TestUxRecorder::degradedStreamsRecordedInManifest() {
    QTemporaryDir base;
    QVERIFY(base.isValid());

    UxRecorder recorder(base.path(), /*withPlatformCapture=*/false);
    QVERIFY(recorder.start());
    const QString sessionDir = recorder.sessionDir();

    QSignalSpy spy(&recorder, &UxRecorder::degradedStreamsChanged);

    // A live "recording" manifest reflects degradation immediately, so
    // even a crashed session shows which streams failed.
    recorder.reportStreamDegraded(QStringLiteral("screen"));
    recorder.reportStreamDegraded(QStringLiteral("screen")); // idempotent
    recorder.reportStreamDegraded(QStringLiteral("camera"));
    QCOMPARE(spy.count(), 2); // one per distinct stream

    const QJsonObject recordingManifest =
        readJsonFile(QDir(sessionDir).filePath(QStringLiteral("manifest.json")));
    QStringList recDegraded;
    for (const QJsonValue &v : recordingManifest.value(QStringLiteral("degraded")).toArray()) {
        recDegraded << v.toString();
    }
    QCOMPARE(recDegraded, (QStringList{QStringLiteral("camera"), QStringLiteral("screen")}));

    recorder.stop();
    const QJsonObject completeManifest =
        readJsonFile(QDir(sessionDir).filePath(QStringLiteral("manifest.json")));
    QCOMPARE(completeManifest.value(QStringLiteral("status")).toString(),
             QStringLiteral("complete"));
    QStringList doneDegraded;
    for (const QJsonValue &v : completeManifest.value(QStringLiteral("degraded")).toArray()) {
        doneDegraded << v.toString();
    }
    QCOMPARE(doneDegraded, (QStringList{QStringLiteral("camera"), QStringLiteral("screen")}));

    // After stop, further reports are ignored (no resurrecting the
    // manifest into "recording").
    recorder.reportStreamDegraded(QStringLiteral("input"));
    QCOMPARE(recorder.degradedStreams(),
             (QStringList{QStringLiteral("camera"), QStringLiteral("screen")}));
}

QTEST_MAIN(TestUxRecorder)
#include "test_ux_recorder.moc"
