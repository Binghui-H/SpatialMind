#include "MainWindow.h"

#include <QApplication>
#include <QtWebEngineWidgets>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("SpatialMind");
    app.setWindowIcon(QIcon(":/icon.ico"));

    MainWindow w;
    w.show();
    return app.exec();
}
