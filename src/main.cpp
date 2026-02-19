#include <QApplication>
#include <QThread>

#include "ui/MainWindow.h"

/**
 * Application entry point.
 * @param[in] argc Argument count from the host process.
 * @param[in] argv Argument vector from the host process.
 * @return Process exit code.
 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QThread buildThread;
    buildThread.setObjectName("mesh-builder");
    buildThread.start();

    MainWindow window(&buildThread);
    window.show();

    const int exitCode = app.exec();

    buildThread.quit();
    buildThread.wait();

    return exitCode;
}
