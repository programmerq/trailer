#include "TwoPageView.h"

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

    const int hMax = std::max(0, static_cast<int>(std::ceil(canvas.width())) - vp.width());
    horizontalScrollBar()->setRange(0, hMax);
    horizontalScrollBar()->setPageStep(vp.width());
    horizontalScrollBar()->setSingleStep(std::max(1, vp.width() / 10));
}

void TwoPageView::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    relayout();
}

void TwoPageView::scrollToPage(int pageIndex) {
    if (m_spreads.empty())
        return;
    const int page1 = pageIndex + 1; // spreads use 1-based page numbers
    double y = kOuterMargin;
    for (const Spread &s : m_spreads) {
        if (s.left == page1 || s.right == page1) {
            verticalScrollBar()->setValue(std::min(
                static_cast<int>(y - kOuterMargin), verticalScrollBar()->maximum()));
            viewport()->update();
            return;
        }
        y += spreadHeight(s) + kSpreadGap;
    }
}

int TwoPageView::topVisibleLeadingPage() const {
    if (m_spreads.empty())
        return 0;
    // Spreads stack from kOuterMargin downward; the top-most visible spread is
    // the first whose bottom edge lies below the current scroll offset (i.e. it
    // still intersects the top of the viewport). scrollToPage top-aligns a
    // spread, so this resolves to that spread's leading page after navigation.
    const double scrollY = verticalScrollBar()->value();
    double y = kOuterMargin;
    for (const Spread &s : m_spreads) {
        const double sh = spreadHeight(s);
        if (y + sh > scrollY)
            return s.left - 1; // 1-based page -> 0-based index
        y += sh + kSpreadGap;
    }
    return m_spreads.back().left - 1;
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
    painter.fillRect(event->rect(), viewport()->palette().color(QPalette::Dark));
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
