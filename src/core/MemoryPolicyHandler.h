#pragma once

#include <functional>

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "core/AssistantTypes.h"

class IdentityProfileService;
class MemoryStore;
struct CompiledContextHistoryPolicyDecision;

struct MemoryDecisionSignal
{
    bool memoryCandidatePresent = false;
    QString memoryAction = QStringLiteral("none");
    QString memoryType = QStringLiteral("none");
    double confidence = 0.0;
    QString privacyRiskLevel = QStringLiteral("low");
    bool wasUserConfirmed = false;
    QString outcomeLabel = QStringLiteral("unknown");
};

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
    [[nodiscard]] QList<MemoryRecord> compiledContextPolicyTuningEpisodeRecords() const;
    [[nodiscard]] QList<MemoryRecord> compiledContextPolicyTuningFeedbackScoreRecords() const;
    [[nodiscard]] QList<MemoryRecord> feedbackSignalRecords() const;
    void captureExplicitMemoryFromInput(const QString &input) const;
    void setDecisionCallback(std::function<void(const MemoryDecisionSignal &)> callback);

private:
    [[nodiscard]] bool storeDerivedMemory(MemoryType type,
                                          const QString &key,
                                          const QString &value,
                                          const QStringList &tags = {},
                                          const QString &source = QStringLiteral("conversation"),
                                          bool userConfirmed = false) const;
    [[nodiscard]] bool captureImplicitMemoryFromInput(const QString &input) const;
    void emitDecisionSignal(const MemoryDecisionSignal &signal) const;

    IdentityProfileService *m_identityProfileService = nullptr;
    MemoryStore *m_memoryStore = nullptr;
    std::function<void(const MemoryDecisionSignal &)> m_decisionCallback;
};
