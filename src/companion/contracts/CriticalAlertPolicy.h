#pragma once

#include <QStringList>
#include <QVariantMap>

struct CriticalAlertPolicy
{
    bool enabled = true;
    QStringList allowedFamilies;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        return {
            { QStringLiteral("enabled"), enabled },
            { QStringLiteral("allowedFamilies"), allowedFamilies }
        };
    }
};
