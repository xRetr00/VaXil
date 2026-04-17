#include <QtTest>

#include "behavior_tuning/FeedbackSignalEventBuilder.h"

class FeedbackSignalEventBuilderTests : public QObject
{
    Q_OBJECT

private slots:
    void buildsProactiveSuggestionFeedbackEvent();
};

void FeedbackSignalEventBuilderTests::buildsProactiveSuggestionFeedbackEvent()
{
    const FeedbackSignal signal = FeedbackSignalEventBuilder::proactiveSuggestionSignal(
        QStringLiteral("accepted"),
        QStringLiteral("proactive_connector"),
        QStringLiteral("Review the new inbox item"),
        QStringLiteral("connector:inbox:1"),
        12345);

    QCOMPARE(signal.signalType, QStringLiteral("accepted"));
    QCOMPARE(signal.value, QStringLiteral("proactive_connector"));
    QCOMPARE(signal.metadata.value(QStringLiteral("surfaceKind")).toString(),
             QStringLiteral("proactive_toast"));

    const BehaviorTraceEvent event = FeedbackSignalEventBuilder::behaviorEvent(signal);
    QCOMPARE(event.family, QStringLiteral("feedback_signal"));
    QCOMPARE(event.stage, QStringLiteral("accepted"));
    QCOMPARE(event.reasonCode, QStringLiteral("feedback.accepted"));
    QCOMPARE(event.actor, QStringLiteral("user"));
    QCOMPARE(event.capabilityId, QStringLiteral("behavior_tuning_feedback"));
    QCOMPARE(event.threadId, QStringLiteral("connector:inbox:1"));
}

QTEST_APPLESS_MAIN(FeedbackSignalEventBuilderTests)
#include "FeedbackSignalEventBuilderTests.moc"
