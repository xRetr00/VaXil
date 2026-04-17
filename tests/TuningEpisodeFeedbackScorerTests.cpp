#include <QtTest>

#include "behavior_tuning/TuningEpisodeFeedbackScorer.h"

class TuningEpisodeFeedbackScorerTests : public QObject
{
    Q_OBJECT

private slots:
    void scoresFeedbackInsideEpisodeWindows();
    void leavesEpisodeUnscoredWhenNoFeedbackMatches();
};

void TuningEpisodeFeedbackScorerTests::scoresFeedbackInsideEpisodeWindows()
{
    const QVariantList episodes = {
        QVariantMap{
            {QStringLiteral("action"), QStringLiteral("promote")},
            {QStringLiteral("mode"), QStringLiteral("document_work")},
            {QStringLiteral("reasonCode"), QStringLiteral("behavior_tuning.promote_bootstrap")},
            {QStringLiteral("fromVersion"), 0},
            {QStringLiteral("toVersion"), 1},
            {QStringLiteral("createdAtMs"), 1000}
        },
        QVariantMap{
            {QStringLiteral("action"), QStringLiteral("promote")},
            {QStringLiteral("mode"), QStringLiteral("research_analysis")},
            {QStringLiteral("reasonCode"), QStringLiteral("behavior_tuning.promote_mode_shift")},
            {QStringLiteral("fromVersion"), 1},
            {QStringLiteral("toVersion"), 2},
            {QStringLiteral("createdAtMs"), 2000}
        }
    };
    const QVariantList feedback = {
        QVariantMap{{QStringLiteral("signalType"), QStringLiteral("accepted")},
                    {QStringLiteral("occurredAtMs"), 1200}},
        QVariantMap{{QStringLiteral("signalType"), QStringLiteral("dismissed")},
                    {QStringLiteral("occurredAtMs"), 1500}},
        QVariantMap{{QStringLiteral("signalType"), QStringLiteral("accepted")},
                    {QStringLiteral("occurredAtMs"), 2100}},
        QVariantMap{{QStringLiteral("signalType"), QStringLiteral("ignored")},
                    {QStringLiteral("occurredAtMs"), 2200}}
    };

    const QVariantList scores = TuningEpisodeFeedbackScorer::score(episodes, feedback);
    QCOMPARE(scores.size(), 2);
    QCOMPARE(scores.first().toMap().value(QStringLiteral("totalFeedbackCount")).toInt(), 2);
    QCOMPARE(scores.first().toMap().value(QStringLiteral("supportScore")).toDouble(), 0.0);
    QCOMPARE(scores.first().toMap().value(QStringLiteral("outcome")).toString(),
             QStringLiteral("mixed"));
    QCOMPARE(scores.last().toMap().value(QStringLiteral("acceptedCount")).toInt(), 1);
    QCOMPARE(scores.last().toMap().value(QStringLiteral("ignoredCount")).toInt(), 1);
    QCOMPARE(scores.last().toMap().value(QStringLiteral("supportScore")).toDouble(), 0.5);
}

void TuningEpisodeFeedbackScorerTests::leavesEpisodeUnscoredWhenNoFeedbackMatches()
{
    const QVariantList scores = TuningEpisodeFeedbackScorer::score({
        QVariantMap{{QStringLiteral("action"), QStringLiteral("promote")},
                    {QStringLiteral("mode"), QStringLiteral("document_work")},
                    {QStringLiteral("createdAtMs"), 1000}}
    }, {});

    QCOMPARE(scores.size(), 1);
    QCOMPARE(scores.first().toMap().value(QStringLiteral("outcome")).toString(),
             QStringLiteral("unscored"));
    QCOMPARE(scores.first().toMap().value(QStringLiteral("feedbackReasonCode")).toString(),
             QStringLiteral("behavior_tuning.feedback_unscored"));
}

QTEST_APPLESS_MAIN(TuningEpisodeFeedbackScorerTests)
#include "TuningEpisodeFeedbackScorerTests.moc"
