#pragma once

#include <QString>
#include <QVariantMap>

struct PermissionGrant
{
    QString capabilityId;
    bool granted = false;
    QString scope;
    QString reasonCode;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        return {
            { QStringLiteral("capabilityId"), capabilityId },
            { QStringLiteral("granted"), granted },
            { QStringLiteral("scope"), scope },
            { QStringLiteral("reasonCode"), reasonCode }
        };
    }
};
