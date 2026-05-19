#include "settings/DocumentTypeDefaults.h"

#include <QCoreApplication>
#include <QObject>
#include <QSettings>
#include <QUuid>
#include <QtTest/QtTest>

using namespace trailer;

class TestDocumentTypeDefaults : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase();
    void defaultsAreEmpty();
    void roundTripsPerType();
    void unknownTypeIsNoOp();

  private:
    QString m_org;
    QString m_app;
};

void TestDocumentTypeDefaults::initTestCase() {
    // Use a per-run unique QSettings org so concurrent test runs (and
    // the user's real preferences) never collide. Each test method
    // calls clear() on the QSettings group at entry to be doubly safe.
    m_org = QStringLiteral("TrailerTest");
    m_app = QStringLiteral("doctype-") + QUuid::createUuid().toString(QUuid::Id128);
}

void TestDocumentTypeDefaults::defaultsAreEmpty() {
    const QString appName = m_app + QStringLiteral("-defaults");
    QSettings(m_org, appName).clear();
    DocumentTypeDefaults defaults(m_org, appName);
    defaults.load();
    const auto pdf = defaults.forType(DocumentType::Pdf);
    QVERIFY(!pdf.hasState());
    QCOMPARE(pdf.zoomMode, ZoomMode::Custom);
    QCOMPARE(pdf.zoomFactor, 0.0);
    QCOMPARE(pdf.sidebarMode, SidebarMode::Hidden);
    QCOMPARE(pdf.markupToolbarVisible, false);
}

void TestDocumentTypeDefaults::roundTripsPerType() {
    const QString appName = m_app + QStringLiteral("-roundtrip");
    {
        // Pre-clean any stale state from a previous run with the same
        // app/org pair (paranoid; m_app already has a UUID).
        QSettings(m_org, appName).clear();

        DocumentTypeDefaults defaults(m_org, appName);
        defaults.load();

        DocumentTypeDefault pdf;
        pdf.zoomMode = ZoomMode::FitToWidth;
        pdf.zoomFactor = 1.5;
        pdf.sidebarMode = SidebarMode::TableOfContents;
        pdf.markupToolbarVisible = true;
        pdf.windowGeometry = QByteArray::fromHex("0badf00d");
        pdf.windowState = QByteArray::fromHex("cafebabe");
        defaults.setForType(DocumentType::Pdf, pdf);

        DocumentTypeDefault image;
        image.zoomMode = ZoomMode::Actual;
        image.zoomFactor = 1.0;
        image.sidebarMode = SidebarMode::Hidden;
        image.markupToolbarVisible = false;
        defaults.setForType(DocumentType::Image, image);

        defaults.save();
    }

    DocumentTypeDefaults reloaded(m_org, appName);
    reloaded.load();
    const auto pdf = reloaded.forType(DocumentType::Pdf);
    QVERIFY(pdf.hasState());
    QCOMPARE(pdf.zoomMode, ZoomMode::FitToWidth);
    QCOMPARE(pdf.zoomFactor, 1.5);
    QCOMPARE(pdf.sidebarMode, SidebarMode::TableOfContents);
    QCOMPARE(pdf.markupToolbarVisible, true);
    QCOMPARE(pdf.windowGeometry, QByteArray::fromHex("0badf00d"));
    QCOMPARE(pdf.windowState, QByteArray::fromHex("cafebabe"));

    const auto image = reloaded.forType(DocumentType::Image);
    QCOMPARE(image.zoomMode, ZoomMode::Actual);
    QCOMPARE(image.zoomFactor, 1.0);
    QCOMPARE(image.sidebarMode, SidebarMode::Hidden);
    QCOMPARE(image.markupToolbarVisible, false);

    QSettings(m_org, appName).clear();
}

void TestDocumentTypeDefaults::unknownTypeIsNoOp() {
    DocumentTypeDefaults defaults(m_org, m_app + QStringLiteral("-unknown"));
    DocumentTypeDefault v;
    v.zoomMode = ZoomMode::FitInView;
    defaults.setForType(DocumentType::Unknown, v);
    // Setting Unknown must not bleed into the Pdf / Image slots.
    QCOMPARE(defaults.forType(DocumentType::Pdf).zoomMode, ZoomMode::Custom);
    QCOMPARE(defaults.forType(DocumentType::Image).zoomMode, ZoomMode::Custom);
}

QTEST_MAIN(TestDocumentTypeDefaults)
#include "test_document_type_defaults.moc"
