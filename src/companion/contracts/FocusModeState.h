#pragma once

#include <QString>
#include <QVariantMap>

struct FocusModeState
{
    bool enabled = false;
    bool allowCriticalAlerts = true;
    int durationMinutes = 0;
    qint64 untilEpochMs = 0;
    QString source;

    [[nodiscard]] bool isTimed() const
    {
        return enabled && durationMinutes > 0 && untilEpochMs > 0;
    }

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        return {
            { QStringLiteral("enabled"), enabled },
            { QStringLiteral("allowCriticalAlerts"), allowCriticalAlerts },
            { QStringLiteral("durationMinutes"), durationMinutes },
            { QStringLiteral("untilEpochMs"), untilEpochMs },
            { QStringLiteral("source"), source }
        };
    }
};
