#include "settings/AppPaths.h"

#include <QDir>
#include <QObject>
#include <QtTest/QtTest>

using namespace trailer;

class TestAppPaths : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void settingsAndDataAreNonEmpty();
    void fileHelpersLiveUnderDirs();
    void platformShape();
};

void TestAppPaths::initTestCase() {
    // Mirror the production Application constructor so that
    // QStandardPaths returns the same platform-specific directory the
    // real app would use (matters most on Windows and macOS).
    QCoreApplication::setApplicationName(QStringLiteral("Trailer"));
    QCoreApplication::setOrganizationName(QStringLiteral("Trailer"));
}

void TestAppPaths::settingsAndDataAreNonEmpty() {
    QVERIFY(!AppPaths::settingsDir().isEmpty());
    QVERIFY(!AppPaths::dataDir().isEmpty());
}

void TestAppPaths::fileHelpersLiveUnderDirs() {
    const QString settings = AppPaths::settingsDir();
    const QString data = AppPaths::dataDir();

    QVERIFY(AppPaths::settingsFile().startsWith(settings));
    QVERIFY(AppPaths::recentFile().startsWith(data));
    QVERIFY(AppPaths::signaturesDir().startsWith(data));
    QVERIFY(AppPaths::autofillDir().startsWith(data));
    QVERIFY(AppPaths::versionsDir().startsWith(data));
    QVERIFY(AppPaths::ocrCacheDir().startsWith(data));
    QVERIFY(AppPaths::iccDir().startsWith(data));
    QVERIFY(AppPaths::filtersDir().startsWith(data));
    QVERIFY(AppPaths::pluginsDir().startsWith(data));
    QVERIFY(AppPaths::logsDir().startsWith(data));
}

void TestAppPaths::platformShape() {
    const QString settings = AppPaths::settingsDir();
    const QString data = AppPaths::dataDir();

#if defined(Q_OS_MACOS)
    QVERIFY(settings.contains("/Library/Application Support/Trailer"));
    QCOMPARE(settings, data);
#elif defined(Q_OS_WIN)
    QVERIFY(settings.contains("Trailer", Qt::CaseInsensitive));
    QCOMPARE(settings, data);
#else
    QVERIFY(settings.contains("trailer"));
    QVERIFY(data.contains("trailer"));
#endif
}

QTEST_MAIN(TestAppPaths)
#include "test_app_paths.moc"
