#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QResource>
#include <QTextStream>

#include "app/JarvisApplication.h"

namespace {
QString bootstrapLogPath()
{
    const QString root = QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
    QDir().mkpath(root);
    return root + QStringLiteral("/startup.log");
}

void bootstrapLog(const QString &message)
{
    const QString line = QStringLiteral("[%1] %2")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODate), message);

    QTextStream err(stderr);
    err << line << Qt::endl;

    QFile file(bootstrapLogPath());
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << line << '\n';
    }
}

void qtMessageLogger(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    QString level = QStringLiteral("INFO");
    switch (type) {
    case QtDebugMsg:
        level = QStringLiteral("DEBUG");
        break;
    case QtInfoMsg:
        level = QStringLiteral("INFO");
        break;
    case QtWarningMsg:
        level = QStringLiteral("WARN");
        break;
    case QtCriticalMsg:
        level = QStringLiteral("ERROR");
        break;
    case QtFatalMsg:
        level = QStringLiteral("FATAL");
        break;
    }

    bootstrapLog(QStringLiteral("[%1] %2").arg(level, message));
    if (type == QtFatalMsg) {
        abort();
    }
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Q_INIT_RESOURCE(resources);
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setApplicationDisplayName(QStringLiteral("JARVIS"));
    QApplication::setApplicationName(QStringLiteral("JARVIS"));
    QApplication::setOrganizationName(QStringLiteral("JARVIS"));
    app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/JARVIS/gui/assets/icon.ico")));
    qInstallMessageHandler(qtMessageLogger);

    bootstrapLog(QStringLiteral("JARVIS bootstrap starting"));
    bootstrapLog(QStringLiteral("Executable directory: %1").arg(QCoreApplication::applicationDirPath()));

    JarvisApplication jarvis;
    if (!jarvis.initialize()) {
        bootstrapLog(QStringLiteral("Initialization failed. Check startup.log and jarvis.log."));
        return 1;
    }

    bootstrapLog(QStringLiteral("Initialization complete. App is running."));
    return app.exec();
}
