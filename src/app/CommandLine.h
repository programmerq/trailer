#pragma once

#include <QStringList>

namespace trailer {

struct CommandLineResult {
    QStringList paths;
    bool help = false;
    bool version = false;
};

CommandLineResult parseCommandLine(const QStringList& arguments);

}  // namespace trailer
