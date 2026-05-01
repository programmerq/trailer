#include "app/Application.h"
#include "app/CommandLine.h"
#include "ui/MainWindow.h"

#include <QIcon>

int main(int argc, char* argv[]) {
    trailer::Application app(argc, argv);

    QIcon icon;
    for (int size : {16, 32, 64, 128, 256, 512, 1024}) {
        icon.addFile(QString(":/icons/trailer_%1.png").arg(size));
    }
    QApplication::setWindowIcon(icon);

    const trailer::CommandLineResult cli =
        trailer::parseCommandLine(app.arguments());

    if (cli.paths.isEmpty()) {
#ifdef Q_OS_MACOS
        // Mac convention: launching with no document opens no
        // window. The Dock icon and menu bar stay live; the user
        // picks File → Open or drops a file on the Dock to make a
        // window. Matches Preview / TextEdit / Pages.
        //
        // Without this branch, ensureWindow() pops a blank canvas
        // that the user has to manually close even if they
        // double-clicked the Dock icon by accident.
#else
        app.ensureWindow();
#endif
    } else {
        app.openFiles(cli.paths);
    }

    return app.exec();
}
