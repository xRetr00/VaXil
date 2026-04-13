#pragma once

#include <QString>
#include <QVariantMap>

#include "companion/contracts/ContextThreadId.h"

struct CompanionContextSnapshot
{
    ContextThreadId threadId;
    QString appId;
    QString taskId;
    QString topic;
    QString recentIntent;
    double confidence = 0.0;
    QVariantMap metadata;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = metadata;
        map.insert(QStringLiteral("threadId"), threadId.value);
        map.insert(QStringLiteral("appId"), appId);
        map.insert(QStringLiteral("taskId"), taskId);
        map.insert(QStringLiteral("topic"), topic);
        map.insert(QStringLiteral("recentIntent"), recentIntent);
        map.insert(QStringLiteral("confidence"), confidence);
        return map;
    }
};
