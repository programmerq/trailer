#include "document/SpreadLayout.h"

namespace trailer {

std::vector<Spread> spreadsFor(int pageCount, bool coverAlone) {
    std::vector<Spread> spreads;
    if (pageCount <= 0) {
        return spreads;
    }

    int next = 1; // next 1-based page still needing a slot

    // Cover-alone: page 1 occupies its own spread, then pairing begins at page 2.
    if (coverAlone) {
        spreads.push_back(Spread{1, 0});
        next = 2;
    }

    // Pair the remaining pages two at a time; a lone trailing page gets right == 0.
    while (next <= pageCount) {
        if (next + 1 <= pageCount) {
            spreads.push_back(Spread{next, next + 1});
            next += 2;
        } else {
            spreads.push_back(Spread{next, 0});
            next += 1;
        }
    }

    return spreads;
}

} // namespace trailer
