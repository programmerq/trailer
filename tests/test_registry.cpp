#include "document/DocumentRegistry.h"
#include "document/IDocument.h"
#include "document/IFormatAdapter.h"

#include <QLabel>
#include <QObject>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

class FakeDocument : public IDocument {
public:
    FakeDocument(QString name, QString path)
        : m_name(std::move(name)), m_path(std::move(path)) {}
    QString displayName() const override { return m_name; }
    QString filePath() const override { return m_path; }
    QWidget* createView(QWidget* parent) override { return new QLabel(m_name, parent); }
private:
    QString m_name;
    QString m_path;
};

class FakeTxtAdapter : public IFormatAdapter {
public:
    QStringList mimeTypes() const override { return {"text/plain"}; }
    QStringList extensions() const override { return {"txt"}; }
    std::unique_ptr<IDocument> open(const QString& path) override {
        return std::make_unique<FakeDocument>(QStringLiteral("fake"), path);
    }
};

}  // namespace

class TestRegistry : public QObject {
    Q_OBJECT
private slots:
    void fallsBackToStubForUnknownExtension();
    void dispatchesToRegisteredAdapterByExtension();
};

void TestRegistry::fallsBackToStubForUnknownExtension() {
    DocumentRegistry reg;
    auto doc = reg.open("/tmp/whatever.xyz123");
    QVERIFY(doc != nullptr);
    QCOMPARE(doc->displayName(), QStringLiteral("whatever.xyz123"));
}

void TestRegistry::dispatchesToRegisteredAdapterByExtension() {
    DocumentRegistry reg;
    reg.registerAdapter(std::make_unique<FakeTxtAdapter>());
    auto doc = reg.open("/tmp/hello.txt");
    QVERIFY(doc != nullptr);
    QCOMPARE(doc->displayName(), QStringLiteral("fake"));
}

QTEST_MAIN(TestRegistry)
#include "test_registry.moc"
