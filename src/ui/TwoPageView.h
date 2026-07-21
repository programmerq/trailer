#pragma once

#include "document/SpreadLayout.h"

#include <QAbstractScrollArea>
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
    void setCoverAlone(bool coverAlone);
    bool coverAlone() const { return m_coverAlone; }

    // Per-page zoom factor. 1.0 == Actual Size (1 pt -> 1 logical px). Shared
    // with QPdfView so the mode-stable zoom-% readout stays truthful.
    void setZoomFactor(double factor);
    double zoomFactor() const { return m_zoom; }

    // The spreads currently laid out (for tests / callers that need the
    // pairing without re-deriving it).
    const std::vector<Spread> &spreads() const { return m_spreads; }

  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    // Recompute the spread list and the scrollbar ranges from the current
    // document, cover-alone setting, zoom, and viewport size.
    void relayout();
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
};

} // namespace trailer
