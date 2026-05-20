#include "SignaturePicker.h"

#include "SignatureCaptureDialog.h"
#include "SignaturesDialog.h"
#include "signatures/SignatureStore.h"

#include <QAction>
#include <QIcon>
#include <QLocale>
#include <QMenu>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QVariant>

namespace trailer {

namespace {

// The thumbnail strip rendered next to each menu entry. Tall enough
// to read at a glance (signatures are landscape), short enough not
// to dominate the popover.
constexpr int kThumbHeight = 36;
constexpr int kThumbMaxWidth = 96;

// Sentinel values stuffed in QAction::setData so we can tell which
// branch the user picked. Real-signature indices are non-negative.
constexpr int kSentinelAdd = -1;
constexpr int kSentinelManage = -2;

QIcon thumbnailIcon(const QString& pngPath) {
    QPixmap pm(pngPath);
    if (pm.isNull()) return QIcon();
    return QIcon(pm.scaled(QSize(kThumbMaxWidth, kThumbHeight),
                           Qt::KeepAspectRatio,
                           Qt::SmoothTransformation));
}

}  // namespace

QString SignaturePicker::show(QWidget* parent, const QPoint& globalPos) {
    SignatureStore store;
    const std::vector<Signature> sigs = store.loadAll();

    QMenu menu(parent);
    menu.setToolTipsVisible(true);
    // QMenu inherits icon size from the active style; we rely on the
    // thumbnail QPixmaps already being scaled to kThumbHeight in
    // thumbnailIcon() so the menu rows stay a sensible height.

    if (sigs.empty()) {
        QAction* empty = menu.addAction(tr("(No signatures yet)"));
        empty->setEnabled(false);
        menu.addSeparator();
    } else {
        for (size_t i = 0; i < sigs.size(); ++i) {
            const Signature& s = sigs[i];
            QAction* a = menu.addAction(thumbnailIcon(s.pngPath), s.label);
            // Carry the index in QAction::data so exec() returns
            // something we can map back to the signature without
            // a second store load.
            a->setData(static_cast<int>(i));
            // SignatureStore writes createdAt in UTC (see
            // SignatureStore.cpp's QDateTime::currentDateTimeUtc);
            // convert to local time before formatting so the tooltip
            // shows the user's wall-clock time, not the UTC clock.
            a->setToolTip(QLocale().toString(s.createdAt.toLocalTime(), QLocale::ShortFormat));
        }
        menu.addSeparator();
    }

    QAction* addAction = menu.addAction(tr("Add Signature…"));
    addAction->setData(kSentinelAdd);

    QAction* manageAction = nullptr;
    if (!sigs.empty()) {
        manageAction = menu.addAction(tr("Manage Signatures…"));
        manageAction->setData(kSentinelManage);
    }

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return QString();

    const int data = chosen->data().toInt();

    if (data == kSentinelAdd) {
        // Inline capture: when the user picks "Add…" we go straight
        // into the capture dialog and, if they finish it, save AND
        // stamp the new signature. Saves them a redundant
        // re-show-the-picker-and-click-the-new-one step.
        SignatureCaptureDialog dialog(parent);
        if (dialog.exec() != QDialog::Accepted) return QString();
        const QImage img = dialog.image();
        if (img.isNull()) return QString();
        const Signature added = store.add(img, dialog.label());
        return added.id;
    }

    if (data == kSentinelManage) {
        // Full management UI for add/remove/relabel flows. Returns
        // the user's selected id on Accept (the existing dialog
        // already exposes that via selectedId()).
        SignaturesDialog dialog(parent);
        if (dialog.exec() != QDialog::Accepted) return QString();
        return dialog.selectedId();
    }

    // Real signature row.
    if (data >= 0 && static_cast<size_t>(data) < sigs.size()) {
        return sigs[static_cast<size_t>(data)].id;
    }
    return QString();
}

}  // namespace trailer
