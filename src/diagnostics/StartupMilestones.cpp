#include "StartupMilestones.h"

namespace StartupMilestones {

QString startupBegin() { return QStringLiteral("startup.begin"); }
QString startupLoggingReady() { return QStringLiteral("startup.logging.ready"); }
QString startupOverlayBegin() { return QStringLiteral("startup.overlay.begin"); }
QString startupOverlayOk() { return QStringLiteral("startup.overlay.ok"); }
QString startupOverlayFail() { return QStringLiteral("startup.overlay.fail"); }
QString startupTtsBegin() { return QStringLiteral("startup.tts.begin"); }
QString startupTtsOk() { return QStringLiteral("startup.tts.ok"); }
QString startupTtsFail() { return QStringLiteral("startup.tts.fail"); }
QString startupWakeBegin() { return QStringLiteral("startup.wake.begin"); }
QString startupWakeOk() { return QStringLiteral("startup.wake.ok"); }
QString startupWakeFail() { return QStringLiteral("startup.wake.fail"); }
QString startupCompleted() { return QStringLiteral("startup.completed"); }
QString shutdownBegin() { return QStringLiteral("shutdown.begin"); }
QString shutdownCompleted() { return QStringLiteral("shutdown.completed"); }

QStringList orderedStartupSequence()
{
    return {
        startupBegin(),
        startupLoggingReady(),
        startupOverlayBegin(),
        startupOverlayOk(),
        startupTtsBegin(),
        startupTtsOk(),
        startupWakeBegin(),
        startupWakeOk(),
        startupCompleted(),
    };
}

QStringList orderedShutdownSequence()
{
    return {
        shutdownBegin(),
        shutdownCompleted(),
    };
}

} // namespace StartupMilestones
