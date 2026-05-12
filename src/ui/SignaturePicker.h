#pragma once

#include <QCoreApplication>
#include <QPoint>
#include <QString>

class QWidget;

namespace trailer {

// Popover-style signature picker. Replaces the modal SignaturesDialog
// for the "I want to sign here" flow (the manage / add flow still
// uses the full dialog). Pops a QMenu of saved signatures with
// thumbnail icons anchored under the toolbar button so the user
// doesn't get yanked into a modal in front of the document they're
// trying to sign.
//
// Menu layout (in order):
//   - one row per saved signature, label + small thumbnail
//   - separator
//   - "Add Signature…" — captures a new one and stamps it
//   - "Manage Signatures…" — opens the full dialog (shown only when
//     at least one signature exists)
//
// Returns the id of the signature the user picked (existing OR
// freshly-added via the Add flow). Empty string means cancelled or
// the user chose Manage and didn't pick a signature there.
class SignaturePicker {
    Q_DECLARE_TR_FUNCTIONS(SignaturePicker)
public:
    static QString show(QWidget* parent, const QPoint& globalPos);
};

}  // namespace trailer
