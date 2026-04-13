#pragma once

#include <QString>
#include <QVariantMap>

struct RiskAssessment
{
    QString level = QStringLiteral("low");
    bool confirmationRequired = false;
    QString reasonCode;
    QVariantMap details;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = details;
        map.insert(QStringLiteral("level"), level);
        map.insert(QStringLiteral("confirmationRequired"), confirmationRequired);
        map.insert(QStringLiteral("reasonCode"), reasonCode);
        return map;
    }
};
