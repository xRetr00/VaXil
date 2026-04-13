#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

struct PromptAssembly
{
    QString systemPrompt;
    QString userPrompt;
    QStringList includedMemoryIds;
    QStringList includedToolIds;
    QVariantMap reasonCodes;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        return {
            { QStringLiteral("systemPrompt"), systemPrompt },
            { QStringLiteral("userPrompt"), userPrompt },
            { QStringLiteral("includedMemoryIds"), includedMemoryIds },
            { QStringLiteral("includedToolIds"), includedToolIds },
            { QStringLiteral("reasonCodes"), reasonCodes }
        };
    }
};
