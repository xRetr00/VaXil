#include <QtTest>

#include "cognition/ProactiveSuggestionGate.h"

class ProactiveSuggestionGateTests : public QObject
{
    Q_OBJECT

private slots:
    void suppressesMediumProposalDuringFocusMode();
    void suppressesProposalDuringFocusedDesktopWork();
    void suppressesProposalDuringLayeredPolicyDefocus();
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
