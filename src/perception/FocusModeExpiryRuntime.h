#pragma once

#include <QString>

class AppSettings;
class LoggingService;

class FocusModeExpiryRuntime
{
public:
    [[nodiscard]] static bool reconcile(AppSettings *settings,
                                        LoggingService *loggingService,
                                        qint64 nowMs,
                                        const QString &source = QStringLiteral("perception_monitor"));
};
