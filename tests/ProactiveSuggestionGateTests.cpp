#include <QtTest>

#include "cognition/ProactiveSuggestionGate.h"

class ProactiveSuggestionGateTests : public QObject
{
    Q_OBJECT

private slots:
    void suppressesMediumProposalDuringFocusMode();
    void suppressesProposalDuringFocusedDesktopWork();
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
