#include "memory/MemoryStore.h"

#include <QtGlobal>

#include "behavior_tuning/TuningEpisodeFeedbackScorer.h"

namespace {
QString scoreSummary(const QVariantMap &score)
{
    return QStringLiteral("Tuning feedback score: %1 episode for mode %2 scored %3 from %4 feedback signals: accepted=%5, dismissed=%6, deferred=%7, ignored=%8, outcome=%9.")
        .arg(score.value(QStringLiteral("action")).toString(),
             score.value(QStringLiteral("mode")).toString(),
             QString::number(score.value(QStringLiteral("supportScore")).toDouble(), 'f', 2),
             score.value(QStringLiteral("totalFeedbackCount")).toString(),
             score.value(QStringLiteral("acceptedCount")).toString(),
             score.value(QStringLiteral("dismissedCount")).toString(),
             score.value(QStringLiteral("deferredCount")).toString(),
             score.value(QStringLiteral("ignoredCount")).toString(),
             score.value(QStringLiteral("outcome")).toString())
        .simplified();
}
}

QVariantList MemoryStore::compiledContextPolicyTuningFeedbackScores() const
{
    return TuningEpisodeFeedbackScorer::score(
        compiledContextPolicyTuningEpisodes(),
        feedbackSignalHistory());
}

QList<MemoryRecord> MemoryStore::compiledContextPolicyTuningFeedbackScoreMemory(int maxCount) const
{
    const QVariantList scores = compiledContextPolicyTuningFeedbackScores();
    if (scores.isEmpty() || maxCount <= 0) {
        return {};
    }

    QList<MemoryRecord> records;
    const int start = qMax(0, scores.size() - maxCount);
    for (int i = start; i < scores.size(); ++i) {
        const QVariantMap score = scores.at(i).toMap();
        MemoryRecord record;
        record.type = QStringLiteral("context");
        record.key = QStringLiteral("compiled_context_policy_tuning_feedback_score_%1").arg(i - start + 1);
        record.value = scoreSummary(score);
        record.confidence = 0.83f;
        record.source = QStringLiteral("compiled_history_policy_tuning_feedback_score");
        record.updatedAt = QString::number(score.value(QStringLiteral("createdAtMs")).toLongLong());
        records.push_back(record);
    }
    return records;
}
