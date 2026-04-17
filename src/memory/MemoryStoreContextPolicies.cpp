#include "memory/MemoryStore.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

#include "cognition/CompiledContextHistoryPolicy.h"

namespace {
int compiledContextPolicyScore(const QString &query, const QVariantMap &state)
{
    const QString haystack = QStringList{
        state.value(QStringLiteral("dominantMode")).toString(),
        state.value(QStringLiteral("selectionDirective")).toString(),
        state.value(QStringLiteral("promptDirective")).toString(),
        state.value(QStringLiteral("reasonCode")).toString()
    }.join(QLatin1Char(' ')).toLower();

    int score = 0;
    if (query.isEmpty()) {
        score += 20;
    } else if (haystack.contains(query)) {
        score += 120;
    }
    score += static_cast<int>(state.value(QStringLiteral("strength")).toDouble() * 10.0);
    return score;
}

bool upsertPolicyHistoryEntry(MemoryStore *store, const QVariantList &history)
{
    const QJsonDocument document = QJsonDocument::fromVariant(history);
    MemoryEntry entry;
    entry.type = MemoryType::Context;
    entry.kind = QStringLiteral("context");
    entry.key = QStringLiteral("compiled_context_policy_history_state");
    entry.title = QStringLiteral("compiled_context_policy_history");
    entry.value = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    entry.content = entry.value;
    entry.id = QStringLiteral("compiled-context-policy-history");
    entry.confidence = 0.9f;
    entry.source = QStringLiteral("compiled_history_policy_history");
    entry.tags = {QStringLiteral("compiled_context_policy_history")};
    entry.createdAt = QDateTime::currentDateTimeUtc();
    entry.updatedAt = entry.createdAt.toUTC().toString(Qt::ISODate);
    return store->upsertEntry(entry);
}

bool upsertTuningHistoryEntry(MemoryStore *store, const QVariantList &history)
{
    const QJsonDocument document = QJsonDocument::fromVariant(history);
    MemoryEntry entry;
    entry.type = MemoryType::Context;
    entry.kind = QStringLiteral("context");
    entry.key = QStringLiteral("compiled_context_policy_tuning_history_state");
    entry.title = QStringLiteral("compiled_context_policy_tuning_history");
    entry.value = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    entry.content = entry.value;
    entry.id = QStringLiteral("compiled-context-policy-tuning-history");
    entry.confidence = 0.9f;
    entry.source = QStringLiteral("compiled_history_policy_tuning_history");
    entry.tags = {QStringLiteral("compiled_context_policy_tuning_history")};
    entry.createdAt = QDateTime::currentDateTimeUtc();
    entry.updatedAt = entry.createdAt.toUTC().toString(Qt::ISODate);
    return store->upsertEntry(entry);
}

bool upsertTuningEpisodesEntry(MemoryStore *store, const QVariantList &episodes)
{
    const QJsonDocument document = QJsonDocument::fromVariant(episodes);
    MemoryEntry entry;
    entry.type = MemoryType::Context;
    entry.kind = QStringLiteral("context");
    entry.key = QStringLiteral("compiled_context_policy_tuning_episodes_state");
    entry.title = QStringLiteral("compiled_context_policy_tuning_episodes");
    entry.value = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    entry.content = entry.value;
    entry.id = QStringLiteral("compiled-context-policy-tuning-episodes");
    entry.confidence = 0.9f;
    entry.source = QStringLiteral("compiled_history_policy_tuning_episodes");
    entry.tags = {QStringLiteral("compiled_context_policy_tuning_episodes")};
    entry.createdAt = QDateTime::currentDateTimeUtc();
    entry.updatedAt = entry.createdAt.toUTC().toString(Qt::ISODate);
    return store->upsertEntry(entry);
}

QVariantMap tuningEpisode(const QString &action,
                          const QString &reason,
                          const QVariantMap &state,
                          int fromVersion,
                          int toVersion,
                          qint64 nowMs)
{
    return {
        {QStringLiteral("action"), action},
        {QStringLiteral("reasonCode"), reason.trimmed().isEmpty()
             ? QStringLiteral("behavior_tuning.unknown")
             : reason.trimmed()},
        {QStringLiteral("fromVersion"), fromVersion},
        {QStringLiteral("toVersion"), toVersion},
        {QStringLiteral("mode"), state.value(QStringLiteral("tuningCurrentMode")).toString().trimmed()},
        {QStringLiteral("volatility"), state.value(QStringLiteral("tuningVolatilityLevel")).toString().trimmed()},
        {QStringLiteral("alignmentBoost"), state.value(QStringLiteral("tuningAlignmentBoost")).toDouble()},
        {QStringLiteral("defocusPenalty"), state.value(QStringLiteral("tuningDefocusPenalty")).toDouble()},
        {QStringLiteral("volatilityPenalty"), state.value(QStringLiteral("tuningVolatilityPenalty")).toDouble()},
        {QStringLiteral("suppressionScoreThreshold"),
         state.value(QStringLiteral("tuningSuppressionScoreThreshold")).toDouble()},
        {QStringLiteral("createdAtMs"), nowMs}
    };
}

bool tuningStateChanged(const QVariantMap &lhs, const QVariantMap &rhs)
{
    const QStringList keys = {
        QStringLiteral("tuningCurrentMode"),
        QStringLiteral("tuningVolatilityLevel"),
        QStringLiteral("tuningAlignmentBoost"),
        QStringLiteral("tuningDefocusPenalty"),
        QStringLiteral("tuningVolatilityPenalty"),
        QStringLiteral("tuningSuppressionScoreThreshold")
    };
    for (const QString &key : keys) {
        if (lhs.value(key) != rhs.value(key)) {
            return true;
        }
    }
    return false;
}
}

QList<MemoryRecord> MemoryStore::compiledContextPolicyMemory(const QString &query) const
{
    const QVariantMap state = compiledContextPolicyState();
    if (state.isEmpty()) {
        return {};
    }

    QString normalized = query.trimmed().toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    normalized = normalized.simplified();
    if (compiledContextPolicyScore(normalized, state) <= 0) {
        return {};
    }

    MemoryRecord record = CompiledContextHistoryPolicy::buildContextRecord(
        CompiledContextHistoryPolicy::fromState(state));
    if (record.key.trimmed().isEmpty()) {
        return {};
    }
    record.source = QStringLiteral("compiled_history_policy_memory");
    record.updatedAt = QString::number(state.value(QStringLiteral("updatedAtMs")).toLongLong());
    return {record};
}

bool MemoryStore::upsertCompiledContextPolicyState(const QVariantMap &state)
{
    if (state.isEmpty()) {
        return false;
    }

    QVariantMap normalizedState = state;
    const qint64 updatedAtMs = normalizedState.value(QStringLiteral("updatedAtMs"),
                                                     QDateTime::currentMSecsSinceEpoch()).toLongLong();
    normalizedState.insert(QStringLiteral("updatedAtMs"), updatedAtMs);

    const QJsonDocument document(QJsonObject::fromVariantMap(normalizedState));
    MemoryEntry entry;
    entry.type = MemoryType::Context;
    entry.kind = QStringLiteral("context");
    entry.key = compiledContextPolicyStorageKey();
    entry.title = QStringLiteral("compiled_context_policy");
    entry.value = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    entry.content = entry.value;
    entry.id = QStringLiteral("compiled-context-policy");
    entry.confidence = 0.94f;
    entry.source = QStringLiteral("compiled_history_policy");
    entry.tags = {
        QStringLiteral("compiled_context_policy"),
        normalizedState.value(QStringLiteral("dominantMode")).toString().trimmed()
    };
    entry.createdAt = QDateTime::currentDateTimeUtc();
    entry.updatedAt = entry.createdAt.toUTC().toString(Qt::ISODate);
    if (!upsertEntry(entry)) {
        return false;
    }

    QVariantList history = compiledContextPolicyHistory();
    if (history.isEmpty()) {
        QVariantMap snapshot = normalizedState;
        snapshot.insert(QStringLiteral("enteredAtMs"), updatedAtMs);
        snapshot.insert(QStringLiteral("observations"), 1);
        history.push_back(snapshot);
    } else {
        QVariantMap last = history.last().toMap();
        if (last.value(QStringLiteral("dominantMode")).toString().trimmed()
            == normalizedState.value(QStringLiteral("dominantMode")).toString().trimmed()) {
            last.insert(QStringLiteral("updatedAtMs"), updatedAtMs);
            last.insert(QStringLiteral("strength"), normalizedState.value(QStringLiteral("strength")));
            last.insert(QStringLiteral("selectionDirective"), normalizedState.value(QStringLiteral("selectionDirective")));
            last.insert(QStringLiteral("promptDirective"), normalizedState.value(QStringLiteral("promptDirective")));
            last.insert(QStringLiteral("reasonCode"), normalizedState.value(QStringLiteral("reasonCode")));
            last.insert(QStringLiteral("observations"), qMax(1, last.value(QStringLiteral("observations")).toInt()) + 1);
            history.last() = last;
        } else {
            QVariantMap snapshot = normalizedState;
            snapshot.insert(QStringLiteral("enteredAtMs"), updatedAtMs);
            snapshot.insert(QStringLiteral("observations"), 1);
            history.push_back(snapshot);
        }
    }

    while (history.size() > 12) {
        history.removeFirst();
    }
    return upsertPolicyHistoryEntry(this, history);
}

bool MemoryStore::deleteCompiledContextPolicyState()
{
    const bool deletedCurrent = deleteEntry(compiledContextPolicyStorageKey());
    const bool deletedHistory = deleteEntry(compiledContextPolicyHistoryStorageKey());
    return deletedCurrent || deletedHistory;
}

QVariantMap MemoryStore::compiledContextPolicyState() const
{
    for (const MemoryEntry &entry : allEntries()) {
        if (entry.source != QStringLiteral("compiled_history_policy")) {
            continue;
        }
        if (entry.key != compiledContextPolicyStorageKey()) {
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(entry.value.toUtf8());
        if (!document.isObject()) {
            return {};
        }
        return document.object().toVariantMap();
    }
    return {};
}

QVariantList MemoryStore::compiledContextPolicyHistory() const
{
    for (const MemoryEntry &entry : allEntries()) {
        if (entry.source != QStringLiteral("compiled_history_policy_history")) {
            continue;
        }
        if (entry.key != compiledContextPolicyHistoryStorageKey()) {
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(entry.value.toUtf8());
        if (!document.isArray()) {
            return {};
        }
        return document.array().toVariantList();
    }
    return {};
}

bool MemoryStore::promoteCompiledContextPolicyTuningState(const QVariantMap &state)
{
    if (state.isEmpty()) {
        return false;
    }

    const QVariantMap existingState = compiledContextPolicyTuningState();
    QVariantMap normalizedState = state;
    const qint64 updatedAtMs = normalizedState.value(QStringLiteral("updatedAtMs"),
                                                     QDateTime::currentMSecsSinceEpoch()).toLongLong();
    normalizedState.insert(QStringLiteral("updatedAtMs"), updatedAtMs);
    if (!existingState.isEmpty() && !tuningStateChanged(existingState, normalizedState)) {
        normalizedState.insert(QStringLiteral("version"), existingState.value(QStringLiteral("version"), 1).toInt());
    } else {
        normalizedState.insert(QStringLiteral("version"), existingState.value(QStringLiteral("version"), 0).toInt() + 1);
    }

    const QJsonDocument document(QJsonObject::fromVariantMap(normalizedState));
    MemoryEntry entry;
    entry.type = MemoryType::Context;
    entry.kind = QStringLiteral("context");
    entry.key = compiledContextPolicyTuningStorageKey();
    entry.title = QStringLiteral("compiled_context_policy_tuning");
    entry.value = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    entry.content = entry.value;
    entry.id = QStringLiteral("compiled-context-policy-tuning");
    entry.confidence = 0.93f;
    entry.source = QStringLiteral("compiled_history_policy_tuning_state");
    entry.tags = {QStringLiteral("compiled_context_policy_tuning"),
                  normalizedState.value(QStringLiteral("tuningCurrentMode")).toString().trimmed()};
    entry.createdAt = QDateTime::currentDateTimeUtc();
    entry.updatedAt = entry.createdAt.toUTC().toString(Qt::ISODate);
    if (!upsertEntry(entry)) {
        return false;
    }

    const int fromVersion = existingState.value(QStringLiteral("version"), 0).toInt();
    const int toVersion = normalizedState.value(QStringLiteral("version"), 1).toInt();
    QVariantList episodes = compiledContextPolicyTuningEpisodes();
    episodes.push_back(tuningEpisode(
        normalizedState.value(QStringLiteral("tuningPromotionAction"), QStringLiteral("promote")).toString(),
        normalizedState.value(QStringLiteral("tuningPromotionReason"),
                              QStringLiteral("behavior_tuning.promote_direct")).toString(),
        normalizedState,
        fromVersion,
        toVersion,
        updatedAtMs));
    while (episodes.size() > 48) {
        episodes.removeFirst();
    }
    upsertTuningEpisodesEntry(this, episodes);

    QVariantList history = compiledContextPolicyTuningHistory();
    if (history.isEmpty() || tuningStateChanged(history.last().toMap(), normalizedState)) {
        QVariantMap snapshot = normalizedState;
        snapshot.insert(QStringLiteral("promotedAtMs"), updatedAtMs);
        history.push_back(snapshot);
        while (history.size() > 24) {
            history.removeFirst();
        }
        return upsertTuningHistoryEntry(this, history);
    }
    return true;
}

bool MemoryStore::rollbackCompiledContextPolicyTuningState(const QVariantMap &metadata)
{
    QVariantList history = compiledContextPolicyTuningHistory();
    if (history.size() < 2) {
        return false;
    }

    const QVariantMap rolledBackState = history.last().toMap();
    history.removeLast();
    const QVariantMap rollbackState = history.last().toMap();
    if (rollbackState.isEmpty()) {
        return false;
    }

    if (!upsertTuningHistoryEntry(this, history)) {
        return false;
    }

    QVariantMap normalizedState = rollbackState;
    normalizedState.insert(QStringLiteral("updatedAtMs"), QDateTime::currentMSecsSinceEpoch());
    normalizedState.insert(QStringLiteral("version"),
                           rollbackState.value(QStringLiteral("version"), 1).toInt() + 1);
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        normalizedState.insert(it.key(), it.value());
    }

    const QJsonDocument document(QJsonObject::fromVariantMap(normalizedState));
    MemoryEntry entry;
    entry.type = MemoryType::Context;
    entry.kind = QStringLiteral("context");
    entry.key = compiledContextPolicyTuningStorageKey();
    entry.title = QStringLiteral("compiled_context_policy_tuning");
    entry.value = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    entry.content = entry.value;
    entry.id = QStringLiteral("compiled-context-policy-tuning");
    entry.confidence = 0.93f;
    entry.source = QStringLiteral("compiled_history_policy_tuning_state");
    entry.tags = {QStringLiteral("compiled_context_policy_tuning"),
                  normalizedState.value(QStringLiteral("tuningCurrentMode")).toString().trimmed()};
    entry.createdAt = QDateTime::currentDateTimeUtc();
    entry.updatedAt = entry.createdAt.toUTC().toString(Qt::ISODate);
    if (!upsertEntry(entry)) {
        return false;
    }

    QVariantList episodes = compiledContextPolicyTuningEpisodes();
    episodes.push_back(tuningEpisode(
        QStringLiteral("rollback"),
        normalizedState.value(QStringLiteral("tuningPromotionReason"),
                              QStringLiteral("behavior_tuning.rollback")).toString(),
        normalizedState,
        rolledBackState.value(QStringLiteral("version"), 1).toInt(),
        normalizedState.value(QStringLiteral("version"), 1).toInt(),
        normalizedState.value(QStringLiteral("updatedAtMs")).toLongLong()));
    while (episodes.size() > 48) {
        episodes.removeFirst();
    }
    return upsertTuningEpisodesEntry(this, episodes);
}

bool MemoryStore::deleteCompiledContextPolicyTuningState()
{
    const bool deletedCurrent = deleteEntry(compiledContextPolicyTuningStorageKey());
    const bool deletedHistory = deleteEntry(compiledContextPolicyTuningHistoryStorageKey());
    const bool deletedEpisodes = deleteEntry(compiledContextPolicyTuningEpisodesStorageKey());
    return deletedCurrent || deletedHistory || deletedEpisodes;
}

QVariantMap MemoryStore::compiledContextPolicyTuningState() const
{
    for (const MemoryEntry &entry : allEntries()) {
        if (entry.source != QStringLiteral("compiled_history_policy_tuning_state")) {
            continue;
        }
        if (entry.key != compiledContextPolicyTuningStorageKey()) {
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(entry.value.toUtf8());
        if (!document.isObject()) {
            return {};
        }
        return document.object().toVariantMap();
    }
    return {};
}

QVariantList MemoryStore::compiledContextPolicyTuningHistory() const
{
    for (const MemoryEntry &entry : allEntries()) {
        if (entry.source != QStringLiteral("compiled_history_policy_tuning_history")) {
            continue;
        }
        if (entry.key != compiledContextPolicyTuningHistoryStorageKey()) {
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(entry.value.toUtf8());
        if (!document.isArray()) {
            return {};
        }
        return document.array().toVariantList();
    }
    return {};
}

QString MemoryStore::compiledContextPolicyStorageKey() const
{
    return QStringLiteral("compiled_context_policy_state");
}

QString MemoryStore::compiledContextPolicyHistoryStorageKey() const
{
    return QStringLiteral("compiled_context_policy_history_state");
}

QString MemoryStore::compiledContextPolicyTuningStorageKey() const
{
    return QStringLiteral("compiled_context_policy_tuning_state");
}

QString MemoryStore::compiledContextPolicyTuningHistoryStorageKey() const
{
    return QStringLiteral("compiled_context_policy_tuning_history_state");
}
