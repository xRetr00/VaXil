#include <QtTest>

#include "cognition/ProactiveSuggestionGate.h"

class ProactiveSuggestionGateTests : public QObject
{
    Q_OBJECT

private slots:
    void suppressesMediumProposalDuringFocusMode();
    void suppressesProposalDuringFocusedDesktopWork();
    void suppressesProposalDuringLayeredPolicyDefocus();
    void suppressesProposalDuringEvolutionDefocus();
    void suppressesProposalDuringTuningVolatilityDefocus();
    void allowsHighPriorityProposal();
};

void ProactiveSuggestionGateTests::suppressesMediumProposalDuringFocusMode()
{
    ProactiveSuggestionGate::Input input;
    input.proposal.priority = QStringLiteral("medium");
    input.focusMode.enabled = true;

    const BehaviorDecision decision = ProactiveSuggestionGate::evaluate(input);
    QVERIFY(!decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("proposal.focus_mode_suppressed"));
}

void ProactiveSuggestionGateTests::suppressesProposalDuringFocusedDesktopWork()
{
    ProactiveSuggestionGate::Input input;
    input.proposal.priority = QStringLiteral("medium");
    input.desktopContext.insert(QStringLiteral("taskId"), QStringLiteral("editor_document"));
    input.desktopContextAtMs = 1000;
    input.nowMs = 1500;

    const BehaviorDecision decision = ProactiveSuggestionGate::evaluate(input);
    QVERIFY(!decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("proposal.focused_context_suppressed"));
}

void ProactiveSuggestionGateTests::suppressesProposalDuringLayeredPolicyDefocus()
{
    ProactiveSuggestionGate::Input input;
    input.proposal.priority = QStringLiteral("medium");
    input.proposal.capabilityId = QStringLiteral("inbox_follow_up");
    input.sourceMetadata.insert(
        QStringLiteral("compiledContextLayeredSummary"),
        QStringLiteral("Prompt steering: Stable mode: research analysis remains active. Dominant continuity source research: seen 5 times, surfaced 2 times, sources connector_research_browser."));

    const BehaviorDecision decision = ProactiveSuggestionGate::evaluate(input);
    QVERIFY(!decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("proposal.layered_policy_research_defocus"));
}

void ProactiveSuggestionGateTests::suppressesProposalDuringEvolutionDefocus()
{
    ProactiveSuggestionGate::Input input;
    input.proposal.priority = QStringLiteral("medium");
    input.proposal.capabilityId = QStringLiteral("inbox_follow_up");
    input.sourceMetadata.insert(
        QStringLiteral("compiledContextEvolutionSummary"),
        QStringLiteral("Policy evolution: document_work -> research_analysis. Current mode research_analysis observed 3 times across 2 mode shifts."));

    const BehaviorDecision decision = ProactiveSuggestionGate::evaluate(input);
    QVERIFY(!decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("proposal.evolution_research_defocus"));
}

void ProactiveSuggestionGateTests::suppressesProposalDuringTuningVolatilityDefocus()
{
    ProactiveSuggestionGate::Input input;
    input.proposal.priority = QStringLiteral("medium");
    input.proposal.capabilityId = QStringLiteral("inbox_follow_up");
    input.proposalScore = 0.65;
    input.sourceMetadata.insert(
        QStringLiteral("compiledContextTuningSummary"),
        QStringLiteral("Tuning signal: policy volatility elevated after 3 mode shifts across 8 total observations; current mode research_analysis. Policy volatility: elevated. Use stricter confidence and stronger defocus control when medium-priority proposals conflict with current mode research_analysis. Policy stability bias: research_analysis sustained for 3 observations. Increase alignment bias for research analysis and trim medium-priority defocused suggestions."));
    input.sourceMetadata.insert(QStringLiteral("tuningSuppressionScoreThreshold"), 0.70);

    const BehaviorDecision decision = ProactiveSuggestionGate::evaluate(input);
    QVERIFY(!decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("proposal.tuning_volatility_research_defocus"));
}

void ProactiveSuggestionGateTests::allowsHighPriorityProposal()
{
    ProactiveSuggestionGate::Input input;
    input.proposal.priority = QStringLiteral("high");
    input.focusMode.enabled = true;

    const BehaviorDecision decision = ProactiveSuggestionGate::evaluate(input);
    QVERIFY(decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("proposal.high_priority_allow"));
}

QTEST_APPLESS_MAIN(ProactiveSuggestionGateTests)
#include "ProactiveSuggestionGateTests.moc"
