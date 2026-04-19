#include "perception/FocusModeExpiryRuntime.h"

#include "companion/contracts/FocusModeState.h"
#include "logging/LoggingService.h"
#include "settings/AppSettings.h"

bool FocusModeExpiryRuntime::reconcile(AppSettings *settings,
                                       LoggingService *loggingService,
                                       qint64 nowMs,
                                       const QString &source)
{
    if (settings == nullptr || !settings->focusModeEnabled()) {
        return false;
    }

    const qint64 untilEpochMs = settings->focusModeUntilEpochMs();
    if (untilEpochMs <= 0 || nowMs < untilEpochMs) {
        return false;
    }

    FocusModeState expiredState;
    expiredState.enabled = false;
    expiredState.allowCriticalAlerts = settings->focusModeAllowCriticalAlerts();
    expiredState.durationMinutes = 0;
    expiredState.untilEpochMs = 0;
    expiredState.source = source.trimmed().isEmpty() ? QStringLiteral("runtime") : source.trimmed();

    settings->setFocusModeEnabled(false);
    settings->setFocusModeDurationMinutes(0);
    settings->setFocusModeUntilEpochMs(0);

    if (loggingService != nullptr) {
        loggingService->logFocusModeTransition(
            expiredState,
            expiredState.source,
            QStringLiteral("focus_mode.timed_expired"));
    }
    return true;
}
