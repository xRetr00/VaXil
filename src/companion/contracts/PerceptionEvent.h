#pragma once

#include <QDateTime>
#include <QString>
#include <QVariantMap>

struct PerceptionEvent
{
    QString family;
    QString source;
    QString summary;
    QDateTime observedAtUtc = QDateTime::currentDateTimeUtc();
    QVariantMap payload;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = payload;
        map.insert(QStringLiteral("family"), family);
        map.insert(QStringLiteral("source"), source);
        map.insert(QStringLiteral("summary"), summary);
        map.insert(QStringLiteral("observedAtUtc"), observedAtUtc.toString(Qt::ISODateWithMs));
        return map;
    }
};
