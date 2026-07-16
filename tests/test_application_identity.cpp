#include "app/Application.h"

#include <QCoreApplication>
#include <QtTest>

class TestApplicationIdentity : public QObject {
    Q_OBJECT
private slots:
    void identityStringsMatchGithubScheme() {
        // The CFBundleIdentifier (io.github.programmerq.trailer) lives in CMake
        // (MACOSX_BUNDLE_GUI_IDENTIFIER) and is unreachable from C++, so this
        // test guards the Qt identity strings only.
        trailer::Application::applyIdentity();
        QCOMPARE(QCoreApplication::applicationName(), QStringLiteral("Trailer"));
        QCOMPARE(QCoreApplication::organizationName(), QStringLiteral("Trailer"));
        QCOMPARE(QCoreApplication::organizationDomain(), QStringLiteral("programmerq.github.io"));
        QVERIFY(!QCoreApplication::applicationVersion().isEmpty());
    }
};

QTEST_MAIN(TestApplicationIdentity)
#include "test_application_identity.moc"
