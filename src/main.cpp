#include "app/Application.h"
#include "app/CommandLine.h"
#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    trailer::Application app(argc, argv);

    const trailer::CommandLineResult cli =
        trailer::parseCommandLine(app.arguments());

    if (cli.paths.isEmpty()) {
        app.ensureWindow();
    } else {
        app.openFiles(cli.paths);
    }

    return app.exec();
}
