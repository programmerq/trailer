#include "TwoPageView.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QResizeEvent>
#include <QScrollBar>

#include <algorithm>
#include <cmath>

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

void TwoPageView::paintEvent(QPaintEvent *event) {
    QPainter painter(viewport());
    painter.fillRect(event->rect(), viewport()->palette().color(QPalette::Dark));
    if (!m_doc || m_spreads.empty() || m_doc->status() != QPdfDocument::Status::Ready)
        return;

    const qreal dpr = effectiveDpr(viewport()->devicePixelRatioF());
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
                // Render at pts x zoom x dpr device pixels, then stamp dpr so
                // the image occupies lw x lh LOGICAL pixels — crisp at HiDPI,
                // identical logical geometry as 1x (record clause 4).
                const QSize devPx(std::max(1, static_cast<int>(std::ceil(lw * dpr))),
                                  std::max(1, static_cast<int>(std::ceil(lh * dpr))));
                QPdfDocumentRenderOptions opts;
                opts.setScaledSize(devPx);
                QImage img = m_doc->render(page, devPx, opts);
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
                img.setDevicePixelRatio(dpr);
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
