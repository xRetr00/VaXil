#pragma once

#include <QStringList>
#include <QVariantMap>

struct ToolExposurePlan
{
    QStringList toolIds;
    QVariantMap reasonCodes;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        return {
            { QStringLiteral("toolIds"), toolIds },
            { QStringLiteral("reasonCodes"), reasonCodes }
        };
    }
};
