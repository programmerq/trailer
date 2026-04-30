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
        app.ensureWindow();
    } else {
        app.openFiles(cli.paths);
    }

    return app.exec();
}
