#include "core/MemoryPolicyHandler.h"

#include <utility>

#include <QDateTime>
#include <QRegularExpression>

#include "cognition/CompiledContextHistoryPolicy.h"
#include "cognition/CompiledContextLayeredMemoryBuilder.h"
#include "cognition/CompiledContextPolicyEvolutionBuilder.h"
#include "cognition/CompiledContextPolicySummaryBuilder.h"
#include "cognition/CompiledContextPolicyTuningSignalBuilder.h"
#include "cognition/ConnectorContextCompiler.h"
#include "memory/MemoryStore.h"
#include "settings/IdentityProfileService.h"

namespace {
QString firstCaptured(const QRegularExpression &expression, const QString &input)
{
    const QRegularExpressionMatch match = expression.match(input.trimmed());
    return match.hasMatch() ? match.captured(1).trimmed() : QString{};
}

QString responseStylePreference(const QString &input)
{
    const QString lowered = input.trimmed().toLower();
    if (lowered.startsWith(QStringLiteral("i prefer "))) {
        return input.trimmed().mid(9).trimmed();
    }
    if (lowered.contains(QStringLiteral("short answers"))
        || lowered.contains(QStringLiteral("keep it brief"))
        || lowered.contains(QStringLiteral("be concise"))) {
        return QStringLiteral("short answers");
    }
    if (lowered.contains(QStringLiteral("detailed answers"))
        || lowered.contains(QStringLiteral("more detail"))
        || lowered.contains(QStringLiteral("explain in detail"))
        || lowered.contains(QStringLiteral("long answers"))) {
        return QStringLiteral("detailed answers");
    }
    return {};
}

MemoryEntry derivedMemoryEntry(MemoryType type,
                               const QString &key,
                               const QString &value,
                               const QStringList &tags,
                               const QString &source)
{
    MemoryEntry entry;
    entry.type = type;
    entry.kind = type == MemoryType::Preference
        ? QStringLiteral("preference")
        : (type == MemoryType::Context ? QStringLiteral("context") : QStringLiteral("fact"));
    entry.key = key.trimmed();
    entry.title = entry.key;
    entry.value = value.trimmed();
    entry.content = entry.value;
    entry.tags = tags;
    entry.confidence = type == MemoryType::Context ? 0.78f : 0.9f;
    entry.source = source;
    entry.createdAt = QDateTime::currentDateTimeUtc();
    if (type == MemoryType::Context) {
        entry.expiresAt = entry.createdAt.addDays(10);
    }
    return entry;
}
}

MemoryPolicyHandler::MemoryPolicyHandler(IdentityProfileService *identityProfileService, MemoryStore *memoryStore)
    : m_identityProfileService(identityProfileService)
    , m_memoryStore(memoryStore)
{
}

void MemoryPolicyHandler::processUserTurn(const QString &rawInput, const QString &effectiveInput) const
{
    applyUserInput(effectiveInput);
    if (m_memoryStore) {
        m_memoryStore->appendConversation(QStringLiteral("user"), rawInput);
    }
}

void MemoryPolicyHandler::applyUserInput(const QString &input) const
{
    const QString trimmed = input.trimmed();
    const QString lowered = trimmed.toLower();
    if (lowered.startsWith(QStringLiteral("my name is "))) {
        const QString userName = trimmed.mid(11).trimmed();
        if (m_identityProfileService) {
            m_identityProfileService->setUserName(userName);
        }
        storeDerivedMemory(MemoryType::Fact,
                           QStringLiteral("name"),
                           userName,
                           {QStringLiteral("profile")},
                           QStringLiteral("conversation"),
                           true);
        return;
    }

    if (lowered.startsWith(QStringLiteral("call me "))) {
        const QString userName = trimmed.mid(8).trimmed();
        if (m_identityProfileService) {
            m_identityProfileService->setUserName(userName);
        }
        storeDerivedMemory(MemoryType::Fact,
                           QStringLiteral("name"),
                           userName,
                           {QStringLiteral("profile")},
                           QStringLiteral("conversation"),
                           true);
        return;
    }

    const QString preference = responseStylePreference(trimmed);
    if (!preference.isEmpty()) {
        if (m_identityProfileService) {
            m_identityProfileService->setPreference(QStringLiteral("general"), preference);
        }
        storeDerivedMemory(MemoryType::Preference,
                           QStringLiteral("general_preference"),
                           preference,
                           {QStringLiteral("profile"), QStringLiteral("style")},
                           QStringLiteral("conversation"),
                           true);
    }

    captureExplicitMemoryFromInput(input);
    captureImplicitMemoryFromInput(input);
}

QList<MemoryRecord> MemoryPolicyHandler::requestMemory(const QString &query, const MemoryRecord &runtimeRecord) const
{
    QList<MemoryRecord> memory = m_memoryStore ? m_memoryStore->relevantMemory(query) : QList<MemoryRecord>{};
    auto appendUnique = [&memory](const QList<MemoryRecord> &records) {
        for (const MemoryRecord &record : records) {
            bool duplicate = false;
            for (const MemoryRecord &existing : memory) {
                if (existing.key == record.key && existing.source == record.source) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                memory.push_back(record);
            }
        }
    };

    if (m_memoryStore) {
        appendUnique(compiledContextPolicySummaryRecords());
        appendUnique(compiledContextLayeredMemoryRecords());
        appendUnique(compiledContextPolicyEvolutionRecords());
        appendUnique(compiledContextPolicyTuningSignalRecords());
        appendUnique(compiledContextPolicyTuningEpisodeRecords());
        appendUnique(compiledContextPolicyTuningFeedbackScoreRecords());
        appendUnique(feedbackSignalRecords());
        appendUnique(m_memoryStore->compiledContextPolicyMemory(query));
        appendUnique(ConnectorContextCompiler::compileSummaries(query, m_memoryStore->connectorStateMap()));
        appendUnique(m_memoryStore->connectorMemory(query));
    }

    if (!runtimeRecord.key.trimmed().isEmpty()) {
        bool duplicate = false;
        for (const MemoryRecord &existing : memory) {
            if (existing.key == runtimeRecord.key && existing.source == runtimeRecord.source) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            memory.push_front(runtimeRecord);
        }
    }
    return memory;
}

QVariantMap MemoryPolicyHandler::compiledContextPolicyState() const
{
    return m_memoryStore ? m_memoryStore->compiledContextPolicyState() : QVariantMap{};
}

CompiledContextHistoryPolicyDecision MemoryPolicyHandler::compiledContextPolicyDecision() const
{
    return CompiledContextHistoryPolicy::fromState(compiledContextPolicyState());
}

QVariantMap MemoryPolicyHandler::compiledContextPolicyTuningMetadata() const
{
    if (m_memoryStore == nullptr) {
        return {};
    }
    const QVariantMap persisted = m_memoryStore->compiledContextPolicyTuningState();
    if (!persisted.isEmpty()) {
        return CompiledContextPolicyTuningSignalBuilder::buildPlannerMetadataFromState(persisted);
    }
    return {};
}

QList<MemoryRecord> MemoryPolicyHandler::compiledContextPolicySummaryRecords() const
{
    if (m_memoryStore == nullptr) {
        return {};
    }
    return CompiledContextPolicySummaryBuilder::build(
        m_memoryStore->compiledContextPolicyState(),
        m_memoryStore->connectorStateMap());
}

QList<MemoryRecord> MemoryPolicyHandler::compiledContextLayeredMemoryRecords() const
{
    if (m_memoryStore == nullptr) {
        return {};
    }
    return CompiledContextLayeredMemoryBuilder::build(
        m_memoryStore->compiledContextPolicyState(),
        m_memoryStore->connectorStateMap());
}

QList<MemoryRecord> MemoryPolicyHandler::compiledContextPolicyEvolutionRecords() const
{
    if (m_memoryStore == nullptr) {
        return {};
    }
    return CompiledContextPolicyEvolutionBuilder::build(
        m_memoryStore->compiledContextPolicyHistory());
}

QList<MemoryRecord> MemoryPolicyHandler::compiledContextPolicyTuningSignalRecords() const
{
    if (m_memoryStore == nullptr) {
        return {};
    }
    const QVariantMap persisted = m_memoryStore->compiledContextPolicyTuningState();
    if (!persisted.isEmpty()) {
        return CompiledContextPolicyTuningSignalBuilder::buildFromState(persisted);
    }
    return {};
}

QList<MemoryRecord> MemoryPolicyHandler::compiledContextPolicyTuningEpisodeRecords() const
{
    return m_memoryStore ? m_memoryStore->compiledContextPolicyTuningEpisodeMemory() : QList<MemoryRecord>{};
}

QList<MemoryRecord> MemoryPolicyHandler::compiledContextPolicyTuningFeedbackScoreRecords() const
{
    return m_memoryStore ? m_memoryStore->compiledContextPolicyTuningFeedbackScoreMemory() : QList<MemoryRecord>{};
}

QList<MemoryRecord> MemoryPolicyHandler::feedbackSignalRecords() const
{
    return m_memoryStore ? m_memoryStore->feedbackSignalMemory() : QList<MemoryRecord>{};
}

void MemoryPolicyHandler::captureExplicitMemoryFromInput(const QString &input) const
{
    const QString lowered = input.trimmed().toLower();
    if (!lowered.startsWith(QStringLiteral("remember "))
        && !lowered.startsWith(QStringLiteral("remember that "))
        && !lowered.startsWith(QStringLiteral("save this preference "))) {
        return;
    }

    bool stored = false;
    if (m_memoryStore) {
        m_memoryStore->extractUserFacts(input);
        stored = true;
    }

    emitDecisionSignal({
        .memoryCandidatePresent = true,
        .memoryAction = stored ? QStringLiteral("save") : QStringLiteral("skip"),
        .memoryType = QStringLiteral("fact"),
        .confidence = stored ? 0.85f : 0.0f,
        .privacyRiskLevel = QStringLiteral("medium"),
        .wasUserConfirmed = true,
        .outcomeLabel = stored ? QStringLiteral("stored") : QStringLiteral("skipped")
    });
}

void MemoryPolicyHandler::setDecisionCallback(std::function<void(const MemoryDecisionSignal &)> callback)
{
    m_decisionCallback = std::move(callback);
}

void MemoryPolicyHandler::emitDecisionSignal(const MemoryDecisionSignal &signal) const
{
    if (m_decisionCallback) {
        m_decisionCallback(signal);
    }
}

bool MemoryPolicyHandler::storeDerivedMemory(MemoryType type,
                                             const QString &key,
                                             const QString &value,
                                             const QStringList &tags,
                                             const QString &source,
                                             bool userConfirmed) const
{
    if (m_memoryStore == nullptr || key.trimmed().isEmpty() || value.trimmed().isEmpty()) {
        emitDecisionSignal({
            .memoryCandidatePresent = true,
            .memoryAction = QStringLiteral("skip"),
            .memoryType = type == MemoryType::Preference
                ? QStringLiteral("preference")
                : (type == MemoryType::Context ? QStringLiteral("context") : QStringLiteral("fact")),
            .confidence = 0.0f,
            .privacyRiskLevel = type == MemoryType::Preference ? QStringLiteral("low") : QStringLiteral("medium"),
            .wasUserConfirmed = userConfirmed,
            .outcomeLabel = QStringLiteral("skipped")
        });
        return false;
    }

    m_memoryStore->upsertEntry(derivedMemoryEntry(type, key, value, tags, source));
    emitDecisionSignal({
        .memoryCandidatePresent = true,
        .memoryAction = QStringLiteral("save"),
        .memoryType = type == MemoryType::Preference
            ? QStringLiteral("preference")
            : (type == MemoryType::Context ? QStringLiteral("context") : QStringLiteral("fact")),
        .confidence = type == MemoryType::Context ? 0.78f : 0.90f,
        .privacyRiskLevel = type == MemoryType::Preference ? QStringLiteral("low") : QStringLiteral("medium"),
        .wasUserConfirmed = userConfirmed,
        .outcomeLabel = QStringLiteral("stored")
    });
    return true;
}

bool MemoryPolicyHandler::captureImplicitMemoryFromInput(const QString &input) const
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    const QString workContext = firstCaptured(
        QRegularExpression(QStringLiteral("^(?:i am|i'm|we are|we're) working on\\s+(.+)$"),
                           QRegularExpression::CaseInsensitiveOption),
        trimmed);
    if (!workContext.isEmpty()) {
        return storeDerivedMemory(MemoryType::Context,
                                  QStringLiteral("active_work"),
                                  workContext,
                                  {QStringLiteral("active_commitment"), QStringLiteral("work")});
    }

    const QString projectName = firstCaptured(
        QRegularExpression(QStringLiteral("^(?:my project is|this project is|the project is)\\s+(.+)$"),
                           QRegularExpression::CaseInsensitiveOption),
        trimmed);
    if (!projectName.isEmpty()) {
        return storeDerivedMemory(MemoryType::Context,
                                  QStringLiteral("current_project"),
                                  projectName,
                                  {QStringLiteral("active_commitment"), QStringLiteral("project")});
    }

    const QString prefersPattern = firstCaptured(
        QRegularExpression(QStringLiteral("^(?:please keep|please make)\\s+(.+)$"),
                           QRegularExpression::CaseInsensitiveOption),
        trimmed);
    if (!prefersPattern.isEmpty()) {
        return storeDerivedMemory(MemoryType::Preference,
                                  QStringLiteral("general_preference"),
                                  prefersPattern,
                                  {QStringLiteral("profile"), QStringLiteral("style")});
    }

    return false;
}
