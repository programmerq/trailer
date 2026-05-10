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

    parser.process(arguments);

    CommandLineResult result;
    result.help = parser.isSet(helpOption);
    result.version = parser.isSet(versionOption);
    result.paths = parser.positionalArguments();
    return result;
}

} // namespace trailer
