#include "behavior_tuning/TuningEpisodeFeedbackScorer.h"

#include <QString>
#include <QVariantMap>
#include <limits>

namespace {
QString normalizedSignalType(const QVariantMap &signal)
{
    return signal.value(QStringLiteral("signalType")).toString().trimmed().toLower();
}

qint64 occurredAtMs(const QVariantMap &signal)
{
    return signal.value(QStringLiteral("occurredAtMs")).toLongLong();
}

qint64 createdAtMs(const QVariantMap &episode)
{
    return episode.value(QStringLiteral("createdAtMs")).toLongLong();
}

double signalWeight(const QString &type)
{
    if (type == QStringLiteral("accepted")) {
        return 1.0;
    }
    if (type == QStringLiteral("dismissed") || type == QStringLiteral("rejected")) {
        return -1.0;
    }
    if (type == QStringLiteral("ignored") || type == QStringLiteral("expired")) {
        return -0.5;
    }
    if (type == QStringLiteral("deferred")) {
        return -0.25;
    }
    return 0.0;
}

QString outcomeFor(double supportScore, int total)
{
    if (total <= 0) {
        return QStringLiteral("unscored");
    }
    if (supportScore >= 1.0) {
        return QStringLiteral("supported");
    }
    if (supportScore <= -1.0) {
        return QStringLiteral("rejected");
    }
    return QStringLiteral("mixed");
}

QString reasonFor(const QString &outcome)
{
    if (outcome == QStringLiteral("supported")) {
        return QStringLiteral("behavior_tuning.feedback_supported");
    }
    if (outcome == QStringLiteral("rejected")) {
        return QStringLiteral("behavior_tuning.feedback_rejected");
    }
    if (outcome == QStringLiteral("mixed")) {
        return QStringLiteral("behavior_tuning.feedback_mixed");
    }
    return QStringLiteral("behavior_tuning.feedback_unscored");
}
}

QVariantList TuningEpisodeFeedbackScorer::score(const QVariantList &episodes,
                                                const QVariantList &feedbackSignals)
{
    QVariantList scores;
    for (int episodeIndex = 0; episodeIndex < episodes.size(); ++episodeIndex) {
        const QVariantMap episode = episodes.at(episodeIndex).toMap();
        const qint64 startMs = createdAtMs(episode);
        const qint64 endMs = episodeIndex + 1 < episodes.size()
            ? createdAtMs(episodes.at(episodeIndex + 1).toMap())
            : std::numeric_limits<qint64>::max();

        int accepted = 0;
        int dismissed = 0;
        int deferred = 0;
        int ignored = 0;
        int total = 0;
        double supportScore = 0.0;

        for (const QVariant &item : feedbackSignals) {
            const QVariantMap signal = item.toMap();
            const qint64 signalAt = occurredAtMs(signal);
            if (signalAt < startMs || signalAt >= endMs) {
                continue;
            }

            const QString type = normalizedSignalType(signal);
            if (type == QStringLiteral("accepted")) {
                ++accepted;
            } else if (type == QStringLiteral("dismissed") || type == QStringLiteral("rejected")) {
                ++dismissed;
            } else if (type == QStringLiteral("deferred")) {
                ++deferred;
            } else if (type == QStringLiteral("ignored") || type == QStringLiteral("expired")) {
                ++ignored;
            }
            ++total;
            supportScore += signalWeight(type);
        }

        const QString outcome = outcomeFor(supportScore, total);
        const QVariantMap score = {
            {QStringLiteral("episodeIndex"), episodeIndex},
            {QStringLiteral("action"), episode.value(QStringLiteral("action")).toString()},
            {QStringLiteral("mode"), episode.value(QStringLiteral("mode")).toString()},
            {QStringLiteral("reasonCode"), episode.value(QStringLiteral("reasonCode")).toString()},
            {QStringLiteral("fromVersion"), episode.value(QStringLiteral("fromVersion"))},
            {QStringLiteral("toVersion"), episode.value(QStringLiteral("toVersion"))},
            {QStringLiteral("createdAtMs"), startMs},
            {QStringLiteral("windowEndAtMs"), endMs == std::numeric_limits<qint64>::max() ? QVariant{} : QVariant(endMs)},
            {QStringLiteral("acceptedCount"), accepted},
            {QStringLiteral("dismissedCount"), dismissed},
            {QStringLiteral("deferredCount"), deferred},
            {QStringLiteral("ignoredCount"), ignored},
            {QStringLiteral("totalFeedbackCount"), total},
            {QStringLiteral("supportScore"), supportScore},
            {QStringLiteral("outcome"), outcome},
            {QStringLiteral("feedbackReasonCode"), reasonFor(outcome)}
        };
        scores.push_back(score);
    }
    return scores;
}
