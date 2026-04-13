#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

struct MemoryQuery
{
    QString queryText;
    QStringList layers;
    int maxResults = 10;
    QVariantMap filters;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = filters;
        map.insert(QStringLiteral("queryText"), queryText);
        map.insert(QStringLiteral("layers"), layers);
        map.insert(QStringLiteral("maxResults"), maxResults);
        return map;
    }
};
