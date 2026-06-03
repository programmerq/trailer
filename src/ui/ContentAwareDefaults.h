#pragma once

#include "Sidebar.h"

#include <optional>

namespace trailer {

// Content-aware first-open sidebar default (roadmap Now #3).
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
// Thresholds come from the 2026-05-20 HITL pass; they are deliberately
// conservative so the heuristic only fires when a document is
// unambiguously long or unambiguously a form.
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
