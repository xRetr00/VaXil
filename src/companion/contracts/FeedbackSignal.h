#pragma once

#include <QString>
#include <QVariantMap>

struct FeedbackSignal
{
    QString signalId;
    QString signalType;
    QString traceId;
    QString value;
    QVariantMap metadata;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = metadata;
        map.insert(QStringLiteral("signalId"), signalId);
        map.insert(QStringLiteral("signalType"), signalType);
        map.insert(QStringLiteral("traceId"), traceId);
        map.insert(QStringLiteral("value"), value);
        return map;
    }
};
