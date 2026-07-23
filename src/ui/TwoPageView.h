#pragma once

#include "document/SpreadLayout.h"

#include <QAbstractScrollArea>
#include <QImage>
#include <QPointer>
#include <vector>

class QPdfDocument;

namespace trailer {

// Custom two-up (facing-page) surface for Two-Pages view mode. Renders a
// CONTINUOUS vertical stack of facing spreads (Apple Preview "Two Pages"
// shape), not a paged single-spread widget — the scroll model ratified in
// docs/decision-records/2026-07-21-two-page-layout.md (D3-A). It is used ONLY
// in Two-Pages mode; QPdfView keeps driving Single and Continuous (D1-A,
// AUGMENT). Markup / selection / search are honestly degraded (disabled-with-
// tooltip in MainWindow) in this mode; overlay/search/selection parity is the
// committed PR2 follow-up (docs/backlog/2026-07-21-two-page-overlay-search-parity).
//
// Pairing comes from the pure trailer::spreadsFor() helper. Each page is
// rendered at pts x zoom x devicePixelRatio device pixels and laid out at
// pts x zoom logical units, so a HiDPI display shows a crisp spread with the
// same logical geometry as a 1x display (record clause 4). "Actual Size" is
// zoom factor 1.0 == 1 PDF point -> 1 logical pixel per page (clause 3); the
// zoom factor is shared with QPdfView so the zoom-% readout is truthful across
// all three modes.
class TwoPageView : public QAbstractScrollArea {
    Q_OBJECT

  public:
    explicit TwoPageView(QWidget *parent = nullptr);

    // The QPdfDocument to render. Not owned; must outlive this view.
    void setDocument(QPdfDocument *doc);

    // Cover-alone pairing: page 1 alone, then (2,3),(4,5)… Default ON (the
    // book-like default ratified for PR1; no user-facing toggle yet).
    // NOTE: setCoverAlone()/coverAlone()/spreads() are intentionally unused in
    // PR1 — the record scopes the user-facing cover-alone toggle out of PR1.
    // They are the thin seam PR2 consumes when it wires that toggle and
    // reprojects overlays onto the spread geometry; kept minimal so the seam
    // costs nothing. Remove them if PR2's shape ends up differing.
    void setCoverAlone(bool coverAlone);
    bool coverAlone() const { return m_coverAlone; }

    // Per-page zoom factor. 1.0 == Actual Size (1 pt -> 1 logical px). Shared
    // with QPdfView so the mode-stable zoom-% readout stays truthful.
    void setZoomFactor(double factor);
    double zoomFactor() const { return m_zoom; }

    // Spread-aware fit zoom factors. Fit-Width returns the zoom at which the
    // WIDEST spread (page1 + gutter + page2) fits the viewport width; Fit-Page
    // additionally requires every spread to fit the viewport height, so one
    // spread is fully visible without horizontal overflow. Unlike QPdfView's
    // per-page fit these account for both facing pages + the gutter, so a spread
    // never overflows the viewport horizontally in Two-Pages mode. The caller
    // applies the returned factor through the shared zoom path so the zoom-%
    // readout stays truthful. Returns the current zoom unchanged if the layout
    // or viewport isn't ready.
    double fitWidthZoom() const;
    double fitPageZoom() const;

    // The spreads currently laid out (for tests / callers that need the
    // pairing without re-deriving it).
    const std::vector<Spread> &spreads() const { return m_spreads; }

    // Spread-adjacency navigation for Previous/Next Page in Two-Pages mode.
    // Given the 0-based `fromPage` that Next/Previous is relative to (any page
    // of the currently-visible spread — the caller passes the free-scroll-
    // tracked leading page), return the 0-based LEADING page of the adjacent
    // spread, clamped at the ends. Derived from the real SpreadLayout, so
    // cover-alone / trailing-unpaired pages are handled by the pairing rule
    // rather than hardcoded arithmetic. Next/Previous MUST step by a whole
    // spread: stepping by a single page maps the right page of a spread back to
    // that same spread (scrollToPage top-aligns the containing spread), so
    // per-page stepping sticks and never advances — the reason these exist.
    // Returns `fromPage` unchanged when there is no layout.
    int leadingPageOfNextSpread(int fromPage) const;
    int leadingPageOfPrevSpread(int fromPage) const;

    // Render 0-based `pageIndex` exactly as the paint path does: at
    // pts x zoom x devicePixelRatio DEVICE pixels, with the dpr stamped so the
    // image occupies pts x zoom LOGICAL pixels. paintEvent() draws this same
    // image, so the two cannot drift. Exposed so a HiDPI test can assert the
    // render target's TRUE pixel resolution scales with the device-pixel ratio
    // — the invariant a "render at 1x then upscale" regression (blur on Retina)
    // would break while leaving the logical geometry unchanged. Returns a null
    // image when the document/page is not renderable.
    QImage renderPageImage(int pageIndex) const;

    // Recompute spreads + scroll ranges from the current document. Public so the
    // adapter can call it after an in-place document reload (rotate / delete /
    // insert / move / crop / revert) changes the page count or page sizes —
    // otherwise the cached spreads would keep drawing the pre-edit layout.
    void relayout();

  public slots:
    // Scroll the spread that contains 1-based-friendly 0-based `pageIndex` into
    // view (top-aligned). Lets Previous/Next Page and thumbnail-click navigation
    // stay live in Two-Pages mode instead of only moving the hidden QPdfView.
    void scrollToPage(int pageIndex);

  signals:
    // Emitted (only on change) with the 0-based leading page of the top-most
    // visible spread as the user free-scrolls. Lets the current-page indicator
    // (sidebar highlight) track the scroll position in Two-Pages mode instead of
    // freezing on the first spread — the QPdfView navigator can't see this
    // custom surface's scrolling. Consumers must NOT scroll this view back in
    // response (feedback loop); they update indicator state only.
    void currentPageChanged(int leadingPageIndex);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    // 0-based leading page of the "current" spread: the last spread whose top
    // has reached or passed the viewport top (so an oversized spread stays
    // current while its lower half fills the viewport, rather than being
    // abandoned at its midpoint). Drives the currentPageChanged() signal.
    int topVisibleLeadingPage() const;
    // Recompute the top-most visible spread and emit currentPageChanged() if it
    // changed since the last emit. Called on every vertical-scroll tick.
    void maybeEmitCurrentPage();

    // Logical height of spread row `i` at the current zoom (max of its pages).
    double spreadHeight(const Spread &s) const;
    // Logical width of spread row `i` at the current zoom (both pages + gutter).
    double spreadWidth(const Spread &s) const;
    // Full logical canvas size (all spreads stacked + outer margins).
    QSizeF canvasSize() const;

    QPointer<QPdfDocument> m_doc;
    bool m_coverAlone = true;
    double m_zoom = 1.0;
    std::vector<Spread> m_spreads;
    // Last leading page reported via currentPageChanged(); -1 forces the first
    // computed value to emit. Guards against per-pixel signal churn.
    int m_lastReportedPage = -1;
};

} // namespace trailer
