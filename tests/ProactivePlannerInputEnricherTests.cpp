#include <QtTest>

#include <QDateTime>

#include "cognition/ProactivePlannerInputEnricher.h"
#include "cognition/ProactiveSuggestionPlanner.h"

class ProactivePlannerInputEnricherTests : public QObject
{
    Q_OBJECT

private slots:
    void enrichesDesktopEventMetadata();
    void enrichesConnectorChangeMetadata();
    void classifiesTaskResultPolicies();
    void plannerUsesEnrichedDesktopMetadata();
    void plannerSuppressesLowSignalTaskResult();
    void plannerAllowsFailureRecoveryTaskResult();
    void plannerEmitsUrgencyAndBurstEvidenceForConnectorBurstInputs();
};

void ProactivePlannerInputEnricherTests::enrichesDesktopEventMetadata()
{
    const QVariantMap enriched = ProactivePlannerInputEnricher::enrich({
        .sourceKind = QStringLiteral("desktop_context"),
        .taskType = QStringLiteral("active_window"),
        .resultSummary = QStringLiteral("User is reading Qt docs"),
        .sourceUrls = {},
        .sourceMetadata = {},
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("browser_tab")},
            {QStringLiteral("threadId"), QStringLiteral("browser::qt")},
            {QStringLiteral("documentContext"), QStringLiteral("Qt Test Reference")},
            {QStringLiteral("siteContext"), QStringLiteral("doc.qt.io")}
        },
        .nowMs = 1000,
        .success = true
    });

    QCOMPARE(enriched.value(QStringLiteral("plannerInputClass")).toString(),
             QStringLiteral("desktop_event"));
    QCOMPARE(enriched.value(QStringLiteral("taskId")).toString(),
             QStringLiteral("browser_tab"));
    QCOMPARE(enriched.value(QStringLiteral("sourceLabel")).toString(),
             QStringLiteral("Qt Test Reference"));
    QCOMPARE(enriched.value(QStringLiteral("presentationKeyHint")).toString(),
             QStringLiteral("browser::qt"));
    QVERIFY(enriched.value(QStringLiteral("plannerInputReasonCodes")).toStringList()
                .contains(QStringLiteral("desktop.browser_tab")));
}

void ProactivePlannerInputEnricherTests::enrichesConnectorChangeMetadata()
{
    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T15:05:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    const QVariantMap enriched = ProactivePlannerInputEnricher::enrich({
        .sourceKind = QStringLiteral("connector_schedule_calendar"),
        .taskType = QStringLiteral("live_update"),
        .resultSummary = QStringLiteral("Schedule updated: Sprint planning"),
        .sourceUrls = {},
        .sourceMetadata = {
            {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
            {QStringLiteral("eventTitle"), QStringLiteral("Sprint planning")},
            {QStringLiteral("occurredAtUtc"), QStringLiteral("2026-04-18T15:00:00.000Z")},
            {QStringLiteral("taskKey"), QStringLiteral("schedule:team")}
        },
        .desktopContext = {},
        .nowMs = nowMs,
        .success = true
    });

    QCOMPARE(enriched.value(QStringLiteral("plannerInputClass")).toString(),
             QStringLiteral("connector_change"));
    QCOMPARE(enriched.value(QStringLiteral("sourceLabel")).toString(),
             QStringLiteral("Sprint planning"));
    QCOMPARE(enriched.value(QStringLiteral("presentationKeyHint")).toString(),
             QStringLiteral("schedule:team"));
    QCOMPARE(enriched.value(QStringLiteral("eventFreshnessBand")).toString(),
             QStringLiteral("fresh"));
    QVERIFY(enriched.value(QStringLiteral("eventAgeMs")).toLongLong() > 0);
}

void ProactivePlannerInputEnricherTests::classifiesTaskResultPolicies()
{
    const QVariantMap research = ProactivePlannerInputEnricher::enrich({
        .sourceKind = QStringLiteral("task_result_surface"),
        .taskType = QStringLiteral("web_search"),
        .resultSummary = QStringLiteral("Found useful sources"),
        .sourceUrls = {QStringLiteral("https://example.test/source")},
        .sourceMetadata = {},
        .nowMs = 1000,
        .success = true
    });
    QCOMPARE(research.value(QStringLiteral("sourceSpecificPolicy")).toString(),
             QStringLiteral("research_task_result"));
    QCOMPARE(research.value(QStringLiteral("sourceUrlCount")).toInt(), 1);

    const QVariantMap lowSignal = ProactivePlannerInputEnricher::enrich({
        .sourceKind = QStringLiteral("task_result_surface"),
        .taskType = QStringLiteral("background_sync"),
        .resultSummary = QStringLiteral("Done"),
        .sourceUrls = {},
        .sourceMetadata = {},
        .nowMs = 1000,
        .success = true
    });
    QCOMPARE(lowSignal.value(QStringLiteral("sourceSpecificPolicy")).toString(),
             QStringLiteral("low_signal_task_result"));
    QCOMPARE(lowSignal.value(QStringLiteral("sourceSpecificSuppressionReason")).toString(),
             QStringLiteral("proposal.source_policy.low_signal_task_result"));
}

void ProactivePlannerInputEnricherTests::plannerUsesEnrichedDesktopMetadata()
{
    const ProactiveSuggestionPlan plan = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("desktop_context"),
        .taskType = QStringLiteral("active_window"),
        .resultSummary = QStringLiteral("User is reading Qt docs"),
        .sourceUrls = {},
        .sourceMetadata = {},
        .presentationKey = QString(),
        .lastPresentedKey = QString(),
        .lastPresentedAtMs = 0,
        .success = true,
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("browser_tab")},
            {QStringLiteral("threadId"), QStringLiteral("browser::qt")},
            {QStringLiteral("documentContext"), QStringLiteral("Qt Test Reference")},
            {QStringLiteral("siteContext"), QStringLiteral("doc.qt.io")}
        },
        .desktopContextAtMs = 1000,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = 1500
    });

    bool foundBrowserProposal = false;
    for (const ActionProposal &proposal : plan.generatedProposals) {
        if (proposal.capabilityId == QStringLiteral("browser_follow_up")) {
            foundBrowserProposal = true;
            QCOMPARE(proposal.arguments.value(QStringLiteral("sourceLabel")).toString(),
                     QStringLiteral("Qt Test Reference"));
            QCOMPARE(proposal.arguments.value(QStringLiteral("presentationKeyHint")).toString(),
                     QStringLiteral("browser::qt"));
            break;
        }
    }

    QVERIFY(foundBrowserProposal);
}

void ProactivePlannerInputEnricherTests::plannerSuppressesLowSignalTaskResult()
{
    const ProactiveSuggestionPlan plan = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("task_result_surface"),
        .taskType = QStringLiteral("background_sync"),
        .resultSummary = QStringLiteral("Done"),
        .sourceUrls = {},
        .sourceMetadata = {},
        .success = true,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = 1500
    });

    QVERIFY(!plan.decision.allowed);
    QCOMPARE(plan.decision.reasonCode,
             QStringLiteral("proposal.source_policy.low_signal_task_result"));
    QCOMPARE(plan.decision.details.value(QStringLiteral("sourceSpecificPolicy")).toString(),
             QStringLiteral("low_signal_task_result"));
    QVERIFY(plan.selectedSummary.isEmpty());
}

void ProactivePlannerInputEnricherTests::plannerAllowsFailureRecoveryTaskResult()
{
    const ProactiveSuggestionPlan plan = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("task_result_surface"),
        .taskType = QStringLiteral("file_search"),
        .resultSummary = QStringLiteral("The file search failed."),
        .sourceUrls = {},
        .sourceMetadata = {},
        .success = false,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = 1500
    });

    QVERIFY(plan.decision.allowed);
    QCOMPARE(plan.selectedProposal.capabilityId, QStringLiteral("failure_recovery"));
    QCOMPARE(plan.decision.details.value(QStringLiteral("sourceSpecificPolicy")).toString(),
             QStringLiteral("failure_recovery"));
    QVERIFY(!plan.selectedSummary.isEmpty());
}

void ProactivePlannerInputEnricherTests::plannerEmitsUrgencyAndBurstEvidenceForConnectorBurstInputs()
{
    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-19T00:50:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    const ProactiveSuggestionPlan plan = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("connector_schedule_calendar"),
        .taskType = QStringLiteral("live_update"),
        .resultSummary = QStringLiteral("Connector update failed for production deadline"),
        .sourceUrls = {},
        .sourceMetadata = {
            {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
            {QStringLiteral("eventTitle"), QStringLiteral("Production deadline")},
            {QStringLiteral("occurredAtUtc"), QStringLiteral("2026-04-18T06:30:00.000Z")},
            {QStringLiteral("historySeenCount"), 6},
            {QStringLiteral("connectorKindRecentSeenCount"), 5},
            {QStringLiteral("connectorKindRecentPresentedCount"), 3},
            {QStringLiteral("taskKey"), QStringLiteral("schedule:prod_deadline")}
        },
        .success = false,
        .cooldownState = CooldownState{
            .threadId = QStringLiteral("connector_event_toast::live_update"),
            .activeUntilEpochMs = nowMs + 120000
        },
        .focusMode = FocusModeState{},
        .nowMs = nowMs
    });

    QVERIFY(!plan.selectedProposal.proposalId.isEmpty());
    QCOMPARE(plan.selectedProposal.priority, QStringLiteral("high"));
    QCOMPARE(plan.selectedProposal.arguments.value(QStringLiteral("urgencyBand")).toString(),
             QStringLiteral("high"));
    QCOMPARE(plan.selectedProposal.arguments.value(QStringLiteral("burstPressureBand")).toString(),
             QStringLiteral("burst"));
    QCOMPARE(plan.cooldownDecision.details.value(QStringLiteral("urgencyBand")).toString(),
             QStringLiteral("high"));
    QCOMPARE(plan.cooldownDecision.details.value(QStringLiteral("burstPressureBand")).toString(),
             QStringLiteral("burst"));
    QCOMPARE(plan.cooldownDecision.details.value(QStringLiteral("connectorKindRecentSeenCount")).toInt(), 5);
    QCOMPARE(plan.cooldownDecision.details.value(QStringLiteral("connectorKindRecentPresentedCount")).toInt(), 3);
}

QTEST_APPLESS_MAIN(ProactivePlannerInputEnricherTests)
#include "ProactivePlannerInputEnricherTests.moc"
