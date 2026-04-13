#pragma once

#include <QDateTime>
#include <QString>
#include <QVariantMap>
#include <QUuid>

struct BehaviorTraceEvent
{
    QString eventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString sessionId;
    QString traceId;
    QString threadId;
    QString capabilityId;
    QString actor = QStringLiteral("system");
    QString stage;
    QString family;
    QString reasonCode;
    QDateTime timestampUtc = QDateTime::currentDateTimeUtc();
    QVariantMap payload;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = payload;
        map.insert(QStringLiteral("eventId"), eventId);
        map.insert(QStringLiteral("sessionId"), sessionId);
        map.insert(QStringLiteral("traceId"), traceId);
        map.insert(QStringLiteral("threadId"), threadId);
        map.insert(QStringLiteral("capabilityId"), capabilityId);
        map.insert(QStringLiteral("actor"), actor);
        map.insert(QStringLiteral("stage"), stage);
        map.insert(QStringLiteral("family"), family);
        map.insert(QStringLiteral("reasonCode"), reasonCode);
        map.insert(QStringLiteral("timestampUtc"), timestampUtc.toString(Qt::ISODateWithMs));
        return map;
    }

    [[nodiscard]] static BehaviorTraceEvent create(const QString &familyName,
                                                   const QString &stageName,
                                                   const QString &reason,
                                                   const QVariantMap &eventPayload = {},
                                                   const QString &eventActor = QStringLiteral("system"))
    {
        BehaviorTraceEvent event;
        event.family = familyName;
        event.stage = stageName;
        event.reasonCode = reason;
        event.payload = eventPayload;
        event.actor = eventActor;
        return event;
    }
};
