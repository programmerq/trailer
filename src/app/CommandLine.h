#pragma once

#include <QStringList>

namespace trailer {

struct CommandLineResult {
    QStringList paths;
    bool help = false;
    bool version = false;
    // UX recorder launch controls. Both options exist only in builds
    // configured with TRAILER_ENABLE_UX_RECORDER; default builds reject
    // them as unknown options, so both fields stay false there.
    //
    // Recorder-enabled builds record EVERY launch by default (so the
    // app can be set as the default file handler and still capture
    // sessions opened from Finder, which pass no CLI args). The flags
    // are therefore mostly belt-and-suspenders:
    //   uxRecord    (--ux-record)    — explicit opt-in; redundant with
    //                                  the default, kept for parity and
    //                                  muscle memory.
    //   uxNoRecord  (--no-ux-record) — opt this single launch OUT of
    //                                  recording (wins over --ux-record).
    bool uxRecord = false;
    bool uxNoRecord = false;
};

CommandLineResult parseCommandLine(const QStringList &arguments);

} // namespace trailer
