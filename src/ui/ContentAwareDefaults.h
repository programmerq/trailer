#pragma once

#include "Sidebar.h"

#include <optional>

namespace trailer {

// Content-aware first-open sidebar default.
//
// On the *first* open of a document for which the user has no saved
// per-file state, the document's own contents are a better signal than
// the global / per-type default:
//
//   - A long document (>= kLongDocumentPages pages) is one the user
//     will page through, so open the thumbnail sidebar for navigation.
//   - A shorter form (>= kFormFieldThreshold fillable AcroForm fields)
//     is for filling, not navigating, so force the sidebar hidden for a
//     clean view. The form-filling toolbar surfaces separately and is
//     unaffected by this.
//
// A long form counts as long: navigation wins, because the page-count
// check is evaluated first.
//
// Returns std::nullopt when neither heuristic applies — the caller then
// leaves the per-type / global default untouched. Any explicit per-file
// state the user has saved must be applied *instead* of consulting this,
// so an explicit choice always wins and sticks.
//
// Both thresholds are conservative *initial* values pending real-document
// validation; they are hand-picked guesses, not the output of a recorded
// tuning pass. Each carries a range and a symptom-to-change note:
//
//   kLongDocumentPages = 20 — "long enough that page-hunting starts to hurt."
//     Range: not empirically swept; 20 is a conservative first pick, set
//     deliberately below the live 50-page OCR-skip constant so a ~30-page
//     document still earns a thumbnail strip. Symptom to change: too eager
//     -> short docs get an unwanted thumbnail strip; too lax -> long docs
//     are left without navigation.
//   kFormFieldThreshold = 3 — "unambiguously a form, not one stray field."
//     Range: not empirically swept; 3 is a conservative first pick, set
//     above the live >=1 fill-enable trigger so a single-field PDF does not
//     count as a form. Symptom to change: too low -> a single-date-box PDF
//     hides the sidebar; too high -> real multi-field forms miss the clean
//     fill view.
//
// Both boundaries are inclusive (19 vs 20 pages, 2 vs 3 fields), pinned by
// exactBoundaries in tests/test_content_aware_defaults.cpp.
inline constexpr int kLongDocumentPages = 20;
inline constexpr int kFormFieldThreshold = 3;

inline std::optional<Sidebar::Mode>
contentAwareSidebarMode(int pageCount, bool supportsFormFilling, int formFieldCount) {
    if (pageCount >= kLongDocumentPages)
        return Sidebar::Mode::Pages;
    if (supportsFormFilling && formFieldCount >= kFormFieldThreshold)
        return Sidebar::Mode::Hidden;
    return std::nullopt;
}

} // namespace trailer
