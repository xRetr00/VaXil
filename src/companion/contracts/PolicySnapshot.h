#pragma once

#include <QString>
#include <QVariantMap>

struct PolicySnapshot
{
    QString policyId;
    QString version;
    QString source;
    QVariantMap parameters;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = parameters;
        map.insert(QStringLiteral("policyId"), policyId);
        map.insert(QStringLiteral("version"), version);
        map.insert(QStringLiteral("source"), source);
        return map;
    }
};
