#pragma once

#include <QString>
#include <QVariantMap>

struct CooldownState
{
    QString threadId;
    qint64 activeUntilEpochMs = 0;
    QString lastTopic;
    QString lastReasonCode;
    int suppressedCount = 0;

    [[nodiscard]] bool isActive(qint64 nowMs) const
    {
        return activeUntilEpochMs > 0 && nowMs < activeUntilEpochMs;
    }

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        return {
            { QStringLiteral("threadId"), threadId },
            { QStringLiteral("activeUntilEpochMs"), activeUntilEpochMs },
            { QStringLiteral("lastTopic"), lastTopic },
            { QStringLiteral("lastReasonCode"), lastReasonCode },
            { QStringLiteral("suppressedCount"), suppressedCount }
        };
    }
};
