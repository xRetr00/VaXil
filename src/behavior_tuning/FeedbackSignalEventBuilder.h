#pragma once

#include <QString>

#include "companion/contracts/BehaviorTraceEvent.h"
#include "companion/contracts/FeedbackSignal.h"

class FeedbackSignalEventBuilder
{
public:
    [[nodiscard]] static FeedbackSignal proactiveSuggestionSignal(const QString &signalType,
                                                                  const QString &suggestionType,
                                                                  const QString &message,
                                                                  const QString &threadId,
                                                                  qint64 nowMs);
    [[nodiscard]] static BehaviorTraceEvent behaviorEvent(const FeedbackSignal &signal);
};
