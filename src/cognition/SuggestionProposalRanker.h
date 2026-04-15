#pragma once

#include <QList>
#include <QVariantMap>

#include "companion/contracts/ActionProposal.h"
#include "companion/contracts/CooldownState.h"
#include "companion/contracts/FocusModeState.h"

struct RankedSuggestionProposal
{
    ActionProposal proposal;
    double score = 0.0;
    QString reasonCode;

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        QVariantMap map = proposal.toVariantMap();
        map.insert(QStringLiteral("score"), score);
        map.insert(QStringLiteral("reasonCode"), reasonCode);
        return map;
    }
};

class SuggestionProposalRanker
{
public:
    struct Input
    {
        QList<ActionProposal> proposals;
        QString sourceKind;
        QString taskType;
        QVariantMap sourceMetadata;
        QString presentationKey;
        QString lastPresentedKey;
        qint64 lastPresentedAtMs = 0;
        QVariantMap desktopContext;
        qint64 desktopContextAtMs = 0;
        CooldownState cooldownState;
        FocusModeState focusMode;
        qint64 nowMs = 0;
    };

    [[nodiscard]] static QList<RankedSuggestionProposal> rank(const Input &input);

private:
    [[nodiscard]] static double priorityScore(const QString &priority);
    [[nodiscard]] static bool hasFreshDesktopContext(const Input &input);
    [[nodiscard]] static bool hasMeaningfulThreadShift(const Input &input);
};
