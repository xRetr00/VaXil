#pragma once

#include <QString>
#include <QVariantMap>

struct TaskRecord
{
    QString taskId;
    QString title;
    QString state;
    QString dueAtUtc;
    QVariantMap metadata;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = metadata;
        map.insert(QStringLiteral("taskId"), taskId);
        map.insert(QStringLiteral("title"), title);
        map.insert(QStringLiteral("state"), state);
        map.insert(QStringLiteral("dueAtUtc"), dueAtUtc);
        return map;
    }
};
