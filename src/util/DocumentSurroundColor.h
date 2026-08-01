#pragma once

#include <QColor>
#include <QPalette>

namespace trailer {

// The colour behind a page/image that doesn't fill the document view — the
// "canvas" or "letterbox". ONE shared rule, so no two call sites can
// independently drift apart per theme (the bug this fixes: PdfDocument's
// QPdfView and TwoPageView each separately read QPalette::Dark for this,
// while ImageDocument's QScrollArea reads QPalette::Base — DR
// 2026-07-31-document-surround-colour-follows-base).
//
// Prefers QPalette::Dark: a canvas that reads visibly RECESSED behind a
// page is the convention every mainstream PDF viewer follows (Preview,
// Acrobat, Chrome's built-in viewer) precisely because a page is typically
// white/paper-coloured — a canvas identical to the page (::Base, which is
// pure white in Trailer's stock light palette) makes the page boundary
// invisible. This is why ImageDocument is NOT switched to this helper: its
// content is rarely pure white, so ::Base already reads fine there, and the
// owner explicitly said the image behaviour is correct as shipped — this
// helper only touches the PDF-shaped surfaces that were reading the wrong
// role.
//
// Falls back to ::Base whenever ::Dark would resolve LIGHTER than ::Base —
// the exact symptom reported ("a grey that's too light in this dark
// mode"): Trailer's dark palette is synthesized entirely by
// QStyleHints::setColorScheme (no hand-built dark QPalette of our own), and
// in that synthesis ::Dark is a bevel/groove-shading role, not a
// page-backdrop one, so it is not guaranteed to stay darker than ::Base the
// way it reliably does in light mode. The fallback makes the canvas AT
// WORST match the page backdrop (never lighter than it), which is exactly
// what ImageDocument already shows — self-healing to "match the image
// viewer" precisely in the case that was broken, while leaving the
// already-correct light-mode canvas (Dark reads darker than Base there —
// verified: #9f9f9f vs #ffffff in Trailer's stock light palette) untouched.
//
// A plain QColor::lightness() (HSL, 0-255) comparison — no perceptual-
// colour-science precision needed, just "is Dark darker than Base or not."
inline QColor documentSurroundColor(const QPalette &pal) {
    const QColor dark = pal.color(QPalette::Dark);
    const QColor base = pal.color(QPalette::Base);
    return dark.lightness() > base.lightness() ? base : dark;
}

} // namespace trailer
