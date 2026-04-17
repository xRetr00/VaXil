#include "memory/MemoryStore.h"

#include <QJsonArray>
#include <QJsonDocument>

QVariantList MemoryStore::compiledContextPolicyTuningEpisodes() const
{
    for (const MemoryEntry &entry : allEntries()) {
        if (entry.source != QStringLiteral("compiled_history_policy_tuning_episodes")) {
            continue;
        }
        if (entry.key != compiledContextPolicyTuningEpisodesStorageKey()) {
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(entry.value.toUtf8());
        return document.isArray() ? document.array().toVariantList() : QVariantList{};
    }
    return {};
}

QList<MemoryRecord> MemoryStore::compiledContextPolicyTuningEpisodeMemory(int maxCount) const
{
    const QVariantList episodes = compiledContextPolicyTuningEpisodes();
    if (episodes.isEmpty() || maxCount <= 0) {
        return {};
    }

    QList<MemoryRecord> records;
    const int start = qMax(0, episodes.size() - maxCount);
    for (int i = start; i < episodes.size(); ++i) {
        const QVariantMap episode = episodes.at(i).toMap();
        const QString action = episode.value(QStringLiteral("action")).toString().trimmed();
        const QString mode = episode.value(QStringLiteral("mode")).toString().trimmed();
        if (action.isEmpty() || mode.isEmpty()) {
            continue;
        }

        MemoryRecord record;
        record.type = QStringLiteral("context");
        record.key = QStringLiteral("compiled_context_policy_tuning_episode_%1").arg(i - start + 1);
        record.value = QStringLiteral("Tuning episode: %1 mode %2 from version %3 to %4 because %5.")
                           .arg(action,
                                mode,
                                episode.value(QStringLiteral("fromVersion")).toString(),
                                episode.value(QStringLiteral("toVersion")).toString(),
                                episode.value(QStringLiteral("reasonCode")).toString())
                           .simplified();
        record.confidence = 0.82f;
        record.source = QStringLiteral("compiled_history_policy_tuning_episode");
        record.updatedAt = QString::number(episode.value(QStringLiteral("createdAtMs")).toLongLong());
        records.push_back(record);
    }
    return records;
}

QString MemoryStore::compiledContextPolicyTuningEpisodesStorageKey() const
{
    return QStringLiteral("compiled_context_policy_tuning_episodes_state");
}
