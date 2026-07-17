#include "uxrecord/UxRecord.h"

#include "uxrecord/UxRecorder.h"

namespace trailer {
namespace uxrecord {

// recorder() lives in UxRecorder.cpp next to the atomic it reads.

bool isActive() {
    UxRecorder *r = recorder();
    return r && r->isRecording();
}

void recordEvent(const QString &type, const QJsonObject &data) {
    if (UxRecorder *r = recorder()) {
        r->recordTrailerEvent(type, data);
    }
}

// attachToMainWindow(MainWindow *) is implemented in
// UxTrailerHooks.cpp, which is where all the MainWindow-facing
// includes live.

} // namespace uxrecord
} // namespace trailer
