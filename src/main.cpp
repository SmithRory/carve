#include <QApplication>
#include <QThread>

#include "ui/main_window.h"

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
