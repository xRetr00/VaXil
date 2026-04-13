#pragma once

#include <QString>
#include <QVariantMap>

struct TriggerRule
{
    QString triggerId;
    QString family;
    QString condition;
    bool enabled = true;
    QVariantMap metadata;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = metadata;
        map.insert(QStringLiteral("triggerId"), triggerId);
        map.insert(QStringLiteral("family"), family);
        map.insert(QStringLiteral("condition"), condition);
        map.insert(QStringLiteral("enabled"), enabled);
        return map;
    }
};
