#include <QtTest>

#include <QSqlDatabase>
#include <QTemporaryDir>

#include "cognition/ProactiveCooldownTracker.h"
#include "cognition/ProactiveSuggestionPlanner.h"
#include "telemetry/BehavioralEventLedger.h"

class ProactiveSuggestionLoopScenarioTests : public QObject
{
    Q_OBJECT

private slots:
    void connectorSuggestionPresentThenDuplicateSuppress();
    void connectorFreshnessAffectsNovelty();
    void connectorBurstLowersCooldownNovelty();
    void connectorFlowRecordsReconstructableLedgerTrace();
    void focusedDesktopWorkSuppressesNonCriticalSuggestion();
};

namespace {
QVariantMap scheduleMetadata(const QString &occurredAtUtc = QStringLiteral("2026-04-18T14:58:00.000Z"))
{
    return {
        {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
        {QStringLiteral("eventTitle"), QStringLiteral("Sprint planning")},
        {QStringLiteral("eventStartUtc"), QStringLiteral("2026-04-18T15:00:00.000Z")},
        {QStringLiteral("occurredAtUtc"), occurredAtUtc},
        {QStringLiteral("taskKey"), QStringLiteral("schedule:team")}
    };
}
}

void ProactiveSuggestionLoopScenarioTests::connectorSuggestionPresentThenDuplicateSuppress()
{
    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T15:00:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();

    const ProactiveSuggestionPlan first = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("connector_schedule_calendar"),
        .taskType = QStringLiteral("live_update"),
        .resultSummary = QStringLiteral("Schedule updated: Sprint planning"),
        .sourceUrls = {},
        .sourceMetadata = scheduleMetadata(),
        .presentationKey = QStringLiteral("connector:schedule:team"),
        .lastPresentedKey = QString(),
        .lastPresentedAtMs = 0,
        .success = true,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = nowMs
    });

    QVERIFY(first.decision.allowed);
    QCOMPARE(first.selectedProposal.capabilityId, QStringLiteral("schedule_follow_up"));
    QCOMPARE(first.selectedProposal.arguments.value(QStringLiteral("sourceLabel")).toString(),
             QStringLiteral("Sprint planning"));
    QCOMPARE(first.selectedProposal.arguments.value(QStringLiteral("presentationKeyHint")).toString(),
             QStringLiteral("schedule:team"));
    QVERIFY(!first.selectedSummary.isEmpty());

    const ProactiveCooldownCommit commit = ProactiveCooldownTracker::commitPresentedSurface({
        .state = CooldownState{},
        .desktopContext = {},
        .taskType = QStringLiteral("live_update"),
        .surfaceKind = QStringLiteral("connector_event_toast"),
        .priority = QStringLiteral("medium"),
        .nowMs = nowMs
    });

    QVERIFY(commit.nextState.isActive(nowMs + 30000));
    QVERIFY(commit.nextState.threadId.contains(QStringLiteral("connector_event_toast")));

    const ProactiveSuggestionPlan duplicate = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("connector_schedule_calendar"),
        .taskType = QStringLiteral("live_update"),
        .resultSummary = QStringLiteral("Schedule updated: Sprint planning"),
        .sourceUrls = {},
        .sourceMetadata = scheduleMetadata(),
        .presentationKey = QStringLiteral("connector:schedule:team"),
        .lastPresentedKey = QStringLiteral("connector:schedule:team"),
        .lastPresentedAtMs = nowMs,
        .success = true,
        .cooldownState = commit.nextState,
        .focusMode = FocusModeState{},
        .nowMs = nowMs + 30000
    });

    QVERIFY(!duplicate.decision.allowed);
    QCOMPARE(duplicate.rankedProposals.first().reasonCode,
             QStringLiteral("proposal_rank.recent_duplicate_penalty"));
    QCOMPARE(duplicate.cooldownDecision.reasonCode, QStringLiteral("cooldown.low_novelty"));
    QVERIFY(duplicate.selectedSummary.isEmpty());
}

void ProactiveSuggestionLoopScenarioTests::connectorFreshnessAffectsNovelty()
{
    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T15:00:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();

    const ProactiveSuggestionPlan fresh = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("connector_schedule_calendar"),
        .taskType = QStringLiteral("live_update"),
        .resultSummary = QStringLiteral("Schedule updated: Sprint planning"),
        .sourceUrls = {},
        .sourceMetadata = scheduleMetadata(QStringLiteral("2026-04-18T14:58:00.000Z")),
        .success = true,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = nowMs
    });

    const ProactiveSuggestionPlan older = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("connector_schedule_calendar"),
        .taskType = QStringLiteral("live_update"),
        .resultSummary = QStringLiteral("Schedule updated: Sprint planning"),
        .sourceUrls = {},
        .sourceMetadata = scheduleMetadata(QStringLiteral("2026-04-18T08:00:00.000Z")),
        .success = true,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = nowMs
    });

    QVERIFY(fresh.decision.allowed);
    QVERIFY(older.decision.allowed);
    QVERIFY(fresh.noveltyScore > older.noveltyScore);
    QVERIFY(fresh.cooldownDecision.details.value(QStringLiteral("noveltyReasonCodes")).toStringList()
                .contains(QStringLiteral("novelty.fresh_connector_event")));
    QVERIFY(older.cooldownDecision.details.value(QStringLiteral("noveltyReasonCodes")).toStringList()
                .contains(QStringLiteral("novelty.older_connector_event")));
}

void ProactiveSuggestionLoopScenarioTests::connectorBurstLowersCooldownNovelty()
{
    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T15:00:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    QVariantMap metadata = scheduleMetadata();
    metadata.insert(QStringLiteral("historySeenCount"), 5);
    metadata.insert(QStringLiteral("connectorKindRecentSeenCount"), 5);
    metadata.insert(QStringLiteral("connectorKindRecentPresentedCount"), 2);

    const ProactiveSuggestionPlan plan = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("connector_schedule_calendar"),
        .taskType = QStringLiteral("live_update"),
        .resultSummary = QStringLiteral("Schedule updated: Sprint planning"),
        .sourceUrls = {},
        .sourceMetadata = metadata,
        .success = true,
        .cooldownState = CooldownState{
            .threadId = QStringLiteral("connector_event_toast::live_update"),
            .activeUntilEpochMs = nowMs + 90000
        },
        .focusMode = FocusModeState{},
        .nowMs = nowMs
    });

    QVERIFY(!plan.decision.allowed);
    QCOMPARE(plan.cooldownDecision.reasonCode, QStringLiteral("cooldown.low_novelty"));
    QVERIFY(plan.noveltyScore < 0.50);
    QVERIFY(plan.cooldownDecision.details.value(QStringLiteral("noveltyReasonCodes")).toStringList()
                .contains(QStringLiteral("novelty.connector_burst")));
}

void ProactiveSuggestionLoopScenarioTests::connectorFlowRecordsReconstructableLedgerTrace()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }

    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T15:00:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    const ProactiveSuggestionPlan plan = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("connector_schedule_calendar"),
        .taskType = QStringLiteral("live_update"),
        .resultSummary = QStringLiteral("Schedule updated: Sprint planning"),
        .sourceUrls = {},
        .sourceMetadata = scheduleMetadata(),
        .presentationKey = QStringLiteral("connector:schedule:team"),
        .lastPresentedKey = QString(),
        .lastPresentedAtMs = 0,
        .success = true,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = nowMs
    });

    QVERIFY(plan.decision.allowed);
    QVERIFY(!plan.selectedSummary.isEmpty());

    const ProactiveCooldownCommit commit = ProactiveCooldownTracker::commitPresentedSurface({
        .state = CooldownState{},
        .desktopContext = {
            {QStringLiteral("threadId"), QStringLiteral("calendar::sprint")},
            {QStringLiteral("taskId"), QStringLiteral("calendar_event")}
        },
        .taskType = QStringLiteral("live_update"),
        .surfaceKind = QStringLiteral("connector_event_toast"),
        .priority = plan.selectedProposal.priority,
        .nowMs = nowMs
    });

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BehavioralEventLedger ledger(dir.path(), true);
    QVERIFY(ledger.initialize());

    const QString traceId = QStringLiteral("trace_connector_schedule_flow");
    const QString threadId = commit.nextState.threadId;
    BehaviorTraceEvent ingested = BehaviorTraceEvent::create(
        QStringLiteral("connector_event"),
        QStringLiteral("ingested"),
        QStringLiteral("connector_event.ingested"),
        {
            {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
            {QStringLiteral("eventTitle"), QStringLiteral("Sprint planning")}
        });
    ingested.traceId = traceId;
    ingested.sessionId = QStringLiteral("session_proactive_loop");
    ingested.threadId = threadId;
    ingested.capabilityId = QStringLiteral("connector_schedule_calendar");
    ingested.timestampUtc = QDateTime::fromMSecsSinceEpoch(nowMs, Qt::UTC);

    BehaviorTraceEvent proposal = BehaviorTraceEvent::create(
        QStringLiteral("action_proposal"),
        QStringLiteral("selected"),
        QStringLiteral("proposal.selected"),
        {
            {QStringLiteral("proposalId"), plan.selectedProposal.proposalId},
            {QStringLiteral("capabilityId"), plan.selectedProposal.capabilityId},
            {QStringLiteral("sourceLabel"), plan.selectedProposal.arguments.value(QStringLiteral("sourceLabel")).toString()},
            {QStringLiteral("presentationKeyHint"), plan.selectedProposal.arguments.value(QStringLiteral("presentationKeyHint")).toString()}
        });
    proposal.traceId = traceId;
    proposal.sessionId = ingested.sessionId;
    proposal.threadId = threadId;
    proposal.capabilityId = plan.selectedProposal.capabilityId;
    proposal.timestampUtc = ingested.timestampUtc.addMSecs(1);

    BehaviorTraceEvent cooldown = BehaviorTraceEvent::create(
        QStringLiteral("cooldown"),
        QStringLiteral("evaluated"),
        plan.cooldownDecision.reasonCode,
        {
            {QStringLiteral("novelty"), plan.noveltyScore},
            {QStringLiteral("confidence"), plan.confidenceScore},
            {QStringLiteral("noveltyReasonCodes"), plan.cooldownDecision.details.value(QStringLiteral("noveltyReasonCodes")).toStringList()}
        });
    cooldown.traceId = traceId;
    cooldown.sessionId = ingested.sessionId;
    cooldown.threadId = threadId;
    cooldown.capabilityId = QStringLiteral("cooldown_engine");
    cooldown.timestampUtc = ingested.timestampUtc.addMSecs(2);

    BehaviorTraceEvent presented = BehaviorTraceEvent::create(
        QStringLiteral("ui_presentation"),
        QStringLiteral("presented"),
        QStringLiteral("proposal.presented"),
        {
            {QStringLiteral("proposalId"), plan.selectedProposal.proposalId},
            {QStringLiteral("surfaceKind"), QStringLiteral("connector_event_toast")},
            {QStringLiteral("summary"), plan.selectedSummary}
        });
    presented.traceId = traceId;
    presented.sessionId = ingested.sessionId;
    presented.threadId = threadId;
    presented.capabilityId = plan.selectedProposal.capabilityId;
    presented.timestampUtc = ingested.timestampUtc.addMSecs(3);

    QVERIFY(ledger.recordEvent(ingested));
    QVERIFY(ledger.recordEvent(proposal));
    QVERIFY(ledger.recordEvent(cooldown));
    QVERIFY(ledger.recordEvent(presented));

    const QList<BehaviorTraceEvent> events = ledger.recentEvents(8);
    QCOMPARE(events.size(), 4);

    bool sawIngested = false;
    bool sawProposal = false;
    bool sawCooldown = false;
    bool sawPresented = false;
    for (const BehaviorTraceEvent &event : events) {
        QCOMPARE(event.traceId, traceId);
        QCOMPARE(event.threadId, threadId);
        if (event.family == QStringLiteral("connector_event")) {
            sawIngested = true;
        } else if (event.family == QStringLiteral("action_proposal")) {
            sawProposal = true;
        } else if (event.family == QStringLiteral("cooldown")) {
            sawCooldown = true;
            QVERIFY(!event.payload.value(QStringLiteral("noveltyReasonCodes")).toStringList().isEmpty());
        } else if (event.family == QStringLiteral("ui_presentation")) {
            sawPresented = true;
            QCOMPARE(event.payload.value(QStringLiteral("proposalId")).toString(),
                     plan.selectedProposal.proposalId);
        }
    }

    QVERIFY(sawIngested);
    QVERIFY(sawProposal);
    QVERIFY(sawCooldown);
    QVERIFY(sawPresented);
}

void ProactiveSuggestionLoopScenarioTests::focusedDesktopWorkSuppressesNonCriticalSuggestion()
{
    const ProactiveSuggestionPlan plan = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("desktop_context"),
        .taskType = QStringLiteral("active_window"),
        .resultSummary = QStringLiteral("User is editing MainWindow.cpp"),
        .sourceUrls = {},
        .sourceMetadata = {},
        .presentationKey = QString(),
        .lastPresentedKey = QString(),
        .lastPresentedAtMs = 0,
        .success = true,
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("editor_document")},
            {QStringLiteral("threadId"), QStringLiteral("editor::vaxil")},
            {QStringLiteral("documentContext"), QStringLiteral("MainWindow.cpp")},
            {QStringLiteral("workspaceContext"), QStringLiteral("Vaxil")}
        },
        .desktopContextAtMs = 1000,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = 1500
    });

    QVERIFY(!plan.decision.allowed);
    QCOMPARE(plan.decision.reasonCode, QStringLiteral("proposal.focused_context_suppressed"));
    QCOMPARE(plan.selectedProposal.arguments.value(QStringLiteral("sourceLabel")).toString(),
             QStringLiteral("MainWindow.cpp"));
    QVERIFY(plan.selectedSummary.isEmpty());
}

QTEST_APPLESS_MAIN(ProactiveSuggestionLoopScenarioTests)
#include "ProactiveSuggestionLoopScenarioTests.moc"
