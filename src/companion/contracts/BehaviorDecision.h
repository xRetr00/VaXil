#pragma once

#include <QString>
#include <QVariantMap>

struct BehaviorDecision
{
    bool allowed = false;
    QString action;
    QString reasonCode;
    double score = 0.0;
    QVariantMap details;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = details;
        map.insert(QStringLiteral("allowed"), allowed);
        map.insert(QStringLiteral("action"), action);
        map.insert(QStringLiteral("reasonCode"), reasonCode);
        map.insert(QStringLiteral("score"), score);
        return map;
    }
};
