#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QVariantMap>
#include <memory>

#include "core/AssistantTypes.h"
#include "companion/contracts/FeedbackSignal.h"
#include "memory/MemoryManager.h"

class MemoryStore : public QObject
{
    Q_OBJECT

public:
    explicit MemoryStore(const QString &storagePath = QString(), QObject *parent = nullptr);

    void appendConversation(const QString &role, const QString &content);
    void extractUserFacts(const QString &content);
    QList<AiMessage> recentMessages(int maxCount) const;
    QList<MemoryRecord> relevantMemory(const QString &query) const;
    QList<MemoryEntry> searchEntries(const QString &query, int maxCount = 8) const;
    bool upsertEntry(const MemoryEntry &entry);
    bool deleteEntry(const QString &idOrTitle);
    QList<MemoryEntry> allEntries() const;
    QString userName() const;
    QList<MemoryRecord> connectorMemory(const QString &query, int maxCount = 4) const;
    QList<MemoryRecord> compiledContextPolicyMemory(const QString &query = QString()) const;
    bool upsertConnectorState(const QString &historyKey, const QVariantMap &state);
    bool deleteConnectorState(const QString &historyKey);
    QHash<QString, QVariantMap> connectorStateMap() const;
    bool upsertCompiledContextPolicyState(const QVariantMap &state);
    bool deleteCompiledContextPolicyState();
    QVariantMap compiledContextPolicyState() const;
    QVariantList compiledContextPolicyHistory() const;
    bool promoteCompiledContextPolicyTuningState(const QVariantMap &state);
    bool rollbackCompiledContextPolicyTuningState(const QVariantMap &metadata = {});
    bool deleteCompiledContextPolicyTuningState();
    QVariantMap compiledContextPolicyTuningState() const;
    QVariantList compiledContextPolicyTuningHistory() const;
    QVariantList compiledContextPolicyTuningEpisodes() const;
    QList<MemoryRecord> compiledContextPolicyTuningEpisodeMemory(int maxCount = 4) const;
    QVariantList compiledContextPolicyTuningFeedbackScores() const;
    QList<MemoryRecord> compiledContextPolicyTuningFeedbackScoreMemory(int maxCount = 4) const;
    bool appendFeedbackSignal(const FeedbackSignal &signal);
    QVariantList feedbackSignalHistory() const;
    QList<MemoryRecord> feedbackSignalMemory(int maxRecentCount = 3) const;

private:
    QString transcriptPath() const;
    QList<MemoryRecord> loadMemory() const;
    MemoryEntry normalizeEntry(const MemoryEntry &entry) const;
    QString connectorStateStorageKey(const QString &historyKey) const;
    QString compiledContextPolicyStorageKey() const;
    QString compiledContextPolicyHistoryStorageKey() const;
    QString compiledContextPolicyTuningStorageKey() const;
    QString compiledContextPolicyTuningHistoryStorageKey() const;
    QString compiledContextPolicyTuningEpisodesStorageKey() const;
    QString feedbackSignalHistoryStorageKey() const;
    std::unique_ptr<MemoryManager> m_memoryManager;
};
