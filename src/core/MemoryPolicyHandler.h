#pragma once

#include <QString>

#include "core/AssistantTypes.h"

class IdentityProfileService;
class MemoryStore;

class MemoryPolicyHandler
{
public:
    MemoryPolicyHandler(IdentityProfileService *identityProfileService, MemoryStore *memoryStore);

    void processUserTurn(const QString &rawInput, const QString &effectiveInput) const;
    void applyUserInput(const QString &input) const;
    QList<MemoryRecord> requestMemory(const QString &query, const MemoryRecord &runtimeRecord) const;
    void captureExplicitMemoryFromInput(const QString &input) const;

private:
    IdentityProfileService *m_identityProfileService = nullptr;
    MemoryStore *m_memoryStore = nullptr;
};
