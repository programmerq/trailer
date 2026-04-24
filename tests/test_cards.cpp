// Tests for the My Card / AutoFill machinery (design doc §6.4.2).
//
//   - autoFillValueFor matches common field-name variants to the right
//     card field, and returns empty for things it doesn't recognise.
//   - CardStore round-trips to disk with full fidelity, preserves the
//     active index, and gracefully handles missing / malformed files.

#include "cards/CardStore.h"
#include "cards/MyCard.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

class TestCards : public QObject {
    Q_OBJECT
private slots:
    void matcherHandlesCommonFieldNames();
    void matcherIgnoresUnknownFields();
    void matcherIsCaseAndSeparatorInsensitive();
    void matcherPrefersLine2OverLine1();
    void storeRoundTripsAllFields();
    void storeLoadHandlesMissingFileSilently();
    void storeRemoveClampsActiveIndex();
    void fullNameFallsBackToGivenFamily();
};

namespace {

MyCard sampleCard() {
    MyCard c;
    c.label = QStringLiteral("Personal");
    c.givenName = QStringLiteral("Alice");
    c.familyName = QStringLiteral("Example");
    c.fullName = QStringLiteral("Alice M. Example");
    c.email = QStringLiteral("alice@example.com");
    c.phone = QStringLiteral("+1 555-0100");
    c.organization = QStringLiteral("Acme Corp");
    c.jobTitle = QStringLiteral("Engineer");
    c.addressLine1 = QStringLiteral("1 Example St");
    c.addressLine2 = QStringLiteral("Apt 3B");
    c.city = QStringLiteral("Portland");
    c.state = QStringLiteral("OR");
    c.postalCode = QStringLiteral("97201");
    c.country = QStringLiteral("USA");
    return c;
}

}  // namespace

void TestCards::matcherHandlesCommonFieldNames() {
    const MyCard c = sampleCard();

    QCOMPARE(autoFillValueFor("name", c), c.fullName);
    QCOMPARE(autoFillValueFor("full_name", c), c.fullName);
    QCOMPARE(autoFillValueFor("FullName", c), c.fullName);

    QCOMPARE(autoFillValueFor("first_name", c), c.givenName);
    QCOMPARE(autoFillValueFor("firstName", c), c.givenName);
    QCOMPARE(autoFillValueFor("given_name", c), c.givenName);
    QCOMPARE(autoFillValueFor("forename", c), c.givenName);

    QCOMPARE(autoFillValueFor("last_name", c), c.familyName);
    QCOMPARE(autoFillValueFor("surname", c), c.familyName);
    QCOMPARE(autoFillValueFor("family-name", c), c.familyName);

    QCOMPARE(autoFillValueFor("email", c), c.email);
    QCOMPARE(autoFillValueFor("EmailAddress", c), c.email);
    QCOMPARE(autoFillValueFor("e-mail", c), c.email);

    QCOMPARE(autoFillValueFor("phone", c), c.phone);
    QCOMPARE(autoFillValueFor("telephone", c), c.phone);
    QCOMPARE(autoFillValueFor("phone_number", c), c.phone);

    QCOMPARE(autoFillValueFor("city", c), c.city);
    QCOMPARE(autoFillValueFor("state", c), c.state);
    QCOMPARE(autoFillValueFor("zip", c), c.postalCode);
    QCOMPARE(autoFillValueFor("postal_code", c), c.postalCode);
    QCOMPARE(autoFillValueFor("country", c), c.country);
    QCOMPARE(autoFillValueFor("company", c), c.organization);
    QCOMPARE(autoFillValueFor("employer", c), c.organization);
    QCOMPARE(autoFillValueFor("job_title", c), c.jobTitle);
}

void TestCards::matcherIgnoresUnknownFields() {
    const MyCard c = sampleCard();
    QVERIFY(autoFillValueFor(QStringLiteral(""), c).isEmpty());
    QVERIFY(autoFillValueFor(QStringLiteral("some_random_field"), c).isEmpty());
    QVERIFY(autoFillValueFor(QStringLiteral("signature"), c).isEmpty());
    QVERIFY(autoFillValueFor(QStringLiteral("dob"), c).isEmpty());
}

void TestCards::matcherIsCaseAndSeparatorInsensitive() {
    const MyCard c = sampleCard();
    QCOMPARE(autoFillValueFor(QStringLiteral("EMAIL"), c), c.email);
    QCOMPARE(autoFillValueFor(QStringLiteral("E-Mail Address"), c), c.email);
    QCOMPARE(autoFillValueFor(QStringLiteral("zip code"), c), c.postalCode);
    QCOMPARE(autoFillValueFor(QStringLiteral("ZipCode"), c), c.postalCode);
    QCOMPARE(autoFillValueFor(QStringLiteral("ZIP_CODE"), c), c.postalCode);
}

void TestCards::matcherPrefersLine2OverLine1() {
    const MyCard c = sampleCard();
    // "address 2" must not fall through to the generic address → line 1
    // branch.
    QCOMPARE(autoFillValueFor(QStringLiteral("address 2"), c), c.addressLine2);
    QCOMPARE(autoFillValueFor(QStringLiteral("address_line_2"), c), c.addressLine2);
    QCOMPARE(autoFillValueFor(QStringLiteral("apt"), c), c.addressLine2);
    QCOMPARE(autoFillValueFor(QStringLiteral("suite"), c), c.addressLine2);

    QCOMPARE(autoFillValueFor(QStringLiteral("street"), c), c.addressLine1);
    QCOMPARE(autoFillValueFor(QStringLiteral("address"), c), c.addressLine1);
    QCOMPARE(autoFillValueFor(QStringLiteral("address_line_1"), c), c.addressLine1);
}

void TestCards::storeRoundTripsAllFields() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("cards.toml"));

    CardStore writer(file);
    writer.addCard(sampleCard());
    MyCard second;
    second.label = QStringLiteral("Work");
    second.email = QStringLiteral("work@example.com");
    writer.addCard(std::move(second));
    writer.setActiveIndex(1);
    writer.save();

    CardStore reader(file);
    reader.load();
    QCOMPARE(reader.activeIndex(), 1);
    QCOMPARE(static_cast<int>(reader.cards().size()), 2);

    const MyCard& a = reader.cards()[0];
    const MyCard expected = sampleCard();
    QCOMPARE(a.label, expected.label);
    QCOMPARE(a.givenName, expected.givenName);
    QCOMPARE(a.familyName, expected.familyName);
    QCOMPARE(a.fullName, expected.fullName);
    QCOMPARE(a.email, expected.email);
    QCOMPARE(a.phone, expected.phone);
    QCOMPARE(a.organization, expected.organization);
    QCOMPARE(a.jobTitle, expected.jobTitle);
    QCOMPARE(a.addressLine1, expected.addressLine1);
    QCOMPARE(a.addressLine2, expected.addressLine2);
    QCOMPARE(a.city, expected.city);
    QCOMPARE(a.state, expected.state);
    QCOMPARE(a.postalCode, expected.postalCode);
    QCOMPARE(a.country, expected.country);

    QCOMPARE(reader.activeCard().email, QStringLiteral("work@example.com"));
}

void TestCards::storeLoadHandlesMissingFileSilently() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("missing.toml"));

    CardStore store(file);
    store.load();
    QVERIFY(store.cards().empty());
    QCOMPARE(store.activeIndex(), -1);
    QVERIFY(!store.hasActive());
}

void TestCards::storeRemoveClampsActiveIndex() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CardStore store(dir.filePath(QStringLiteral("cards.toml")));

    MyCard a; a.label = QStringLiteral("A"); store.addCard(std::move(a));
    MyCard b; b.label = QStringLiteral("B"); store.addCard(std::move(b));
    MyCard c; c.label = QStringLiteral("C"); store.addCard(std::move(c));
    store.setActiveIndex(2);

    store.removeCard(2);
    QCOMPARE(store.activeIndex(), 1);
    QCOMPARE(static_cast<int>(store.cards().size()), 2);

    store.removeCard(0);
    store.removeCard(0);
    QCOMPARE(store.activeIndex(), -1);
    QVERIFY(!store.hasActive());
}

void TestCards::fullNameFallsBackToGivenFamily() {
    MyCard c;
    c.givenName = QStringLiteral("Bob");
    c.familyName = QStringLiteral("Smith");
    QCOMPARE(c.displayFullName(), QStringLiteral("Bob Smith"));

    c.fullName = QStringLiteral("  Robert S. Smith  ");
    QCOMPARE(c.displayFullName(), QStringLiteral("Robert S. Smith"));
}

QTEST_MAIN(TestCards)
#include "test_cards.moc"
