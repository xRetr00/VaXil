#include <QtTest>

#include "cognition/SuggestionProposalRanker.h"

class SuggestionProposalHistoryPolicyTests : public QObject
{
    Q_OBJECT

private slots:
    void structuresProposalRankingFromCompiledHistoryMode();
    void boostsProposalFromCompiledPolicyFocusSummary();
    void boostsProposalFromCompiledPolicySourceSummary();
    void boostsProposalFromCompiledLayeredSignals();
    void penalizesProposalFromCompiledLayeredDefocus();
};

void SuggestionProposalHistoryPolicyTests::structuresProposalRankingFromCompiledHistoryMode()
{
    const QList<RankedSuggestionProposal> ranked = SuggestionProposalRanker::rank({
        .proposals = {
            ActionProposal{
                .proposalId = QStringLiteral("p1"),
                .capabilityId = QStringLiteral("document_follow_up"),
                .title = QStringLiteral("Review document"),
                .summary = QStringLiteral("I can stay on the current document."),
                .priority = QStringLiteral("medium")
            },
            ActionProposal{
                .proposalId = QStringLiteral("p2"),
                .capabilityId = QStringLiteral("inbox_follow_up"),
                .title = QStringLiteral("Review inbox"),
                .summary = QStringLiteral("I can triage the inbox."),
                .priority = QStringLiteral("medium")
            }
        },
        .sourceMetadata = {
            {QStringLiteral("compiledContextHistoryMode"), QStringLiteral("document_work")},
            {QStringLiteral("compiledContextHistoryModeStrength"), 2.6}
        },
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("editor_document")}
        },
        .desktopContextAtMs = 1000,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = 1500
    });

    QVERIFY(ranked.size() >= 2);
    QCOMPARE(ranked.first().proposal.capabilityId, QStringLiteral("document_follow_up"));
    QCOMPARE(ranked.first().reasonCode, QStringLiteral("proposal_rank.compiled_history_document_structure"));
    QVERIFY(ranked.first().score > ranked.last().score);
}

void SuggestionProposalHistoryPolicyTests::boostsProposalFromCompiledPolicyFocusSummary()
{
    const QList<RankedSuggestionProposal> ranked = SuggestionProposalRanker::rank({
        .proposals = {
            ActionProposal{
                .proposalId = QStringLiteral("p1"),
                .capabilityId = QStringLiteral("inbox_follow_up"),
                .title = QStringLiteral("Review inbox"),
                .summary = QStringLiteral("I can triage the inbox."),
                .priority = QStringLiteral("medium")
            },
            ActionProposal{
                .proposalId = QStringLiteral("p2"),
                .capabilityId = QStringLiteral("document_follow_up"),
                .title = QStringLiteral("Review document"),
                .summary = QStringLiteral("I can stay on the current document."),
                .priority = QStringLiteral("medium")
            }
        },
        .sourceMetadata = {
            {QStringLiteral("compiledContextPolicySummaryKeys"),
             QStringList{
                 QStringLiteral("compiled_context_policy_summary"),
                 QStringLiteral("compiled_context_policy_focus")
             }},
            {QStringLiteral("compiledContextPolicySummary"),
             QStringLiteral("Long-horizon policy mode inbox_triage. Prioritize message review, summarization, and reply preparation.")}
        },
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = 1500
    });

    QVERIFY(ranked.size() >= 2);
    QCOMPARE(ranked.first().proposal.capabilityId, QStringLiteral("inbox_follow_up"));
    QCOMPARE(ranked.first().reasonCode, QStringLiteral("proposal_rank.policy_focus_inbox"));
    QVERIFY(ranked.first().score > ranked.last().score);
}

void SuggestionProposalHistoryPolicyTests::boostsProposalFromCompiledPolicySourceSummary()
{
    const QList<RankedSuggestionProposal> ranked = SuggestionProposalRanker::rank({
        .proposals = {
            ActionProposal{
                .proposalId = QStringLiteral("p1"),
                .capabilityId = QStringLiteral("schedule_follow_up"),
                .title = QStringLiteral("Review schedule"),
                .summary = QStringLiteral("I can summarize the current schedule."),
                .priority = QStringLiteral("medium")
            },
            ActionProposal{
                .proposalId = QStringLiteral("p2"),
                .capabilityId = QStringLiteral("inbox_follow_up"),
                .title = QStringLiteral("Review inbox"),
                .summary = QStringLiteral("I can triage the inbox."),
                .priority = QStringLiteral("medium")
            }
        },
        .sourceMetadata = {
            {QStringLiteral("compiledContextPolicySummaryKeys"),
             QStringList{
                 QStringLiteral("compiled_context_policy_summary"),
                 QStringLiteral("compiled_context_policy_sources")
             }},
            {QStringLiteral("compiledContextPolicySummary"),
             QStringLiteral("schedule source continuity: seen 4 times, surfaced 2 times, sources connector_schedule_calendar.")}
        },
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = 1500
    });

    QVERIFY(ranked.size() >= 2);
    QCOMPARE(ranked.first().proposal.capabilityId, QStringLiteral("schedule_follow_up"));
    QCOMPARE(ranked.first().reasonCode, QStringLiteral("proposal_rank.policy_source_schedule"));
    QVERIFY(ranked.first().score > ranked.last().score);
}

void SuggestionProposalHistoryPolicyTests::boostsProposalFromCompiledLayeredSignals()
{
    const QList<RankedSuggestionProposal> ranked = SuggestionProposalRanker::rank({
        .proposals = {
            ActionProposal{
                .proposalId = QStringLiteral("p1"),
                .capabilityId = QStringLiteral("source_review"),
                .title = QStringLiteral("Review sources"),
                .summary = QStringLiteral("I can synthesize the recent sources."),
                .priority = QStringLiteral("medium")
            },
            ActionProposal{
                .proposalId = QStringLiteral("p2"),
                .capabilityId = QStringLiteral("inbox_follow_up"),
                .title = QStringLiteral("Review inbox"),
                .summary = QStringLiteral("I can triage the inbox."),
                .priority = QStringLiteral("medium")
            }
        },
        .sourceMetadata = {
            {QStringLiteral("compiledContextLayeredKeys"),
             QStringList{
                 QStringLiteral("compiled_context_layered_focus"),
                 QStringLiteral("compiled_context_layered_continuity")
             }},
            {QStringLiteral("compiledContextLayeredSummary"),
             QStringLiteral("Prompt steering: Stable mode: research analysis remains active. Dominant continuity source research: seen 5 times, surfaced 2 times, sources connector_research_browser.")}
        },
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = 1500
    });

    QVERIFY(ranked.size() >= 2);
    QCOMPARE(ranked.first().proposal.capabilityId, QStringLiteral("source_review"));
    QCOMPARE(ranked.first().reasonCode, QStringLiteral("proposal_rank.layered_focus_research"));
    QVERIFY(ranked.first().score > ranked.last().score);
}

void SuggestionProposalHistoryPolicyTests::penalizesProposalFromCompiledLayeredDefocus()
{
    const QList<RankedSuggestionProposal> ranked = SuggestionProposalRanker::rank({
        .proposals = {
            ActionProposal{
                .proposalId = QStringLiteral("p1"),
                .capabilityId = QStringLiteral("inbox_follow_up"),
                .title = QStringLiteral("Review inbox"),
                .summary = QStringLiteral("I can triage the inbox."),
                .priority = QStringLiteral("medium")
            },
            ActionProposal{
                .proposalId = QStringLiteral("p2"),
                .capabilityId = QStringLiteral("source_review"),
                .title = QStringLiteral("Review sources"),
                .summary = QStringLiteral("I can synthesize the recent sources."),
                .priority = QStringLiteral("medium")
            }
        },
        .sourceMetadata = {
            {QStringLiteral("compiledContextLayeredKeys"),
             QStringList{
                 QStringLiteral("compiled_context_layered_focus"),
                 QStringLiteral("compiled_context_layered_continuity")
             }},
            {QStringLiteral("compiledContextLayeredSummary"),
             QStringLiteral("Prompt steering: Stable mode: research analysis remains active. Dominant continuity source research: seen 5 times, surfaced 2 times, sources connector_research_browser.")}
        },
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = 1500
    });

    QVERIFY(ranked.size() >= 2);
    QCOMPARE(ranked.last().proposal.capabilityId, QStringLiteral("inbox_follow_up"));
    QCOMPARE(ranked.last().reasonCode, QStringLiteral("proposal_rank.layered_focus_research_defocus"));
    QVERIFY(ranked.first().score > ranked.last().score);
}

QTEST_APPLESS_MAIN(SuggestionProposalHistoryPolicyTests)
#include "SuggestionProposalHistoryPolicyTests.moc"
