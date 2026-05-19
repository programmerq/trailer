#include "app/Application.h"
#include "app/CommandLine.h"
#include "ui/MainWindow.h"

#include <QIcon>

int main(int argc, char *argv[]) {
    trailer::Application app(argc, argv);

#ifndef Q_OS_MACOS
    // On macOS the Dock icon is set by the system from the .app
    // bundle's CFBundleIconFile (Resources/trailer.icns), which
    // carries every standard icon size (ic04 through ic10). Calling
    // setWindowIcon here additionally pushes a QIcon into the running
    // app's NSDockTile via Qt's QIcon→NSImage bridge — and that
    // conversion picks a single resolution, which the Dock then
    // upscales for retina. The result is a pixelated Dock icon while
    // the app is running; quitting reveals the proper .icns variant
    // briefly during the genie animation. Skip the override on macOS
    // so the bundled .icns is authoritative.
    //
    // Other platforms still want the multi-resolution QIcon — window
    // titlebar icons (Linux), Alt-Tab thumbnails, taskbar icons
    // (Windows), etc.
    QIcon icon;
    for (int size : {16, 32, 64, 128, 256, 512, 1024}) {
        icon.addFile(QString(":/icons/trailer_%1.png").arg(size));
    }
    QApplication::setWindowIcon(icon);
#endif

    const trailer::CommandLineResult cli = trailer::parseCommandLine(app.arguments());

    if (cli.paths.isEmpty()) {
        // Workstream I: "quit and keep windows" across every platform.
        // If the user had files open at the last quit AND opted in to
        // session restore (default: on), reopen them now. Explicit CLI
        // args always win — they're a direct request that overrides the
        // persisted session.
        const bool restored = app.restorePreviousSession();
#ifdef Q_OS_MACOS
        // Mac convention: launching with no document opens no
        // window. The Dock icon and menu bar stay live; the user
        // picks File → Open or drops a file on the Dock to make a
        // window. Matches Preview / TextEdit / Pages.
        //
        // Without this branch, ensureWindow() pops a blank canvas
        // that the user has to manually close even if they
        // double-clicked the Dock icon by accident. Session-restore
        // (above) takes precedence — if a session was restored, the
        // restored windows are what the user sees.
        (void)restored;
#else
        if (!restored) {
            app.ensureWindow();
        }
#endif
    } else {
        app.openFiles(cli.paths);
    }

    return app.exec();
}
