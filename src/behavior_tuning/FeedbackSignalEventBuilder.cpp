#include "behavior_tuning/FeedbackSignalEventBuilder.h"

#include <QUuid>

FeedbackSignal FeedbackSignalEventBuilder::proactiveSuggestionSignal(const QString &signalType,
                                                                     const QString &suggestionType,
                                                                     const QString &message,
                                                                     const QString &threadId,
                                                                     qint64 nowMs)
{
    FeedbackSignal signal;
    signal.signalId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    signal.signalType = signalType.trimmed().isEmpty()
        ? QStringLiteral("unknown")
        : signalType.trimmed();
    signal.traceId = threadId.trimmed();
    signal.value = suggestionType.trimmed();
    signal.metadata = {
        {QStringLiteral("surfaceKind"), QStringLiteral("proactive_toast")},
        {QStringLiteral("suggestionType"), suggestionType.trimmed()},
        {QStringLiteral("message"), message.trimmed().left(600)},
        {QStringLiteral("occurredAtMs"), nowMs}
    };
    return signal;
}

BehaviorTraceEvent FeedbackSignalEventBuilder::behaviorEvent(const FeedbackSignal &signal)
{
    BehaviorTraceEvent event = BehaviorTraceEvent::create(
        QStringLiteral("feedback_signal"),
        signal.signalType,
        QStringLiteral("feedback.%1").arg(signal.signalType.trimmed()),
        signal.toVariantMap(),
        QStringLiteral("user"));
    event.traceId = signal.traceId;
    event.threadId = signal.traceId;
    event.capabilityId = QStringLiteral("behavior_tuning_feedback");
    return event;
}
