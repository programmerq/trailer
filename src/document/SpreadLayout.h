#pragma once

#include <vector>

namespace trailer {

// One horizontal spread (row) in a two-page / facing-page layout: the 1-based
// page numbers shown side by side. `right == 0` is the sentinel for a spread
// that holds a single page in its left slot and nothing on the right — either
// the lone cover (cover-alone mode) or a trailing unpaired page. We use the 0
// sentinel rather than std::optional so the struct stays a trivial POD that a
// paint layer can copy freely; 0 is never a valid 1-based page number, so it is
// an unambiguous "no page here" marker.
struct Spread {
    int left;  // 1-based page number, always >= 1 for any spread returned.
    int right; // 1-based page number, or 0 meaning "single page in this spread".
};

inline bool operator==(const Spread &a, const Spread &b) {
    return a.left == b.left && a.right == b.right;
}
inline bool operator!=(const Spread &a, const Spread &b) { return !(a == b); }

// Pure page-pairing rule for facing-page (two-up) layout. GUI-free and
// side-effect-free so every pairing can be enumerated headlessly in unit tests;
// the TwoPageView paint layer (a later increment) consumes this to decide which
// pages share a row. See docs/decision-records/2026-07-21-two-page-layout.md.
//
// coverAlone == true  (book-like default): page 1 (the cover) renders alone,
//   then facing pairs {2,3}, {4,5}, ….
// coverAlone == false: pairs start at the first page: {1,2}, {3,4}, ….
//
// In either mode a trailing page with no partner is its own spread with
// right == 0. pageCount <= 0 returns an empty vector.
std::vector<Spread> spreadsFor(int pageCount, bool coverAlone);

} // namespace trailer
