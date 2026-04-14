#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "companion/contracts/CompanionContextSnapshot.h"
#include "companion/contracts/CooldownState.h"
#include "cognition/ProactiveSuggestionGate.h"
#include "cognition/SuggestionProposalRanker.h"

struct ProactiveSuggestionPlan
{
    QList<ActionProposal> generatedProposals;
    QList<RankedSuggestionProposal> rankedProposals;
    ActionProposal selectedProposal;
    CompanionContextSnapshot context;
    CooldownState nextCooldownState;
    BehaviorDecision cooldownDecision;
    BehaviorDecision decision;
    double confidenceScore = 0.0;
    double noveltyScore = 0.0;
    QString selectedSummary;

    [[nodiscard]] bool hasSelectedProposal() const
    {
        return !selectedProposal.summary.trimmed().isEmpty();
    }
};

class ProactiveSuggestionPlanner
{
public:
    struct Input
    {
        QString sourceKind;
        QString taskType;
        QString resultSummary;
        QStringList sourceUrls;
        bool success = true;
        QVariantMap desktopContext;
        qint64 desktopContextAtMs = 0;
        CooldownState cooldownState;
        FocusModeState focusMode;
        qint64 nowMs = 0;
    };

    [[nodiscard]] static ProactiveSuggestionPlan plan(const Input &input);
};
