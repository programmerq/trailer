#include "TwoPageView.h"

#include "util/DocumentSurroundColor.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QResizeEvent>
#include <QScrollBar>

#include <algorithm>
#include <cmath>
#include <limits>

namespace trailer {

namespace {
// Hand-tuned spread-layout metrics, in logical pixels. Range tried: gutter
// 8..24 (too tight looks like one wide page; too wide breaks the facing-page
// read); spread gap 16..40; outer margin 12..32. These read as facing pages
// with a clear centre seam and comfortable separation between spreads at the
// default zoom, matching Preview's "Two Pages" spacing. Change if spreads look
// merged (raise kPageGutter) or the scroll feels cramped (raise kSpreadGap).
constexpr double kPageGutter = 12.0; // between the two pages within a spread
constexpr double kSpreadGap = 24.0;  // vertical gap between stacked spreads
constexpr double kOuterMargin = 20.0; // canvas padding on all sides

// Clamp a widget devicePixelRatio to a sane positive value (mirrors the
// thumbnail path's guard against a null/unrealized 0 dpr).
qreal effectiveDpr(qreal dpr) { return dpr > 0.0 ? dpr : 1.0; }
} // namespace

TwoPageView::TwoPageView(QWidget *parent) : QAbstractScrollArea(parent) {
    // Stable selector for the UAT harness and any AT-SPI/QTest tier, so lookups
    // survive label / IA renames (matches the action-objectName convention).
    setObjectName(QStringLiteral("view.twoPage"));
    // Cosmetic only — paintEvent() below fills the exposed rect explicitly
    // on every repaint via documentSurroundColor(), which is the actual
    // source of truth. This backgroundRole just avoids a flash of the
    // style's default fill before the first paintEvent runs; the exact
    // static role doesn't matter since it's immediately painted over.
    viewport()->setBackgroundRole(QPalette::Dark);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // Repaint on scroll — the paint path reads the scrollbar values directly.
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(),
            qOverload<>(&QWidget::update));
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(),
            qOverload<>(&QWidget::update));
    // Track the visible spread as the user free-scrolls so the current-page
    // indicator (sidebar highlight) stays live in Two-Pages mode instead of
    // freezing on the first spread.
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this]() { maybeEmitCurrentPage(); });
}

void TwoPageView::setDocument(QPdfDocument *doc) {
    if (m_doc == doc)
        return;
    m_doc = doc;
    relayout();
    viewport()->update();
}

void TwoPageView::setCoverAlone(bool coverAlone) {
    if (m_coverAlone == coverAlone)
        return;
    m_coverAlone = coverAlone;
    relayout();
    viewport()->update();
}

void TwoPageView::setZoomFactor(double factor) {
    if (factor <= 0.0 || qFuzzyCompare(factor, m_zoom))
        return;
    m_zoom = factor;
    relayout();
    viewport()->update();
}

double TwoPageView::fitWidthZoom() const {
    if (!m_doc || m_spreads.empty())
        return m_zoom;
    // Fit the WIDEST spread to the viewport width, discounting the outer canvas
    // margins. The inter-page gutter is a fixed logical gap (not scaled), so it
    // is subtracted from the available width before solving for the zoom that
    // makes the two page widths fill the rest.
    const double availW = viewport()->width() - 2.0 * kOuterMargin;
    if (availW <= 0.0)
        return m_zoom;
    double z = std::numeric_limits<double>::max();
    for (const Spread &s : m_spreads) {
        const double p1w = m_doc->pagePointSize(s.left - 1).width();
        if (s.right != 0) {
            const double p2w = m_doc->pagePointSize(s.right - 1).width();
            const double pagesW = p1w + p2w;
            if (pagesW > 0.0)
                z = std::min(z, (availW - kPageGutter) / pagesW);
        } else if (p1w > 0.0) {
            z = std::min(z, availW / p1w);
        }
    }
    if (z == std::numeric_limits<double>::max() || z <= 0.0)
        return m_zoom;
    return z;
}

double TwoPageView::fitPageZoom() const {
    if (!m_doc || m_spreads.empty())
        return m_zoom;
    // Start from the width fit (no horizontal overflow for any spread), then
    // tighten so every spread's height also fits the viewport — the result makes
    // a whole spread visible in one screen without horizontal overflow.
    double z = fitWidthZoom();
    const double availH = viewport()->height() - 2.0 * kOuterMargin;
    if (availH <= 0.0)
        return z;
    for (const Spread &s : m_spreads) {
        double ph = m_doc->pagePointSize(s.left - 1).height();
        if (s.right != 0)
            ph = std::max(ph, m_doc->pagePointSize(s.right - 1).height());
        if (ph > 0.0)
            z = std::min(z, availH / ph);
    }
    if (z <= 0.0)
        return m_zoom;
    return z;
}

double TwoPageView::spreadHeight(const Spread &s) const {
    if (!m_doc)
        return 0.0;
    double h = m_doc->pagePointSize(s.left - 1).height() * m_zoom;
    if (s.right != 0)
        h = std::max(h, m_doc->pagePointSize(s.right - 1).height() * m_zoom);
    return h;
}

double TwoPageView::spreadWidth(const Spread &s) const {
    if (!m_doc)
        return 0.0;
    double w = m_doc->pagePointSize(s.left - 1).width() * m_zoom;
    if (s.right != 0)
        w += kPageGutter + m_doc->pagePointSize(s.right - 1).width() * m_zoom;
    return w;
}

QSizeF TwoPageView::canvasSize() const {
    double maxW = 0.0;
    double totalH = kOuterMargin;
    for (size_t i = 0; i < m_spreads.size(); ++i) {
        maxW = std::max(maxW, spreadWidth(m_spreads[i]));
        totalH += spreadHeight(m_spreads[i]);
        if (i + 1 < m_spreads.size())
            totalH += kSpreadGap;
    }
    totalH += kOuterMargin;
    return {maxW + 2 * kOuterMargin, totalH};
}

void TwoPageView::relayout() {
    m_spreads.clear();
    if (m_doc && m_doc->status() == QPdfDocument::Status::Ready) {
        m_spreads = spreadsFor(m_doc->pageCount(), m_coverAlone);
    }

    const QSizeF canvas = canvasSize();
    const QSize vp = viewport()->size();

    // Vertical range: how far past the viewport the canvas extends. pageStep is
    // a screenful (viewport height) so Space / PageDown advance sensibly; the
    // single step is a fraction of that for wheel/arrow nudges.
    const int vMax = std::max(0, static_cast<int>(std::ceil(canvas.height())) - vp.height());
    verticalScrollBar()->setRange(0, vMax);
    verticalScrollBar()->setPageStep(vp.height());
    verticalScrollBar()->setSingleStep(std::max(1, vp.height() / 10));

    // Floor (not ceil) the canvas width for the horizontal range: fitWidthZoom()
    // solves the widest spread to exactly the viewport width, but float round-up
    // in canvasSize() can leave the ceil'd width one pixel over, producing a 1px
    // scrollbar Fit-Width is meant to remove. Flooring absorbs that sub-pixel
    // excess; a genuine overflow of a full pixel or more still yields a range.
    const int hMax = std::max(0, static_cast<int>(std::floor(canvas.width())) - vp.width());
    horizontalScrollBar()->setRange(0, hMax);
    horizontalScrollBar()->setPageStep(vp.width());
    horizontalScrollBar()->setSingleStep(std::max(1, vp.width() / 10));
}

void TwoPageView::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    relayout();
}

void TwoPageView::scrollToPage(int pageIndex) {
    // Self-safe against a negative index regardless of caller guards: page1
    // would become <= 0 and could false-match the cover spread's right==0
    // sentinel (0 means "no page in this slot"), scrolling to the wrong place.
    if (pageIndex < 0 || m_spreads.empty())
        return;
    const int page1 = pageIndex + 1; // spreads use 1-based page numbers
    double y = kOuterMargin;
    for (const Spread &s : m_spreads) {
        if (s.left == page1 || s.right == page1) {
            // Round (not truncate) to the nearest logical pixel. A plain
            // static_cast<int> always rounds toward zero, which can lose up
            // to just under 1px of the target offset; topVisibleLeadingPage()
            // reconstructs the scrolled-to spread's top from this same value
            // with only a 0.5px epsilon (kProbeEpsilon), so a truncation-only
            // rounding here could silently under-shoot enough to make the
            // reconstruction land ONE SPREAD SHORT of the page just navigated
            // to (currentPage() reporting the previous spread right after a
            // successful scrollToPage — the real regression this fixes:
            // Cmd-3 into Two-Pages mode on a page deep in the document).
            const int target = static_cast<int>(std::lround(y - kOuterMargin));
            verticalScrollBar()->setValue(std::min(target, verticalScrollBar()->maximum()));
            viewport()->update();
            return;
        }
        y += spreadHeight(s) + kSpreadGap;
    }
}

int TwoPageView::leadingPageOfNextSpread(int fromPage) const {
    if (m_spreads.empty())
        return fromPage;
    const int page1 = fromPage + 1; // spreads use 1-based page numbers
    for (size_t i = 0; i < m_spreads.size(); ++i) {
        const Spread &s = m_spreads[i];
        if (s.left == page1 || s.right == page1) {
            const size_t next = std::min(i + 1, m_spreads.size() - 1);
            return m_spreads[next].left - 1; // 1-based -> 0-based index
        }
    }
    // fromPage is not in any spread (out of range) — clamp to the last spread.
    return m_spreads.back().left - 1;
}

int TwoPageView::leadingPageOfPrevSpread(int fromPage) const {
    if (m_spreads.empty())
        return fromPage;
    const int page1 = fromPage + 1;
    for (size_t i = 0; i < m_spreads.size(); ++i) {
        const Spread &s = m_spreads[i];
        if (s.left == page1 || s.right == page1) {
            const size_t prev = (i == 0) ? 0 : i - 1;
            return m_spreads[prev].left - 1;
        }
    }
    return m_spreads.front().left - 1;
}

int TwoPageView::topVisibleLeadingPage() const {
    if (m_spreads.empty())
        return 0;
    // Spreads stack from kOuterMargin downward. The "current" spread is the LAST
    // one whose TOP has reached or passed the viewport top — we advance to the
    // next spread only once THAT spread's top actually crosses the viewport top.
    //
    // Why not the vertical MIDPOINT (the prior rule): a spread taller than ~2x
    // the viewport is abandoned the instant the user scrolls past its midpoint,
    // even though its bottom half still fills the whole viewport and the next
    // spread has not appeared. That makes the current-page indicator lead by one
    // and lets Next Page skip a whole spread. A top-crossing rule keeps the
    // oversized spread current until its successor genuinely enters view.
    //
    // Why not a naive `top <= scrollY`: scrollToPage() top-aligns spread S by
    // setting scrollY = absoluteTop(S) - kOuterMargin, so absoluteTop(S) sits
    // kOuterMargin BELOW scrollY. Comparing against scrollY alone would report
    // S-1 right after a top-align. Probe the line scrollY + kOuterMargin instead
    // (with a small float epsilon for arithmetic slack), i.e. report the spread
    // whose vertical extent contains that line: the last spread whose
    // absoluteTop <= scrollY + kOuterMargin + epsilon. That keeps the top-align
    // correct (S reported after scrollToPage(S)) while fixing the oversized skip.
    constexpr double kProbeEpsilon = 0.5; // logical px of float slack
    const double probe = verticalScrollBar()->value() + kOuterMargin + kProbeEpsilon;
    double y = kOuterMargin;                    // absolute top of the current spread
    int leading = m_spreads.front().left - 1;   // last spread whose top <= probe
    for (const Spread &s : m_spreads) {
        if (y > probe)
            break; // this spread's top has not reached the viewport top yet
        leading = s.left - 1; // 1-based page -> 0-based index
        y += spreadHeight(s) + kSpreadGap;
    }
    return leading;
}

void TwoPageView::maybeEmitCurrentPage() {
    const int page = topVisibleLeadingPage();
    if (page == m_lastReportedPage)
        return; // emit only on change — no per-pixel churn / feedback churn
    m_lastReportedPage = page;
    emit currentPageChanged(page);
}

QImage TwoPageView::renderPageImage(int pageIndex) const {
    if (!m_doc || m_doc->status() != QPdfDocument::Status::Ready)
        return {};
    const QSizeF pts = m_doc->pagePointSize(pageIndex);
    if (pts.isEmpty())
        return {};
    const qreal dpr = effectiveDpr(viewport()->devicePixelRatioF());
    const double lw = pts.width() * m_zoom;
    const double lh = pts.height() * m_zoom;
    // Render at pts x zoom x dpr DEVICE pixels, then stamp dpr so the image
    // occupies lw x lh LOGICAL pixels — crisp at HiDPI, identical logical
    // geometry as 1x (record clause 4). Rendering at the logical size and
    // letting the painter upscale by dpr would blur on Retina — the exact
    // regression the dpr-matrix UAT guards.
    const QSize devPx(std::max(1, static_cast<int>(std::ceil(lw * dpr))),
                      std::max(1, static_cast<int>(std::ceil(lh * dpr))));
    QPdfDocumentRenderOptions opts;
    opts.setScaledSize(devPx);
    QImage img = m_doc->render(pageIndex, devPx, opts);
    if (!img.isNull())
        img.setDevicePixelRatio(dpr);
    return img;
}

void TwoPageView::paintEvent(QPaintEvent *event) {
    QPainter painter(viewport());
    // The canvas surrounding a spread that doesn't fill the viewport uses
    // documentSurroundColor() (util/DocumentSurroundColor.h) — the SAME
    // shared rule PdfDocument pins its QPdfView's ::Dark role to
    // (PdfAdapter.cpp, applyViewPalette), so the two PDF-shaped surfaces
    // can never independently drift apart per theme; see that header's
    // comment for the full rationale (prefers ::Dark — the recessed-canvas
    // convention every mainstream PDF viewer follows against a typically-
    // white page — but falls back to ::Base whenever ::Dark would resolve
    // LIGHTER than it, which is the reported "grey that's too light in
    // dark mode" bug: DR 2026-07-31-document-surround-colour-follows-base).
    // Read live off the palette every paint (no cached/pinned colour), so a
    // runtime theme flip (PR #105) needs no extra refresh plumbing here —
    // unlike QPdfView, which pins the role and must be told to re-derive it
    // (see PdfDocument::refreshViewPalette).
    painter.fillRect(event->rect(), documentSurroundColor(viewport()->palette()));
    if (!m_doc || m_spreads.empty() || m_doc->status() != QPdfDocument::Status::Ready)
        return;

    const QSizeF canvas = canvasSize();
    const int vpW = viewport()->width();

    // Horizontal origin: honour the scrollbar, but if the whole canvas fits the
    // viewport, centre it (a lone narrow spread shouldn't hug the left edge).
    double originX = -horizontalScrollBar()->value();
    if (canvas.width() <= vpW)
        originX = (vpW - canvas.width()) / 2.0;

    const double scrollY = verticalScrollBar()->value();
    double y = kOuterMargin - scrollY;

    for (const Spread &s : m_spreads) {
        const double sh = spreadHeight(s);
        const double sw = spreadWidth(s);
        // Cull spreads fully above or below the viewport.
        const bool visible = (y + sh) >= 0 && y <= viewport()->height();
        if (visible) {
            // Centre this spread within the canvas width.
            double x = originX + kOuterMargin + (canvas.width() - 2 * kOuterMargin - sw) / 2.0;

            auto drawPage = [&](int page1Based, double px, double py) {
                if (page1Based <= 0)
                    return;
                const int page = page1Based - 1;
                const QSizeF pts = m_doc->pagePointSize(page);
                if (pts.isEmpty())
                    return;
                const double lw = pts.width() * m_zoom;
                const double lh = pts.height() * m_zoom;
                // Render at pts x zoom x dpr device pixels (crisp at HiDPI) via
                // the shared helper that renderPageImage() / the dpr UAT also
                // read, so the render-target resolution cannot drift from the
                // tested invariant.
                const QImage img = renderPageImage(page);
                // A PDF page is opaque white; QPdfDocument::render returns an
                // image with a transparent background, so paint a white page
                // rectangle first, then composite the rendered content over it.
                // Without this the page shows as the (dark) viewport with only
                // the ink floating on it. The rect also serves as the honest
                // placeholder frame if the render failed.
                painter.fillRect(QRectF(px, py, lw, lh), Qt::white);
                if (img.isNull()) {
                    painter.setPen(Qt::gray);
                    painter.drawRect(QRectF(px, py, lw, lh));
                    return;
                }
                painter.drawImage(QPointF(px, py), img);
            };

            const double leftH = m_doc->pagePointSize(s.left - 1).height() * m_zoom;
            drawPage(s.left, x, y + (sh - leftH) / 2.0);
            if (s.right != 0) {
                const double leftW = m_doc->pagePointSize(s.left - 1).width() * m_zoom;
                const double rightH = m_doc->pagePointSize(s.right - 1).height() * m_zoom;
                drawPage(s.right, x + leftW + kPageGutter, y + (sh - rightH) / 2.0);
            }
        }
        y += sh + kSpreadGap;
        if (y - kSpreadGap > viewport()->height())
            break; // everything below is off-screen
    }
}

} // namespace trailer
