#include "CommandLine.h"

#include <QCommandLineOption>
#include <QCommandLineParser>

namespace trailer {

CommandLineResult parseCommandLine(const QStringList &arguments) {
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Trailer — cross-platform PDF and image workbench"));
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Files to open."),
                                 QStringLiteral("[files...]"));

    const QCommandLineOption helpOption = parser.addHelpOption();
    const QCommandLineOption versionOption = parser.addVersionOption();

#ifdef TRAILER_UX_RECORDER
    // Developer recorder builds record every launch by default (so the
    // app can be set as the default file handler and still capture
    // Finder-launched sessions, which carry no CLI args). --ux-record
    // is the explicit opt-in kept for parity; --no-ux-record opts a
    // single launch out. See docs/ux-recorder.md.
    const QCommandLineOption uxRecordOption(
        QStringLiteral("ux-record"),
        QStringLiteral("Record this run into a local UX session (on by default in "
                       "recorder builds; see docs/ux-recorder.md)."));
    parser.addOption(uxRecordOption);
    const QCommandLineOption uxNoRecordOption(
        QStringLiteral("no-ux-record"),
        QStringLiteral("Do not record this run, even though this is a recorder build."));
    parser.addOption(uxNoRecordOption);
#endif

    parser.process(arguments);

    CommandLineResult result;
    result.help = parser.isSet(helpOption);
    result.version = parser.isSet(versionOption);
    result.paths = parser.positionalArguments();
#ifdef TRAILER_UX_RECORDER
    result.uxRecord = parser.isSet(uxRecordOption);
    result.uxNoRecord = parser.isSet(uxNoRecordOption);
#endif
    return result;
}

} // namespace trailer
