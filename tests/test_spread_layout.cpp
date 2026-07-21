#include "document/SpreadLayout.h"

#include <QObject>
#include <QTest>

#include <vector>

using namespace trailer;

// Exhaustive unit tests for the pure facing-page pairing rule `spreadsFor`.
// The function is GUI-free (no Qt-UI deps), so every pairing for the small
// page counts that exercise all branches — empty, lone cover, exact pairs,
// trailing unpaired page — is pinned here headlessly. See
// docs/decision-records/2026-07-21-two-page-layout.md for the ratified rule.
class TestSpreadLayout : public QObject {
    Q_OBJECT

  private:
    // Helper so QCOMPARE reports the whole vector on mismatch.
    static void checkSpreads(int pageCount, bool coverAlone,
                             const std::vector<Spread> &expected) {
        const std::vector<Spread> actual = spreadsFor(pageCount, coverAlone);
        QCOMPARE(actual, expected);
    }

  private slots:
    // pageCount <= 0 is always empty, in either mode.
    void nonPositivePageCountIsEmpty() {
        QVERIFY(spreadsFor(0, true).empty());
        QVERIFY(spreadsFor(0, false).empty());
        QVERIFY(spreadsFor(-1, true).empty());
        QVERIFY(spreadsFor(-5, false).empty());
    }

    // Cover-alone ON (book-like default): page 1 renders alone, then facing
    // pairs {2,3},{4,5},…; a trailing unpaired page is its own {n,0} spread.
    void coverAloneEnumeration() {
        checkSpreads(0, true, {});
        checkSpreads(1, true, {{1, 0}});
        checkSpreads(2, true, {{1, 0}, {2, 0}});
        checkSpreads(3, true, {{1, 0}, {2, 3}});
        checkSpreads(4, true, {{1, 0}, {2, 3}, {4, 0}});
        checkSpreads(5, true, {{1, 0}, {2, 3}, {4, 5}});
        checkSpreads(6, true, {{1, 0}, {2, 3}, {4, 5}, {6, 0}});
        checkSpreads(7, true, {{1, 0}, {2, 3}, {4, 5}, {6, 7}});
    }

    // Cover-alone OFF: pairing starts at the first page {1,2},{3,4},…; a
    // trailing unpaired page is its own {n,0} spread.
    void coverPairedEnumeration() {
        checkSpreads(0, false, {});
        checkSpreads(1, false, {{1, 0}});
        checkSpreads(2, false, {{1, 2}});
        checkSpreads(3, false, {{1, 2}, {3, 0}});
        checkSpreads(4, false, {{1, 2}, {3, 4}});
        checkSpreads(5, false, {{1, 2}, {3, 4}, {5, 0}});
        checkSpreads(6, false, {{1, 2}, {3, 4}, {5, 6}});
        checkSpreads(7, false, {{1, 2}, {3, 4}, {5, 6}, {7, 0}});
    }

    // Structural invariants that must hold for every enumerated spread across
    // the whole sweep, independent of the exact pinned rows above: left is
    // always a real 1-based page, right is either 0 or left+1, no page is
    // dropped or duplicated, and pages appear in ascending order.
    void invariantsAcrossSweep() {
        for (bool coverAlone : {true, false}) {
            for (int n = 0; n <= 7; ++n) {
                const std::vector<Spread> spreads = spreadsFor(n, coverAlone);
                int expectedNext = 1;
                for (const Spread &s : spreads) {
                    QVERIFY(s.left >= 1);
                    QCOMPARE(s.left, expectedNext);
                    if (s.right == 0) {
                        expectedNext = s.left + 1;
                    } else {
                        QCOMPARE(s.right, s.left + 1);
                        expectedNext = s.right + 1;
                    }
                }
                // Every page from 1..n was placed exactly once, in order.
                QCOMPARE(expectedNext, n + 1);
            }
        }
    }
};

QTEST_MAIN(TestSpreadLayout)
#include "test_spread_layout.moc"
