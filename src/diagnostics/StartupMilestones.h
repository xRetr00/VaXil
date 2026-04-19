#pragma once

#include <QString>
#include <QStringList>

namespace StartupMilestones {

QString startupBegin();
QString startupLoggingReady();
QString startupOverlayBegin();
QString startupOverlayOk();
QString startupOverlayFail();
QString startupTtsBegin();
QString startupTtsOk();
QString startupTtsFail();
QString startupWakeBegin();
QString startupWakeOk();
QString startupWakeFail();
QString startupCompleted();
QString shutdownBegin();
QString shutdownCompleted();

QStringList orderedStartupSequence();
QStringList orderedShutdownSequence();

} // namespace StartupMilestones
