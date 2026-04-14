#pragma once

#include <QDateTime>
#include <QString>
#include <QVariantMap>

struct ConnectorEvent
{
    QString eventId;
    QString sourceKind;
    QString connectorKind;
    QString taskType;
    QString summary;
    QString taskKey;
    int taskId = 0;
    int itemCount = 0;
    QString priority = QStringLiteral("medium");
    QDateTime occurredAtUtc = QDateTime::currentDateTimeUtc();
    QVariantMap metadata;

    [[nodiscard]] bool isValid() const
    {
        return !sourceKind.trimmed().isEmpty()
            && !connectorKind.trimmed().isEmpty()
            && !taskType.trimmed().isEmpty()
            && !summary.trimmed().isEmpty();
    }

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = metadata;
        map.insert(QStringLiteral("eventId"), eventId);
        map.insert(QStringLiteral("sourceKind"), sourceKind);
        map.insert(QStringLiteral("connectorKind"), connectorKind);
        map.insert(QStringLiteral("taskType"), taskType);
        map.insert(QStringLiteral("summary"), summary);
        map.insert(QStringLiteral("taskKey"), taskKey);
        map.insert(QStringLiteral("taskId"), taskId);
        map.insert(QStringLiteral("itemCount"), itemCount);
        map.insert(QStringLiteral("priority"), priority);
        map.insert(QStringLiteral("occurredAtUtc"), occurredAtUtc.toString(Qt::ISODateWithMs));
        return map;
    }
};
