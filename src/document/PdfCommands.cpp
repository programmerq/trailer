#include "PdfCommands.h"

#include "PdfEditor.h"

#include <QObject>

namespace trailer {

RotatePageCommand::RotatePageCommand(int pageIndex, int degreesClockwise)
    : m_pageIndex(pageIndex), m_degrees(degreesClockwise) {}

bool RotatePageCommand::apply(PdfEditor &editor) {
    // PdfEditor::rotatePage is void — it tolerates out-of-range
    // indices by silently doing nothing. The PdfDocument layer
    // pre-validates so we just call through and report success.
    editor.rotatePage(m_pageIndex, m_degrees);
    return true;
}

bool RotatePageCommand::revert(PdfEditor &editor) {
    editor.rotatePage(m_pageIndex, -m_degrees);
    return true;
}

QString RotatePageCommand::description() const {
    return QObject::tr("Rotate Page");
}

} // namespace trailer
