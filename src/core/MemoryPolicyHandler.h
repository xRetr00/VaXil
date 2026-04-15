#pragma once

#include <QString>
#include <QVariantMap>

#include "core/AssistantTypes.h"

class IdentityProfileService;
class MemoryStore;
struct CompiledContextHistoryPolicyDecision;

class MemoryPolicyHandler
{
public:
    MemoryPolicyHandler(IdentityProfileService *identityProfileService, MemoryStore *memoryStore);

    void processUserTurn(const QString &rawInput, const QString &effectiveInput) const;
    void applyUserInput(const QString &input) const;
    QList<MemoryRecord> requestMemory(const QString &query, const MemoryRecord &runtimeRecord) const;
    [[nodiscard]] QVariantMap compiledContextPolicyState() const;
    [[nodiscard]] CompiledContextHistoryPolicyDecision compiledContextPolicyDecision() const;
    [[nodiscard]] QVariantMap compiledContextPolicyTuningMetadata() const;
    [[nodiscard]] QList<MemoryRecord> compiledContextPolicySummaryRecords() const;
    [[nodiscard]] QList<MemoryRecord> compiledContextLayeredMemoryRecords() const;
    [[nodiscard]] QList<MemoryRecord> compiledContextPolicyEvolutionRecords() const;
    [[nodiscard]] QList<MemoryRecord> compiledContextPolicyTuningSignalRecords() const;
    void captureExplicitMemoryFromInput(const QString &input) const;

private:
    void storeDerivedMemory(MemoryType type,
                            const QString &key,
                            const QString &value,
                            const QStringList &tags = {},
                            const QString &source = QStringLiteral("conversation")) const;
    void captureImplicitMemoryFromInput(const QString &input) const;

    IdentityProfileService *m_identityProfileService = nullptr;
    MemoryStore *m_memoryStore = nullptr;
};
