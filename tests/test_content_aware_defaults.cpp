// Unit test — content-aware first-open sidebar heuristic.
//
// Pins the pure decision in src/ui/ContentAwareDefaults.h across the
// full matrix: long documents, short forms, the long-form conflict
// (navigation wins), the below-threshold cases that must leave the
// per-type / global default in place, and the exact threshold
// boundaries. Keeping the decision in a pure function lets us verify
// every combination here without standing up a MainWindow or wrangling
// QPdfView's lazy layout — the wiring is covered separately by the
// integration UAT slots (uat_pdf_080 / uat_frm_060).

#include "ui/ContentAwareDefaults.h"
#include "ui/Sidebar.h"

#include <QObject>
#include <QtTest/QtTest>

#include <optional>

using namespace trailer;

namespace {
// True when the heuristic yields exactly `expected` for these inputs.
// `expected` accepts a bare Sidebar::Mode (implicitly wrapped) or
// std::nullopt for "leave the default".
bool modeIs(int pages, bool supportsForms, int fields, std::optional<Sidebar::Mode> expected) {
    return contentAwareSidebarMode(pages, supportsForms, fields) == expected;
}
} // namespace

class TestContentAwareDefaults : public QObject {
    Q_OBJECT
  private slots:
    void longDocumentOpensThumbnails();
    void shortFormHidesSidebar();
    void longFormPrefersThumbnails();
    void belowThresholdsLeaveDefault();
    void exactBoundaries();
};

void TestContentAwareDefaults::longDocumentOpensThumbnails() {
    // >= 20 pages and not a form: open the thumbnail sidebar.
    QVERIFY2(modeIs(20, false, 0, Sidebar::Mode::Pages), "20-page non-form -> Pages");
    QVERIFY2(modeIs(21, false, 0, Sidebar::Mode::Pages), "21-page non-form -> Pages");
    QVERIFY2(modeIs(500, false, 0, Sidebar::Mode::Pages), "500-page non-form -> Pages");
}

void TestContentAwareDefaults::shortFormHidesSidebar() {
    // < 20 pages with >= 3 fillable fields: force the sidebar hidden.
    QVERIFY2(modeIs(1, true, 3, Sidebar::Mode::Hidden), "1-page 3-field form -> Hidden");
    QVERIFY2(modeIs(5, true, 12, Sidebar::Mode::Hidden), "5-page 12-field form -> Hidden");
    QVERIFY2(modeIs(19, true, 3, Sidebar::Mode::Hidden), "19-page 3-field form -> Hidden");
}

void TestContentAwareDefaults::longFormPrefersThumbnails() {
    // A long form is both: navigation wins because the page check is
    // evaluated first (the user's chosen resolution).
    QVERIFY2(modeIs(20, true, 99, Sidebar::Mode::Pages), "20-page heavy form -> Pages (long wins)");
    QVERIFY2(modeIs(50, true, 3, Sidebar::Mode::Pages), "50-page form -> Pages (long wins)");
}

void TestContentAwareDefaults::belowThresholdsLeaveDefault() {
    // Neither long nor an unambiguous form: leave the per-type / global
    // default untouched (std::nullopt).
    QVERIFY2(modeIs(1, false, 0, std::nullopt), "1-page non-form -> default");
    QVERIFY2(modeIs(19, false, 0, std::nullopt), "19-page non-form -> default (just under long)");
    QVERIFY2(modeIs(3, true, 2, std::nullopt), "2-field form -> default (under field threshold)");
    QVERIFY2(modeIs(3, true, 0, std::nullopt), "form-capable but 0 fields -> default");
    QVERIFY2(modeIs(10, false, 99, std::nullopt), "field count ignored when forms unsupported");
}

void TestContentAwareDefaults::exactBoundaries() {
    // Page threshold is inclusive at 20.
    QVERIFY2(modeIs(19, false, 0, std::nullopt), "19 pages is not long");
    QVERIFY2(modeIs(20, false, 0, Sidebar::Mode::Pages), "20 pages is long");
    // Field threshold is inclusive at 3 (short docs so the page check
    // doesn't pre-empt it).
    QVERIFY2(modeIs(2, true, 2, std::nullopt), "2 fields is not a form");
    QVERIFY2(modeIs(2, true, 3, Sidebar::Mode::Hidden), "3 fields is a form");
}

QTEST_MAIN(TestContentAwareDefaults)
#include "test_content_aware_defaults.moc"
