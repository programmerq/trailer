#pragma once

#include <QStringList>

namespace trailer {

struct CommandLineResult {
    QStringList paths;
    bool help = false;
    bool version = false;
    // True when the user passed --ux-record. The option only exists in
    // builds configured with TRAILER_ENABLE_UX_RECORDER; in default
    // builds the parser rejects the flag as an unknown option, so this
    // field stays false there.
    bool uxRecord = false;
};

CommandLineResult parseCommandLine(const QStringList &arguments);

} // namespace trailer
