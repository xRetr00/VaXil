#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

struct CompiledContextPolicyTuningPromotionDecision
{
    enum class Action {
        Hold,
        Promote,
        Rollback
    };

    Action action = Action::Hold;
    QString reasonCode = QStringLiteral("behavior_tuning.hold_default");
    QVariantMap nextState;
};

class CompiledContextPolicyTuningPromotionPolicy
{
public:
    [[nodiscard]] static CompiledContextPolicyTuningPromotionDecision evaluate(
        const QVariantMap &candidateState,
        const QVariantMap &persistedState,
        const QVariantList &persistedHistory,
        qint64 nowMs,
        const QVariantList &feedbackScores = {});
};
