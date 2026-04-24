#pragma once

#include <QString>
#include <vector>

namespace trailer {

// One "My Card" — the personal details Trailer uses to AutoFill PDF
// form fields. Corresponds to the spec in DESIGN.md §6.4.2.
//
// All fields are free-form strings; empty means "leave blank / don't
// fill". The `label` is purely for the picker UI ("Personal" / "Work").
struct MyCard {
    QString label;

    QString givenName;
    QString familyName;

    // Full name as one string. If empty, callers should synthesise it
    // from givenName + familyName.
    QString fullName;

    QString email;
    QString phone;

    QString organization;   // Company / employer / school
    QString jobTitle;

    QString addressLine1;   // Street and number (or "Line 1")
    QString addressLine2;   // Apt / suite / PO box
    QString city;
    QString state;          // State / province / region
    QString postalCode;     // ZIP / postcode
    QString country;

    // Full name, synthesised from fullName or given+family. Never
    // returns a bare space; trims properly.
    QString displayFullName() const;
};

// Heuristic field-name → MyCard-field lookup. Given the PDF form
// field's internal name (e.g. "fullname", "Email_1"), return the
// autofill value from `card` — or an empty string if there's no
// confident mapping. The matcher is case-insensitive and tolerates
// underscores / hyphens / spaces between words.
QString autoFillValueFor(const QString& fieldName, const MyCard& card);

class IDocument;  // fwd
// Apply `card` to every text field in `doc` whose name the matcher
// recognises. Returns {filled, examined} so callers can surface a
// "filled N of M" message. Skips non-text fields and fields whose
// name has no match. Returns {0, 0} if `doc` is null or doesn't
// support form filling — never crashes.
struct AutoFillResult { int filled = 0; int examined = 0; };
AutoFillResult autoFillDocument(IDocument* doc, const MyCard& card);

}  // namespace trailer
