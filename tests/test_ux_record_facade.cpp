#include "uxrecord/UxRecord.h"

#include <QJsonObject>
#include <QObject>
#include <QtTest/QtTest>

using namespace trailer;

// The uxrecord:: facade must be harmless in every configuration this
// test compiles under:
//
//   - default builds (TRAILER_ENABLE_UX_RECORDER=OFF): everything is
//     an inline no-op;
//   - recorder builds without an active session (no --ux-record):
//     every call must early-out.
//
// This is the "compile-time-disabled behaviour" guard: MainWindow
// calls these functions unconditionally, so any configuration where
// they stop being safe no-ops would break normal app behaviour.
class TestUxRecordFacade : public QObject {
    Q_OBJECT
  private slots:
    void inactiveByDefault();
    void recordEventIsNoOpWithoutSession();
    void attachToNullWindowIsSafe();
};

void TestUxRecordFacade::inactiveByDefault() {
    QVERIFY(!uxrecord::isActive());
    QVERIFY(uxrecord::recorder() == nullptr);
}

void TestUxRecordFacade::recordEventIsNoOpWithoutSession() {
    // Must not crash, allocate sessions, or create directories.
    uxrecord::recordEvent(QStringLiteral("document_opened"),
                          QJsonObject{{QStringLiteral("document"), QStringLiteral("/tmp/x.pdf")}});
    uxrecord::recordEvent(QStringLiteral("no_data_event"));
    QVERIFY(!uxrecord::isActive());
}

void TestUxRecordFacade::attachToNullWindowIsSafe() {
    uxrecord::attachToMainWindow(nullptr);
    QVERIFY(!uxrecord::isActive());
}

QTEST_MAIN(TestUxRecordFacade)
#include "test_ux_record_facade.moc"
