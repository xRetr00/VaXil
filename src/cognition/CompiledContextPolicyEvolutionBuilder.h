#pragma once

#include <QList>
#include <QVariantList>

#include "core/AssistantTypes.h"

class CompiledContextPolicyEvolutionBuilder
{
public:
    [[nodiscard]] static QList<MemoryRecord> build(const QVariantList &history);
};
