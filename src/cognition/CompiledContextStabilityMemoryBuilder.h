#pragma once

#include <QString>
#include <QStringList>

#include "core/AssistantTypes.h"

class CompiledContextStabilityMemoryBuilder
{
public:
    [[nodiscard]] static MemoryRecord build(const QString &purpose,
                                            const QString &summaryText,
                                            const QStringList &stableKeys,
                                            int stableCycles,
                                            qint64 stableDurationMs);
};
