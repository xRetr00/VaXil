#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QQuickStyle>
#include <QResource>
#include <QTextStream>

#include "diagnostics/CrashDiagnosticsService.h"
#include "diagnostics/StartupMilestones.h"
#include "diagnostics/VaxilErrorCodes.h"
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
        CrashDiagnosticsService::instance().captureQtFatal(message);
        bootstrapLog(QStringLiteral("[FATAL][%1] Qt fatal message captured")
                         .arg(VaxilErrorCodes::forKey(VaxilErrorCodes::Key::CrashQtFatal)));
        abort();
    }
}
}

int main(int argc, char *argv[])
{
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    Q_INIT_RESOURCE(resources);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setApplicationDisplayName(QStringLiteral("Vaxil"));
    QApplication::setApplicationName(QStringLiteral("Vaxil AI Assistant"));
    QApplication::setApplicationVersion(QStringLiteral("dev"));
    QApplication::setOrganizationName(QStringLiteral("xRetro Labs"));
    app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/VAXIL/gui/assets/icon.ico")));

    CrashDiagnosticsConfig diagnosticsConfig;
    diagnosticsConfig.applicationName = QStringLiteral("vaxil");
    diagnosticsConfig.applicationVersion = QCoreApplication::applicationVersion();
    diagnosticsConfig.buildInfo = QStringLiteral("qt=%1").arg(QString::fromLatin1(QT_VERSION_STR));
    diagnosticsConfig.logsRootPath = QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
    diagnosticsConfig.breadcrumbCapacity = 220;
    CrashDiagnosticsService::instance().initialize(diagnosticsConfig);
    CrashDiagnosticsService::instance().installHandlers();

    qInstallMessageHandler(qtMessageLogger);
    CrashDiagnosticsService::instance().markStartupMilestone(StartupMilestones::startupBegin(),
                                                             QStringLiteral("bootstrap entry"));

    bootstrapLog(QStringLiteral("VAXIL bootstrap starting"));
    bootstrapLog(QStringLiteral("Executable directory: %1").arg(QCoreApplication::applicationDirPath()));

    JarvisApplication jarvis;
    if (!jarvis.initialize()) {
        CrashDiagnosticsService::instance().markStartupMilestone(
            StartupMilestones::startupCompleted(),
            QStringLiteral("initialize=false"),
            false);
        bootstrapLog(QStringLiteral("[ERROR][%1] Initialization failed")
                         .arg(VaxilErrorCodes::forKey(VaxilErrorCodes::Key::StartupInitializationFailed)));
        bootstrapLog(QStringLiteral("Initialization failed. Check startup.log and vaxil.log."));
        return 1;
    }

    CrashDiagnosticsService::instance().markStartupMilestone(StartupMilestones::startupCompleted(),
                                                             QStringLiteral("initialize=true"),
                                                             true);
    bootstrapLog(QStringLiteral("Initialization complete. Waiting for service readiness."));

    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        CrashDiagnosticsService::instance().markStartupMilestone(StartupMilestones::shutdownBegin(),
                                                                 QStringLiteral("aboutToQuit"),
                                                                 true);
        CrashDiagnosticsService::instance().markStartupMilestone(StartupMilestones::shutdownCompleted(),
                                                                 QStringLiteral("aboutToQuit"),
                                                                 true);
    });

    return app.exec();
}
