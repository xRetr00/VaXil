#pragma once

#include <QString>
#include <QVariantMap>

struct MemoryRecord
{
    QString memoryId;
    QString layer;
    QString summary;
    QVariantMap payload;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = payload;
        map.insert(QStringLiteral("memoryId"), memoryId);
        map.insert(QStringLiteral("layer"), layer);
        map.insert(QStringLiteral("summary"), summary);
        return map;
    }
};
