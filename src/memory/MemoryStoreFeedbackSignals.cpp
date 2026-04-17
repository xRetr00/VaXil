#include "memory/MemoryStore.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QStringList>

namespace {
constexpr int kMaxFeedbackHistory = 96;

bool upsertFeedbackSignalHistoryEntry(MemoryStore *store, const QVariantList &history)
{
    const QJsonDocument document = QJsonDocument::fromVariant(history);
    MemoryEntry entry;
    entry.type = MemoryType::Context;
    entry.kind = QStringLiteral("context");
    entry.key = QStringLiteral("feedback_signal_history_state");
    entry.title = QStringLiteral("feedback_signal_history");
    entry.value = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    entry.content = entry.value;
    entry.id = QStringLiteral("feedback-signal-history");
    entry.confidence = 0.91f;
    entry.source = QStringLiteral("behavior_tuning_feedback_history");
    entry.tags = {QStringLiteral("behavior_tuning_feedback")};
    entry.createdAt = QDateTime::currentDateTimeUtc();
    entry.updatedAt = entry.createdAt.toUTC().toString(Qt::ISODate);
    return store->upsertEntry(entry);
}

QString normalizedType(const QVariantMap &signal)
{
    const QString value = signal.value(QStringLiteral("signalType")).toString().trimmed();
    return value.isEmpty() ? QStringLiteral("unknown") : value;
}

QString normalizedSuggestionType(const QVariantMap &signal)
{
    QString value = signal.value(QStringLiteral("suggestionType")).toString().trimmed();
    if (value.isEmpty()) {
        value = signal.value(QStringLiteral("value")).toString().trimmed();
    }
    return value.isEmpty() ? QStringLiteral("unknown") : value;
}

QString aggregateSummary(const QVariantList &history)
{
    QMap<QString, int> bySignal;
    QMap<QString, int> bySuggestion;
    for (const QVariant &item : history) {
        const QVariantMap signal = item.toMap();
        bySignal[normalizedType(signal)] += 1;
        bySuggestion[normalizedSuggestionType(signal)] += 1;
    }

    QStringList signalParts;
    for (auto it = bySignal.cbegin(); it != bySignal.cend(); ++it) {
        signalParts.push_back(QStringLiteral("%1=%2").arg(it.key(), QString::number(it.value())));
    }

    QStringList suggestionParts;
    for (auto it = bySuggestion.cbegin(); it != bySuggestion.cend(); ++it) {
        suggestionParts.push_back(QStringLiteral("%1=%2").arg(it.key(), QString::number(it.value())));
    }

    return QStringLiteral("Feedback aggregate: %1 signals across %2 proactive suggestions. Signal counts: %3. Suggestion counts: %4.")
        .arg(QString::number(history.size()),
             QString::number(bySuggestion.size()),
             signalParts.join(QStringLiteral(", ")),
             suggestionParts.join(QStringLiteral(", ")))
        .simplified();
}
}

bool MemoryStore::appendFeedbackSignal(const FeedbackSignal &signal)
{
    if (signal.signalId.trimmed().isEmpty() && signal.signalType.trimmed().isEmpty()) {
        return false;
    }

    QVariantMap normalized = signal.toVariantMap();
    if (!normalized.contains(QStringLiteral("occurredAtMs"))) {
        normalized.insert(QStringLiteral("occurredAtMs"), QDateTime::currentMSecsSinceEpoch());
    }

    QVariantList history = feedbackSignalHistory();
    history.push_back(normalized);
    while (history.size() > kMaxFeedbackHistory) {
        history.removeFirst();
    }
    return upsertFeedbackSignalHistoryEntry(this, history);
}

QVariantList MemoryStore::feedbackSignalHistory() const
{
    for (const MemoryEntry &entry : allEntries()) {
        if (entry.source != QStringLiteral("behavior_tuning_feedback_history")) {
            continue;
        }
        if (entry.key != feedbackSignalHistoryStorageKey()) {
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(entry.value.toUtf8());
        return document.isArray() ? document.array().toVariantList() : QVariantList{};
    }
    return {};
}

QList<MemoryRecord> MemoryStore::feedbackSignalMemory(int maxRecentCount) const
{
    const QVariantList history = feedbackSignalHistory();
    if (history.isEmpty()) {
        return {};
    }

    QList<MemoryRecord> records;
    MemoryRecord aggregate;
    aggregate.type = QStringLiteral("context");
    aggregate.key = QStringLiteral("behavior_tuning_feedback_aggregate");
    aggregate.value = aggregateSummary(history);
    aggregate.confidence = 0.84f;
    aggregate.source = QStringLiteral("behavior_tuning_feedback_memory");
    aggregate.updatedAt = QString::number(history.last().toMap().value(QStringLiteral("occurredAtMs")).toLongLong());
    records.push_back(aggregate);

    const int start = qMax(0, history.size() - qMax(0, maxRecentCount));
    for (int i = start; i < history.size(); ++i) {
        const QVariantMap signal = history.at(i).toMap();
        MemoryRecord recent;
        recent.type = QStringLiteral("context");
        recent.key = QStringLiteral("behavior_tuning_feedback_recent_%1").arg(i - start + 1);
        recent.value = QStringLiteral("Recent feedback: %1 on %2 suggestion, trace %3.")
                           .arg(normalizedType(signal),
                                normalizedSuggestionType(signal),
                                signal.value(QStringLiteral("traceId")).toString().trimmed())
                           .simplified();
        recent.confidence = 0.8f;
        recent.source = QStringLiteral("behavior_tuning_feedback_memory");
        recent.updatedAt = QString::number(signal.value(QStringLiteral("occurredAtMs")).toLongLong());
        records.push_back(recent);
    }
    return records;
}

QString MemoryStore::feedbackSignalHistoryStorageKey() const
{
    return QStringLiteral("feedback_signal_history_state");
}
