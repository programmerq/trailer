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
    // Developer-only opt-in. Even recorder-enabled builds behave like
    // normal Trailer unless this flag is present at launch.
    const QCommandLineOption uxRecordOption(
        QStringLiteral("ux-record"),
        QStringLiteral("Record this run into a local UX session under the app data "
                       "directory (developer recorder builds; see docs/ux-recorder.md)."));
    parser.addOption(uxRecordOption);
#endif

    parser.process(arguments);

    CommandLineResult result;
    result.help = parser.isSet(helpOption);
    result.version = parser.isSet(versionOption);
    result.paths = parser.positionalArguments();
#ifdef TRAILER_UX_RECORDER
    result.uxRecord = parser.isSet(uxRecordOption);
#endif
    return result;
}

} // namespace trailer
