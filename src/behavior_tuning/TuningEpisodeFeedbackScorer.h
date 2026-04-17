#pragma once

#include <QVariantList>

class TuningEpisodeFeedbackScorer
{
public:
    [[nodiscard]] static QVariantList score(const QVariantList &episodes,
                                            const QVariantList &feedbackSignals);
};
