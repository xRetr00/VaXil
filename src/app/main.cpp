#include <QApplication>
#include <QIcon>

#include "app/JarvisApplication.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setApplicationDisplayName(QStringLiteral("JARVIS"));
    QApplication::setApplicationName(QStringLiteral("JARVIS"));
    QApplication::setOrganizationName(QStringLiteral("JARVIS"));
    app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/JARVIS/gui/assets/icon.ico")));

    JarvisApplication jarvis;
    if (!jarvis.initialize()) {
        return 1;
    }

    return app.exec();
}
