#include "uxrecord/UxEventStream.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

// Read every JSONL line back as parsed objects, failing the test on
// any malformed line.
QList<QJsonObject> readEvents(const QString &path) {
    QList<QJsonObject> events;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return events;
    }
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            return {};
        }
        events.append(doc.object());
    }
    return events;
}

} // namespace

class TestUxEventStream : public QObject {
    Q_OBJECT
  private slots:
    void envelopeFieldsAndOrdering();
    void appendBeforeOpenIsRejected();
    void bufferDrainsOnFlushAndClose();
    void timestampsAreUtcAndElapsedMonotonic();
};

void TestUxEventStream::envelopeFieldsAndOrdering() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("events.jsonl"));

    UxEventStream stream;
    QVERIFY(stream.open(path, QStringLiteral("sess-test")));
    QCOMPARE(stream.append(QStringLiteral("session"), QStringLiteral("session_started")),
             quint64(1));
    QCOMPARE(stream.append(QStringLiteral("trailer"), QStringLiteral("document_opened"),
                           QJsonObject{{QStringLiteral("document"), QStringLiteral("a.pdf")}}),
             quint64(2));
    QCOMPARE(stream.append(QStringLiteral("qt"), QStringLiteral("key")), quint64(3));
    stream.close();

    const auto events = readEvents(path);
    QCOMPARE(events.size(), 3);

    // Envelope shape on every event.
    for (const QJsonObject &event : events) {
        QCOMPARE(event.value(QStringLiteral("schema_version")).toInt(), kUxSchemaVersion);
        QCOMPARE(event.value(QStringLiteral("session_id")).toString(), QStringLiteral("sess-test"));
        QVERIFY(event.contains(QStringLiteral("sequence")));
        QVERIFY(event.contains(QStringLiteral("timestamp_utc")));
        QVERIFY(event.contains(QStringLiteral("elapsed_ms")));
        QVERIFY(event.contains(QStringLiteral("source")));
        QVERIFY(event.contains(QStringLiteral("type")));
        QVERIFY(event.value(QStringLiteral("data")).isObject());
    }

    // Sequence is strictly increasing from 1 and payloads survive.
    QCOMPARE(events[0].value(QStringLiteral("sequence")).toInt(), 1);
    QCOMPARE(events[1].value(QStringLiteral("sequence")).toInt(), 2);
    QCOMPARE(events[2].value(QStringLiteral("sequence")).toInt(), 3);
    QCOMPARE(events[1].value(QStringLiteral("type")).toString(), QStringLiteral("document_opened"));
    QCOMPARE(events[1]
                 .value(QStringLiteral("data"))
                 .toObject()
                 .value(QStringLiteral("document"))
                 .toString(),
             QStringLiteral("a.pdf"));
    QCOMPARE(stream.eventCount(), quint64(3));
}

void TestUxEventStream::appendBeforeOpenIsRejected() {
    UxEventStream stream;
    QCOMPARE(stream.append(QStringLiteral("trailer"), QStringLiteral("x")), quint64(0));
    QVERIFY(!stream.isOpen());
    QCOMPARE(stream.elapsedMs(), qint64(0));
}

void TestUxEventStream::bufferDrainsOnFlushAndClose() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("events.jsonl"));

    UxEventStream stream;
    QVERIFY(stream.open(path, QStringLiteral("sess-flush")));
    stream.append(QStringLiteral("trailer"), QStringLiteral("one"));
    stream.append(QStringLiteral("trailer"), QStringLiteral("two"));

    // Events buffer in memory until an explicit flush — write batching
    // is the whole point (no synchronous disk write per event).
    QCOMPARE(QFileInfo(path).size(), qint64(0));
    stream.flush();
    const auto afterFlush = readEvents(path);
    QCOMPARE(afterFlush.size(), 2);

    // close() drains whatever arrived after the last flush.
    stream.append(QStringLiteral("trailer"), QStringLiteral("three"));
    stream.close();
    const auto afterClose = readEvents(path);
    QCOMPARE(afterClose.size(), 3);
    QCOMPARE(afterClose[2].value(QStringLiteral("type")).toString(), QStringLiteral("three"));

    // Closed stream rejects further appends.
    QCOMPARE(stream.append(QStringLiteral("trailer"), QStringLiteral("late")), quint64(0));
}

void TestUxEventStream::timestampsAreUtcAndElapsedMonotonic() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("events.jsonl"));

    UxEventStream stream;
    QVERIFY(stream.open(path, QStringLiteral("sess-time")));
    stream.append(QStringLiteral("trailer"), QStringLiteral("first"));
    QTest::qWait(20);
    stream.append(QStringLiteral("trailer"), QStringLiteral("second"));
    stream.close();

    const auto events = readEvents(path);
    QCOMPARE(events.size(), 2);

    const QDateTime first = QDateTime::fromString(
        events[0].value(QStringLiteral("timestamp_utc")).toString(), Qt::ISODateWithMs);
    QVERIFY(first.isValid());
    QCOMPARE(first.timeSpec(), Qt::UTC);

    const qint64 elapsed0 = events[0].value(QStringLiteral("elapsed_ms")).toVariant().toLongLong();
    const qint64 elapsed1 = events[1].value(QStringLiteral("elapsed_ms")).toVariant().toLongLong();
    QVERIFY(elapsed0 >= 0);
    QVERIFY2(elapsed1 > elapsed0, "elapsed_ms must be monotonic across appends");
}

QTEST_MAIN(TestUxEventStream)
#include "test_ux_event_stream.moc"
