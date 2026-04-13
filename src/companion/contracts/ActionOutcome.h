#pragma once

#include <QString>
#include <QVariantMap>

struct ActionOutcome
{
    QString proposalId;
    bool success = false;
    QString resultCode;
    QString summary;
    QVariantMap details;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = details;
        map.insert(QStringLiteral("proposalId"), proposalId);
        map.insert(QStringLiteral("success"), success);
        map.insert(QStringLiteral("resultCode"), resultCode);
        map.insert(QStringLiteral("summary"), summary);
        return map;
    }
};
