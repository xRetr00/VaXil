#include <QtTest>

#include "cognition/ProactiveSuggestionPlanner.h"
#include "cognition/SuggestionProposalBuilder.h"
#include "cognition/SuggestionProposalRanker.h"

class SuggestionProposalPlannerTests : public QObject
{
    Q_OBJECT

private slots:
    void buildsRecoveryProposalForFailedResult();
    void ranksDocumentFollowUpHigherInFocusedEditorContext();
    void appliesFocusPenaltyToMediumProposal();
    void appliesCooldownPenaltyToMediumProposal();
    void boostsInboxConnectorProposalForDirectInboxSource();
    void boostsUpcomingScheduleConnectorProposal();
    void penalizesRecentlyPresentedDuplicateProposal();
    void penalizesConnectorBurstHistory();
    void buildsClipboardProposal();
    void buildsNotificationProposal();
    void buildsScheduleProposal();
    void buildsInboxProposal();
    void plansClipboardSuggestionWhenContextIsIdle();
    void suppressesPlannedSuggestionDuringFocusMode();
    void suppressesPlannedSuggestionDuringActiveCooldownSameThread();
    void allowsThreadShiftToBreakPlannerCooldown();
};

void SuggestionProposalPlannerTests::buildsRecoveryProposalForFailedResult()
{
    const QList<ActionProposal> proposals = SuggestionProposalBuilder::build({
        .sourceKind = QStringLiteral("task_result"),
        .taskType = QStringLiteral("file_search"),
        .resultSummary = QStringLiteral("The search failed."),
        .sourceUrls = {},
        .success = false
    });

    bool foundRecovery = false;
    for (const ActionProposal &proposal : proposals) {
        if (proposal.capabilityId == QStringLiteral("failure_recovery")) {
            foundRecovery = true;
            break;
        }
    }

    QVERIFY(foundRecovery);
}

void SuggestionProposalPlannerTests::ranksDocumentFollowUpHigherInFocusedEditorContext()
{
    const QList<ActionProposal> proposals = SuggestionProposalBuilder::build({
        .sourceKind = QStringLiteral("task_result"),
        .taskType = QStringLiteral("file_search"),
        .resultSummary = QStringLiteral("Found matching files."),
        .sourceUrls = {},
        .success = true
    });

    const QList<RankedSuggestionProposal> ranked = SuggestionProposalRanker::rank({
        .proposals = proposals,
        .desktopContext = {{QStringLiteral("taskId"), QStringLiteral("editor_document")}},
        .desktopContextAtMs = 1000,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = 1500
    });

    QVERIFY(!ranked.isEmpty());
    QCOMPARE(ranked.first().proposal.capabilityId, QStringLiteral("document_follow_up"));
}

void SuggestionProposalPlannerTests::appliesFocusPenaltyToMediumProposal()
{
    const QList<RankedSuggestionProposal> ranked = SuggestionProposalRanker::rank({
        .proposals = {
            ActionProposal{
                .proposalId = QStringLiteral("p1"),
                .capabilityId = QStringLiteral("source_review"),
                .title = QStringLiteral("Review sources"),
                .summary = QStringLiteral("I can review the sources."),
                .priority = QStringLiteral("medium")
            }
        },
        .desktopContext = {},
        .desktopContextAtMs = 0,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{.enabled = true},
        .nowMs = 1000
    });

    QVERIFY(!ranked.isEmpty());
    QCOMPARE(ranked.first().reasonCode, QStringLiteral("proposal_rank.focus_penalty"));
    QVERIFY(ranked.first().score < 0.68);
}

void SuggestionProposalPlannerTests::appliesCooldownPenaltyToMediumProposal()
{
    const QList<RankedSuggestionProposal> ranked = SuggestionProposalRanker::rank({
        .proposals = {
            ActionProposal{
                .proposalId = QStringLiteral("p1"),
                .capabilityId = QStringLiteral("source_review"),
                .title = QStringLiteral("Review sources"),
                .summary = QStringLiteral("I can review the sources."),
                .priority = QStringLiteral("medium")
            }
        },
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("browser_tab")},
            {QStringLiteral("threadId"), QStringLiteral("browser::research")}
        },
        .desktopContextAtMs = 1000,
        .cooldownState = CooldownState{
            .threadId = QStringLiteral("browser::research"),
            .activeUntilEpochMs = 3000
        },
        .focusMode = FocusModeState{},
        .nowMs = 1500
    });

    QVERIFY(!ranked.isEmpty());
    QCOMPARE(ranked.first().reasonCode, QStringLiteral("proposal_rank.cooldown_penalty"));
    QVERIFY(ranked.first().score < 0.68);
}

void SuggestionProposalPlannerTests::boostsInboxConnectorProposalForDirectInboxSource()
{
    const QList<RankedSuggestionProposal> ranked = SuggestionProposalRanker::rank({
        .proposals = SuggestionProposalBuilder::build({
            .sourceKind = QStringLiteral("connector_inbox_maildrop"),
            .taskType = QStringLiteral("email_fetch"),
            .resultSummary = QStringLiteral("Inbox updated: PR review requested from GitHub"),
            .sourceUrls = {},
            .success = true
        }),
        .sourceKind = QStringLiteral("connector_inbox_maildrop"),
        .taskType = QStringLiteral("email_fetch"),
        .sourceMetadata = {
            {QStringLiteral("connectorKind"), QStringLiteral("inbox")},
            {QStringLiteral("occurredAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("sender"), QStringLiteral("GitHub")}
        },
        .presentationKey = QStringLiteral("connector:inbox:message.eml"),
        .lastPresentedKey = QString(),
        .lastPresentedAtMs = 0,
        .desktopContext = {},
        .desktopContextAtMs = 0,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = QDateTime::currentMSecsSinceEpoch()
    });

    QVERIFY(!ranked.isEmpty());
    QCOMPARE(ranked.first().proposal.capabilityId, QStringLiteral("inbox_follow_up"));
    QVERIFY(ranked.first().score > 0.80);
}

void SuggestionProposalPlannerTests::boostsUpcomingScheduleConnectorProposal()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QDateTime startUtc = QDateTime::fromMSecsSinceEpoch(nowMs + (60 * 60 * 1000), Qt::UTC);

    const QList<RankedSuggestionProposal> ranked = SuggestionProposalRanker::rank({
        .proposals = SuggestionProposalBuilder::build({
            .sourceKind = QStringLiteral("connector_schedule_calendar"),
            .taskType = QStringLiteral("calendar_review"),
            .resultSummary = QStringLiteral("Schedule updated: Sprint planning"),
            .sourceUrls = {},
            .success = true
        }),
        .sourceKind = QStringLiteral("connector_schedule_calendar"),
        .taskType = QStringLiteral("calendar_review"),
        .sourceMetadata = {
            {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
            {QStringLiteral("eventUpcoming"), true},
            {QStringLiteral("eventStartUtc"), startUtc.toString(Qt::ISODateWithMs)},
            {QStringLiteral("occurredAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}
        },
        .presentationKey = QStringLiteral("connector:schedule:today.ics"),
        .lastPresentedKey = QString(),
        .lastPresentedAtMs = 0,
        .desktopContext = {},
        .desktopContextAtMs = 0,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = nowMs
    });

    QVERIFY(!ranked.isEmpty());
    QCOMPARE(ranked.first().proposal.capabilityId, QStringLiteral("schedule_follow_up"));
    QVERIFY(ranked.first().score > 0.84);
}

void SuggestionProposalPlannerTests::penalizesRecentlyPresentedDuplicateProposal()
{
    const QList<RankedSuggestionProposal> ranked = SuggestionProposalRanker::rank({
        .proposals = {
            ActionProposal{
                .proposalId = QStringLiteral("p1"),
                .capabilityId = QStringLiteral("inbox_follow_up"),
                .title = QStringLiteral("Triage messages"),
                .summary = QStringLiteral("I can summarize the important messages."),
                .priority = QStringLiteral("medium")
            }
        },
        .sourceKind = QStringLiteral("connector_inbox_maildrop"),
        .taskType = QStringLiteral("email_fetch"),
        .sourceMetadata = {
            {QStringLiteral("connectorKind"), QStringLiteral("inbox")},
            {QStringLiteral("occurredAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}
        },
        .presentationKey = QStringLiteral("connector:inbox:message.eml"),
        .lastPresentedKey = QStringLiteral("connector:inbox:message.eml"),
        .lastPresentedAtMs = QDateTime::currentMSecsSinceEpoch() - 30000,
        .desktopContext = {},
        .desktopContextAtMs = 0,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = QDateTime::currentMSecsSinceEpoch()
    });

    QVERIFY(!ranked.isEmpty());
    QCOMPARE(ranked.first().reasonCode, QStringLiteral("proposal_rank.recent_duplicate_penalty"));
    QVERIFY(ranked.first().score < 0.68);
}

void SuggestionProposalPlannerTests::penalizesConnectorBurstHistory()
{
    const QList<RankedSuggestionProposal> ranked = SuggestionProposalRanker::rank({
        .proposals = {
            ActionProposal{
                .proposalId = QStringLiteral("p1"),
                .capabilityId = QStringLiteral("inbox_follow_up"),
                .title = QStringLiteral("Triage messages"),
                .summary = QStringLiteral("I can summarize the important messages."),
                .priority = QStringLiteral("medium")
            }
        },
        .sourceKind = QStringLiteral("connector_inbox_maildrop"),
        .taskType = QStringLiteral("email_fetch"),
        .sourceMetadata = {
            {QStringLiteral("connectorKind"), QStringLiteral("inbox")},
            {QStringLiteral("historySeenCount"), 4},
            {QStringLiteral("connectorKindRecentSeenCount"), 5},
            {QStringLiteral("connectorKindRecentPresentedCount"), 2},
            {QStringLiteral("occurredAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}
        },
        .presentationKey = QStringLiteral("connector:inbox:message-4.eml"),
        .lastPresentedKey = QString(),
        .lastPresentedAtMs = 0,
        .desktopContext = {},
        .desktopContextAtMs = 0,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = QDateTime::currentMSecsSinceEpoch()
    });

    QVERIFY(!ranked.isEmpty());
    QCOMPARE(ranked.first().reasonCode, QStringLiteral("proposal_rank.connector_burst_penalty"));
    QVERIFY(ranked.first().score < 0.84);
}

void SuggestionProposalPlannerTests::buildsClipboardProposal()
{
    const QList<ActionProposal> proposals = SuggestionProposalBuilder::build({
        .sourceKind = QStringLiteral("desktop_context"),
        .taskType = QStringLiteral("clipboard"),
        .resultSummary = QStringLiteral("Copied text from the current app."),
        .sourceUrls = {},
        .success = true
    });

    bool foundClipboard = false;
    for (const ActionProposal &proposal : proposals) {
        if (proposal.capabilityId == QStringLiteral("clipboard_follow_up")) {
            foundClipboard = true;
            break;
        }
    }

    QVERIFY(foundClipboard);
}

void SuggestionProposalPlannerTests::buildsNotificationProposal()
{
    const QList<ActionProposal> proposals = SuggestionProposalBuilder::build({
        .sourceKind = QStringLiteral("desktop_context"),
        .taskType = QStringLiteral("notification"),
        .resultSummary = QStringLiteral("New notification appeared."),
        .sourceUrls = {},
        .success = true
    });

    bool foundNotification = false;
    for (const ActionProposal &proposal : proposals) {
        if (proposal.capabilityId == QStringLiteral("notification_triage")) {
            foundNotification = true;
            break;
        }
    }

    QVERIFY(foundNotification);
}

void SuggestionProposalPlannerTests::buildsScheduleProposal()
{
    const QList<ActionProposal> proposals = SuggestionProposalBuilder::build({
        .sourceKind = QStringLiteral("task_result"),
        .taskType = QStringLiteral("calendar_review"),
        .resultSummary = QStringLiteral("Upcoming meetings were collected."),
        .sourceUrls = {},
        .success = true
    });

    bool foundSchedule = false;
    for (const ActionProposal &proposal : proposals) {
        if (proposal.capabilityId == QStringLiteral("schedule_follow_up")) {
            foundSchedule = true;
            break;
        }
    }

    QVERIFY(foundSchedule);
}

void SuggestionProposalPlannerTests::buildsInboxProposal()
{
    const QList<ActionProposal> proposals = SuggestionProposalBuilder::build({
        .sourceKind = QStringLiteral("task_result"),
        .taskType = QStringLiteral("email_fetch"),
        .resultSummary = QStringLiteral("Unread email messages were fetched."),
        .sourceUrls = {},
        .success = true
    });

    bool foundInbox = false;
    for (const ActionProposal &proposal : proposals) {
        if (proposal.capabilityId == QStringLiteral("inbox_follow_up")) {
            foundInbox = true;
            break;
        }
    }

    QVERIFY(foundInbox);
}

void SuggestionProposalPlannerTests::plansClipboardSuggestionWhenContextIsIdle()
{
    const ProactiveSuggestionPlan plan = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("desktop_context"),
        .taskType = QStringLiteral("clipboard"),
        .resultSummary = QStringLiteral("Copied text from the current app."),
        .sourceUrls = {},
        .success = true,
        .desktopContext = {{QStringLiteral("taskId"), QStringLiteral("clipboard")}},
        .desktopContextAtMs = 1000,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = 1500
    });

    QVERIFY(!plan.generatedProposals.isEmpty());
    QVERIFY(!plan.rankedProposals.isEmpty());
    QVERIFY(plan.decision.allowed);
    QCOMPARE(plan.selectedProposal.capabilityId, QStringLiteral("clipboard_follow_up"));
    QVERIFY(!plan.selectedSummary.isEmpty());
}

void SuggestionProposalPlannerTests::suppressesPlannedSuggestionDuringFocusMode()
{
    const ProactiveSuggestionPlan plan = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("desktop_context"),
        .taskType = QStringLiteral("clipboard"),
        .resultSummary = QStringLiteral("Copied text from the current app."),
        .sourceUrls = {},
        .success = true,
        .desktopContext = {{QStringLiteral("taskId"), QStringLiteral("clipboard")}},
        .desktopContextAtMs = 1000,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{.enabled = true},
        .nowMs = 1500
    });

    QVERIFY(!plan.generatedProposals.isEmpty());
    QVERIFY(!plan.rankedProposals.isEmpty());
    QVERIFY(!plan.decision.allowed);
    QCOMPARE(plan.decision.reasonCode, QStringLiteral("proposal.focus_mode_suppressed"));
    QVERIFY(plan.selectedSummary.isEmpty());
}

void SuggestionProposalPlannerTests::suppressesPlannedSuggestionDuringActiveCooldownSameThread()
{
    const ProactiveSuggestionPlan plan = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("desktop_context"),
        .taskType = QStringLiteral("clipboard"),
        .resultSummary = QStringLiteral("Copied text from the current app."),
        .sourceUrls = {},
        .success = true,
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("clipboard")},
            {QStringLiteral("threadId"), QStringLiteral("clipboard::notes")},
            {QStringLiteral("topic"), QStringLiteral("notes")},
            {QStringLiteral("confidence"), 0.78}
        },
        .desktopContextAtMs = 1000,
        .cooldownState = CooldownState{
            .threadId = QStringLiteral("clipboard::notes"),
            .activeUntilEpochMs = 4000,
            .lastTopic = QStringLiteral("notes")
        },
        .focusMode = FocusModeState{},
        .nowMs = 1500
    });

    QVERIFY(!plan.decision.allowed);
    QCOMPARE(plan.cooldownDecision.reasonCode, QStringLiteral("cooldown.low_novelty"));
    QVERIFY(plan.selectedSummary.isEmpty());
}

void SuggestionProposalPlannerTests::allowsThreadShiftToBreakPlannerCooldown()
{
    const ProactiveSuggestionPlan plan = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("desktop_context"),
        .taskType = QStringLiteral("notification"),
        .resultSummary = QStringLiteral("New build notification appeared."),
        .sourceUrls = {},
        .success = true,
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("notification")},
            {QStringLiteral("threadId"), QStringLiteral("notification::builds")},
            {QStringLiteral("topic"), QStringLiteral("builds")},
            {QStringLiteral("confidence"), 0.82}
        },
        .desktopContextAtMs = 1000,
        .cooldownState = CooldownState{
            .threadId = QStringLiteral("editor::coding"),
            .activeUntilEpochMs = 4000,
            .lastTopic = QStringLiteral("coding")
        },
        .focusMode = FocusModeState{},
        .nowMs = 1500
    });

    QVERIFY(plan.decision.allowed);
    QCOMPARE(plan.cooldownDecision.reasonCode, QStringLiteral("cooldown.thread_shift"));
    QVERIFY(!plan.selectedSummary.isEmpty());
}

QTEST_APPLESS_MAIN(SuggestionProposalPlannerTests)
#include "SuggestionProposalPlannerTests.moc"
