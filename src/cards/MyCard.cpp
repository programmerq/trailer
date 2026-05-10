#include "MyCard.h"

#include "document/IDocument.h"
#include "document/PdfEditor.h" // FormField, FormFieldType

#include <QRegularExpression>

namespace trailer {

QString MyCard::displayFullName() const {
    if (!fullName.trimmed().isEmpty())
        return fullName.trimmed();
    QString out;
    if (!givenName.isEmpty())
        out = givenName;
    if (!familyName.isEmpty()) {
        if (!out.isEmpty())
            out += QLatin1Char(' ');
        out += familyName;
    }
    return out.trimmed();
}

namespace {

// Normalise a field name to lowercase with separators collapsed to
// single spaces. This lets the matcher treat "FullName", "full_name",
// and "FULL-NAME" as identical keys.
//
// The implementation also inserts a space at camelCase transitions
// BEFORE lowercasing so names like "EmailAddress", "phoneNumber", or
// "firstName" decompose into their constituent words. Sequences of
// capitals ("XMLParser") collapse to a single token — good enough for
// the field names we actually see on real PDFs.
QString canonicalise(const QString &raw) {
    QString spaced;
    spaced.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (i > 0 && c.isUpper() && raw.at(i - 1).isLower()) {
            spaced.append(QLatin1Char(' '));
        }
        spaced.append(c);
    }
    QString s = spaced.toLower();
    // Common PDF hierarchy separators include "." (fully qualified
    // AcroForm names like form1.BillingInfo.FirstName), along with
    // whitespace and the usual word separators. Collapse them all to
    // a single space so containsWord() can anchor on " word " pads.
    static const QRegularExpression sep(QStringLiteral("[\\s_\\-/.]+"));
    s.replace(sep, QStringLiteral(" "));
    return s.trimmed();
}

// Returns true if any of `needles` appears as a whole word in `hay`.
// Matching is substring-based but anchored on word boundaries so
// "address" doesn't accidentally match "addressee_name" semantics
// inappropriately (we want it to match, but we also want "state" to
// not match "statement").
bool containsWord(const QString &hay, std::initializer_list<QLatin1String> needles) {
    for (const auto n : needles) {
        // Word-boundary match on canonicalised input: the separator
        // regex already collapses non-alnum runs into single spaces,
        // so we can check for " needle ", "needle ", " needle",
        // or an exact equality.
        const QString pad = QLatin1Char(' ') + hay + QLatin1Char(' ');
        const QString needlePad = QLatin1Char(' ') + QString(n) + QLatin1Char(' ');
        if (pad.contains(needlePad))
            return true;
    }
    return false;
}

} // namespace

QString autoFillValueFor(const QString &fieldName, const MyCard &card) {
    const QString key = canonicalise(fieldName);
    if (key.isEmpty())
        return {};

    // Ordering matters: check the most specific keys first. "address
    // line 2" must be tested before "address".

    // Name
    if (containsWord(key, {QLatin1String("full name"), QLatin1String("fullname")}) ||
        key == QLatin1String("name")) {
        return card.displayFullName();
    }
    if (containsWord(key, {QLatin1String("first name"), QLatin1String("firstname"),
                           QLatin1String("given name"), QLatin1String("given"),
                           QLatin1String("forename")})) {
        return card.givenName;
    }
    if (containsWord(key, {QLatin1String("last name"), QLatin1String("lastname"),
                           QLatin1String("family name"), QLatin1String("surname")})) {
        return card.familyName;
    }

    // Contact
    if (containsWord(key, {QLatin1String("email"), QLatin1String("e mail"),
                           QLatin1String("email address")})) {
        return card.email;
    }
    if (containsWord(key,
                     {QLatin1String("phone"), QLatin1String("telephone"), QLatin1String("mobile"),
                      QLatin1String("cell"), QLatin1String("phone number")})) {
        return card.phone;
    }

    // Address — line 2 first to avoid "address" swallowing it.
    if (containsWord(key, {QLatin1String("address 2"), QLatin1String("address line 2"),
                           QLatin1String("apt"), QLatin1String("apartment"), QLatin1String("suite"),
                           QLatin1String("unit"), QLatin1String("po box")})) {
        return card.addressLine2;
    }
    if (containsWord(key, {QLatin1String("address"), QLatin1String("address 1"),
                           QLatin1String("address line 1"), QLatin1String("street"),
                           QLatin1String("street address")})) {
        return card.addressLine1;
    }
    if (containsWord(key,
                     {QLatin1String("city"), QLatin1String("town"), QLatin1String("locality")})) {
        return card.city;
    }
    if (containsWord(
            key, {QLatin1String("state"), QLatin1String("province"), QLatin1String("region")})) {
        return card.state;
    }
    if (containsWord(key, {QLatin1String("zip"), QLatin1String("zipcode"),
                           QLatin1String("zip code"), QLatin1String("postal"),
                           QLatin1String("postal code"), QLatin1String("postcode")})) {
        return card.postalCode;
    }
    if (containsWord(key, {QLatin1String("country")})) {
        return card.country;
    }

    // Organization
    if (containsWord(key, {QLatin1String("company"), QLatin1String("organization"),
                           QLatin1String("organisation"), QLatin1String("employer"),
                           QLatin1String("business")})) {
        return card.organization;
    }
    if (containsWord(key,
                     {QLatin1String("job title"), QLatin1String("title"), QLatin1String("position"),
                      QLatin1String("role"), QLatin1String("occupation")})) {
        return card.jobTitle;
    }

    return {};
}

AutoFillResult autoFillDocument(IDocument *doc, const MyCard &card) {
    AutoFillResult r;
    if (!doc || !doc->supportsFormFilling())
        return r;
    const auto fields = doc->formFields();
    for (const auto &f : fields) {
        if (f.type != FormFieldType::Text)
            continue;
        r.examined++;
        // Try the fully-qualified /T name first; many real-world PDFs
        // use an opaque hierarchical /T (e.g. "form1[0].#subform[0]
        // .FirstName[0]") and expose the human-readable label via /TU
        // ("First Name"). Fall back to /TU so both conventions work.
        QString v = autoFillValueFor(f.name, card);
        if (v.isEmpty() && !f.label.isEmpty()) {
            v = autoFillValueFor(f.label, card);
        }
        if (v.isEmpty())
            continue;
        if (doc->setFormFieldValue(f.id, v))
            r.filled++;
    }
    return r;
}

} // namespace trailer
