// generate_icon_sheet — renders every toolbar SVG via the same code
// path the running app uses (IconHelper::themedActionIcon) and lays
// them out in a grid PNG. Useful for spotting silhouette collisions,
// proportion problems, or ambiguities at the shipped 18 px size
// without launching the GUI. Run after editing any SVG to confirm
// it survives downscaling.
//
// Usage:
//   ./generate_icon_sheet <output.png>
//
// The sheet has rows labelled with the icon name; each row shows the
// icon at 18 px (shipped) and 36 px (HiDPI) against a light and dark
// background pair.

#include "ui/IconHelper.h"

#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QStringList>
#include <cstdio>

namespace {

struct IconEntry {
    const char* name;
    const char* resource;
    bool hasFilledVariant;
};

constexpr IconEntry kIcons[] = {
    {"tool-select",         ":/icons/actions/tool-select.svg",         false},
    {"tool-rectangle",      ":/icons/actions/tool-rectangle.svg",      true},
    {"tool-ellipse",        ":/icons/actions/tool-ellipse.svg",        true},
    {"tool-line",           ":/icons/actions/tool-line.svg",           true},
    {"tool-arrow",          ":/icons/actions/tool-arrow.svg",          true},
    {"tool-freehand",       ":/icons/actions/tool-freehand.svg",       false},
    {"tool-text",           ":/icons/actions/tool-text.svg",           false},
    {"tool-note",           ":/icons/actions/tool-note.svg",           true},
    {"tool-speech-bubble",  ":/icons/actions/tool-speech-bubble.svg",  true},
    {"tool-highlight-shape",":/icons/actions/tool-highlight-shape.svg",false},
    {"tool-zoom-lens",      ":/icons/actions/tool-zoom-lens.svg",      false},
    {"tool-highlight",      ":/icons/actions/tool-highlight.svg",      false},
    {"tool-underline",      ":/icons/actions/tool-underline.svg",      false},
    {"tool-strikeout",      ":/icons/actions/tool-strikeout.svg",      false},
    {"tool-redact",         ":/icons/actions/tool-redact.svg",         false},
    {"tool-autofill",       ":/icons/actions/tool-autofill.svg",       false},
    {"tool-sign-here",      ":/icons/actions/tool-sign-here.svg",      false},
    {"tool-checkmark",      ":/icons/actions/tool-checkmark.svg",      false},
    {"tool-xmark",          ":/icons/actions/tool-xmark.svg",          false},
    {"view-zoom-in",        ":/icons/actions/view-zoom-in.svg",        false},
    {"view-zoom-out",       ":/icons/actions/view-zoom-out.svg",       false},
    {"view-zoom-actual",    ":/icons/actions/view-zoom-actual.svg",    false},
    {"view-fit-page",       ":/icons/actions/view-fit-page.svg",       false},
    {"view-fit-width",      ":/icons/actions/view-fit-width.svg",      false},
    {"page-rotate-left",    ":/icons/actions/page-rotate-left.svg",    false},
    {"page-rotate-right",   ":/icons/actions/page-rotate-right.svg",   false},
    {"panel-sidebar",       ":/icons/actions/panel-sidebar.svg",       false},
    {"panel-markup",        ":/icons/actions/panel-markup.svg",        false},
    {"panel-form",          ":/icons/actions/panel-form.svg",          false},
};

// Layout constants. The sheet shows each icon in four "cells":
// (light/18px, light/36px, dark/18px, dark/36px) plus a filled-state
// pair for tools that have one. Generous padding so glyphs don't
// blend into neighbours.
constexpr int kRowHeight = 48;
constexpr int kLabelWidth = 180;
constexpr int kCellWidth = 60;
constexpr int kHeaderHeight = 32;

void paintIconAt(QPainter& painter, const QIcon& icon, int x, int y,
                 int size, const QColor& bg, QIcon::State state) {
    const int cellSize = size + 12;
    painter.fillRect(x, y, cellSize, cellSize, bg);
    const int offset = (cellSize - size) / 2;
    const QPixmap pm = icon.pixmap(QSize(size, size), QIcon::Normal, state);
    painter.drawPixmap(x + offset, y + offset, pm);
    // Cell border so empty pixmaps are obvious vs. background.
    painter.setPen(QColor(180, 180, 180));
    painter.drawRect(x, y, cellSize - 1, cellSize - 1);
}

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    // trailer_core is a static library; if nothing references the
    // auto-generated qInitResources_trailer symbol, the linker drops
    // it and our resource paths return "No such file or directory".
    // Force the init explicitly so this tool works without depending
    // on which other trailer_core .o files happen to be pulled in.
    Q_INIT_RESOURCE(trailer);

    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <output.png>\n", argv[0]);
        return 2;
    }
    const QString outPath = QString::fromUtf8(argv[1]);

    constexpr int rows = sizeof(kIcons) / sizeof(kIcons[0]);
    // Column layout per row:
    //   [label] [18 L] [36 L] [18 D] [36 D] [18 L on] [36 L on]
    // 7 cells. Filled-state cells are blank where there's no sibling.
    constexpr int totalWidth = kLabelWidth + 7 * kCellWidth;
    const int totalHeight = kHeaderHeight + rows * kRowHeight;

    QImage sheet(totalWidth, totalHeight, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(QColor(245, 245, 245));

    QPainter painter(&sheet);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Header
    QFont header = painter.font();
    header.setPointSize(10);
    header.setBold(true);
    painter.setFont(header);
    painter.setPen(Qt::black);
    painter.drawText(QRect(0, 0, kLabelWidth, kHeaderHeight),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("  Icon"));
    const QStringList headers = {
        QStringLiteral("18 light"), QStringLiteral("36 light"),
        QStringLiteral("18 dark"),  QStringLiteral("36 dark"),
        QStringLiteral("18 on"),    QStringLiteral("36 on"),
        QStringLiteral(""),
    };
    for (int c = 0; c < 6; ++c) {
        painter.drawText(QRect(kLabelWidth + c * kCellWidth, 0,
                               kCellWidth, kHeaderHeight),
                         Qt::AlignCenter, headers[c]);
    }

    QFont label = painter.font();
    label.setPointSize(10);
    label.setBold(false);
    label.setFamily(QStringLiteral("Menlo"));
    painter.setFont(label);

    const QColor lightBg(255, 255, 255);
    const QColor darkBg(40, 40, 40);
    const QColor lightTint(30, 30, 30);
    const QColor darkTint(220, 220, 220);

    for (int i = 0; i < rows; ++i) {
        const IconEntry& e = kIcons[i];
        const int y = kHeaderHeight + i * kRowHeight;

        painter.setPen(Qt::black);
        painter.drawText(QRect(0, y, kLabelWidth, kRowHeight),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         QStringLiteral("  ") + QString::fromUtf8(e.name));

        const QString resource = QString::fromUtf8(e.resource);
        const QIcon lightIcon = trailer::themedActionIcon(resource, lightTint);
        const QIcon darkIcon  = trailer::themedActionIcon(resource, darkTint);

        int col = kLabelWidth;
        paintIconAt(painter, lightIcon, col, y + 2, 18, lightBg, QIcon::Off);
        col += kCellWidth;
        paintIconAt(painter, lightIcon, col, y + 2, 36, lightBg, QIcon::Off);
        col += kCellWidth;
        paintIconAt(painter, darkIcon, col, y + 2, 18, darkBg, QIcon::Off);
        col += kCellWidth;
        paintIconAt(painter, darkIcon, col, y + 2, 36, darkBg, QIcon::Off);
        col += kCellWidth;
        if (e.hasFilledVariant) {
            paintIconAt(painter, lightIcon, col, y + 2, 18, lightBg, QIcon::On);
            col += kCellWidth;
            paintIconAt(painter, lightIcon, col, y + 2, 36, lightBg, QIcon::On);
        }
    }

    painter.end();
    if (!sheet.save(outPath)) {
        std::fprintf(stderr, "failed to write %s\n",
                     outPath.toLocal8Bit().constData());
        return 1;
    }
    std::printf("wrote %s (%d x %d)\n",
                outPath.toLocal8Bit().constData(),
                sheet.width(), sheet.height());
    return 0;
}
