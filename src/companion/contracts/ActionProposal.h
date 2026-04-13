#pragma once

#include <QString>
#include <QVariantMap>

struct ActionProposal
{
    QString proposalId;
    QString capabilityId;
    QString title;
    QString summary;
    QString priority;
    QVariantMap arguments;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = arguments;
        map.insert(QStringLiteral("proposalId"), proposalId);
        map.insert(QStringLiteral("capabilityId"), capabilityId);
        map.insert(QStringLiteral("title"), title);
        map.insert(QStringLiteral("summary"), summary);
        map.insert(QStringLiteral("priority"), priority);
        return map;
    }
};
