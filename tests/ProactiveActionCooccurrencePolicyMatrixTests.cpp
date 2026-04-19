#include <QtTest>

#include <QSqlDatabase>
#include <QTemporaryDir>

#include "cognition/CooldownEngine.h"
#include "cognition/ProactiveSuggestionPlanner.h"
#include "core/ActionRiskPermissionService.h"
#include "telemetry/BehavioralEventLedger.h"

class ProactiveActionCooccurrencePolicyMatrixTests : public QObject
{
    Q_OBJECT

private slots:
    void cooccurrenceMatrixCoversSuppressDeferWithDeniedCanceled();
    void cooccurrenceReasonCodesRemainStable();
    void connectorNoveltyPenaltyThenDeniedActionSharesTrace();
    void breakCooldownFastPathStillRunsPermissionAndConfirmationGates();
    void mixedPriorityConnectorBurstCrossesRankingCooldownAndActionOrdering();
};

namespace {
QVariantMap mixedDesktopConnectorContext()
{
    return {
        {QStringLiteral("threadId"), QStringLiteral("desktop::editor_document::calendar::sprint_plan")},
        {QStringLiteral("taskId"), QStringLiteral("editor_document")},
        {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
        {QStringLiteral("eventTitle"), QStringLiteral("Sprint planning")}
    };
}

ToolPlan sideEffectingPlan()
{
    ToolPlan plan;
    plan.sideEffecting = true;
    plan.orderedToolNames = {QStringLiteral("file_patch"), QStringLiteral("web_search")};
    return plan;
}

TrustDecision highRiskTrust()
{
    TrustDecision trust;
    trust.highRisk = true;
    trust.requiresConfirmation = true;
    trust.contextReasonCode = QStringLiteral("action_policy.mixed_context_confirmation");
    trust.desktopWorkMode = QStringLiteral("focused");
    trust.reason = QStringLiteral("Mixed editor + connector context");
    return trust;
}
}

void ProactiveActionCooccurrencePolicyMatrixTests::cooccurrenceMatrixCoversSuppressDeferWithDeniedCanceled()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }
    const QVariantMap desktopContext = mixedDesktopConnectorContext();
    const ActionRiskPermissionEvaluation pending = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), false, {});

    struct Row {
        QString entryMode;
        QString outcome;
        QString expectedEntryReason;
    };
    const QList<Row> rows = {
        {QStringLiteral("suppress"), QStringLiteral("denied"), QStringLiteral("proposal.focused_context_suppressed")},
        {QStringLiteral("suppress"), QStringLiteral("canceled"), QStringLiteral("proposal.focused_context_suppressed")},
        {QStringLiteral("defer"), QStringLiteral("denied"), QStringLiteral("confidence.low")},
        {QStringLiteral("defer"), QStringLiteral("canceled"), QStringLiteral("confidence.low")}
    };

    for (int i = 0; i < rows.size(); ++i) {
        const Row row = rows.at(i);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        BehavioralEventLedger ledger(dir.path(), true);
        QVERIFY(ledger.initialize());

        const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T20:00:00.000Z"),
                                                   Qt::ISODateWithMs).toMSecsSinceEpoch() + (i * 1000);
        BehaviorDecision entryDecision;
        if (row.entryMode == QStringLiteral("suppress")) {
            const ProactiveSuggestionPlan proactive = ProactiveSuggestionPlanner::plan({
                .sourceKind = QStringLiteral("desktop_context"),
                .taskType = QStringLiteral("active_window"),
                .resultSummary = QStringLiteral("User is editing PLAN.md"),
                .sourceUrls = {},
                .sourceMetadata = {},
                .success = true,
                .desktopContext = {
                    {QStringLiteral("taskId"), QStringLiteral("editor_document")},
                    {QStringLiteral("threadId"), desktopContext.value(QStringLiteral("threadId"))},
                    {QStringLiteral("documentContext"), QStringLiteral("PLAN.md")},
                    {QStringLiteral("workspaceContext"), QStringLiteral("Vaxil")}
                },
                .desktopContextAtMs = nowMs,
                .cooldownState = CooldownState{},
                .focusMode = FocusModeState{},
                .nowMs = nowMs + 1000
            });
            entryDecision = proactive.decision;
        } else {
            CooldownEngine cooldownEngine;
            entryDecision = cooldownEngine.evaluate({
                .context = CompanionContextSnapshot{
                    .threadId = ContextThreadId{},
                    .appId = QStringLiteral("system"),
                    .taskId = QStringLiteral("background_sync"),
                    .topic = QStringLiteral("maintenance"),
                    .recentIntent = QStringLiteral("none"),
                    .confidence = 0.20,
                    .metadata = {}
                },
                .state = CooldownState{},
                .focusMode = FocusModeState{},
                .priority = QStringLiteral("medium"),
                .confidence = 0.40,
                .novelty = 0.52,
                .nowMs = nowMs
            });
        }

        ActionSession session;
        session.id = QStringLiteral("session_cooccurrence_matrix_%1").arg(i);
        session.userRequest = QStringLiteral("cooccurrence matrix");
        BehaviorTraceEvent entry = BehaviorTraceEvent::create(
            QStringLiteral("action_proposal"),
            QStringLiteral("gated"),
            entryDecision.reasonCode,
            {{QStringLiteral("action"), entryDecision.action}, {QStringLiteral("sourceLabel"), row.entryMode}});
        BehaviorTraceEvent permission = ActionRiskPermissionService::permissionEvent(
            pending, QStringLiteral("CooccurrenceMatrix"), desktopContext);
        BehaviorTraceEvent confirmation = ActionRiskPermissionService::confirmationOutcomeEvent(
            pending, QStringLiteral("CooccurrenceMatrix"), session, row.outcome, row.outcome, desktopContext);

        const QString traceId = QStringLiteral("trace_cooccurrence_matrix_%1").arg(i);
        entry.traceId = traceId;
        permission.traceId = traceId;
        confirmation.traceId = traceId;
        entry.threadId = desktopContext.value(QStringLiteral("threadId")).toString();
        entry.timestampUtc = QDateTime::fromMSecsSinceEpoch(nowMs, Qt::UTC);
        permission.timestampUtc = entry.timestampUtc.addMSecs(1);
        confirmation.timestampUtc = entry.timestampUtc.addMSecs(2);

        QVERIFY(ledger.recordEvent(entry));
        QVERIFY(ledger.recordEvent(permission));
        QVERIFY(ledger.recordEvent(confirmation));

        const QList<BehaviorTraceEvent> events = ledger.recentEvents(8);
        QCOMPARE(events.size(), 3);
        QCOMPARE(events.first().traceId, traceId);
        QCOMPARE(events.last().traceId, traceId);
        QCOMPARE(events.last().stage, row.outcome);
        QCOMPARE(events.last().payload.value(QStringLiteral("executionWillContinue")).toBool(), false);
        QCOMPARE(events.first().reasonCode, row.expectedEntryReason);
    }
}

void ProactiveActionCooccurrencePolicyMatrixTests::cooccurrenceReasonCodesRemainStable()
{
    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T20:30:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    const ProactiveSuggestionPlan suppressed = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("desktop_context"),
        .taskType = QStringLiteral("active_window"),
        .resultSummary = QStringLiteral("User is editing PLAN.md"),
        .sourceUrls = {},
        .sourceMetadata = {},
        .success = true,
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("editor_document")},
            {QStringLiteral("threadId"), QStringLiteral("desktop::editor_document::calendar::sprint_plan")},
            {QStringLiteral("documentContext"), QStringLiteral("PLAN.md")},
            {QStringLiteral("workspaceContext"), QStringLiteral("Vaxil")}
        },
        .desktopContextAtMs = nowMs,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = nowMs + 500
    });
    QCOMPARE(suppressed.decision.reasonCode, QStringLiteral("proposal.focused_context_suppressed"));

    CooldownEngine cooldownEngine;
    const BehaviorDecision deferred = cooldownEngine.evaluate({
        .context = CompanionContextSnapshot{.threadId = ContextThreadId{}},
        .state = CooldownState{},
        .focusMode = FocusModeState{},
        .priority = QStringLiteral("medium"),
        .confidence = 0.40,
        .novelty = 0.52,
        .nowMs = nowMs
    });
    QCOMPARE(deferred.reasonCode, QStringLiteral("confidence.low"));

    const QVariantMap desktopContext = mixedDesktopConnectorContext();
    const ActionRiskPermissionEvaluation pending = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), false, {});
    ActionSession session;
    session.id = QStringLiteral("session_reason_code_stability");
    session.userRequest = QStringLiteral("reason code stability");
    const BehaviorTraceEvent denied = ActionRiskPermissionService::confirmationOutcomeEvent(
        pending, QStringLiteral("ReasonCodePolicy"), session, QStringLiteral("denied"), QStringLiteral("no"), desktopContext);
    const BehaviorTraceEvent canceled = ActionRiskPermissionService::confirmationOutcomeEvent(
        pending, QStringLiteral("ReasonCodePolicy"), session, QStringLiteral("canceled"), QStringLiteral("cancel"), desktopContext);
    QCOMPARE(denied.reasonCode, QStringLiteral("confirmation.denied"));
    QCOMPARE(canceled.reasonCode, QStringLiteral("confirmation.canceled"));
}

void ProactiveActionCooccurrencePolicyMatrixTests::connectorNoveltyPenaltyThenDeniedActionSharesTrace()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BehavioralEventLedger ledger(dir.path(), true);
    QVERIFY(ledger.initialize());

    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T21:00:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    QVariantMap metadata{{QStringLiteral("connectorKind"), QStringLiteral("schedule")},
                         {QStringLiteral("eventTitle"), QStringLiteral("Sprint planning")},
                         {QStringLiteral("occurredAtUtc"), QStringLiteral("2026-04-18T10:00:00.000Z")},
                         {QStringLiteral("historySeenCount"), 5},
                         {QStringLiteral("connectorKindRecentSeenCount"), 5},
                         {QStringLiteral("connectorKindRecentPresentedCount"), 2}};
    const ProactiveSuggestionPlan proactive = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("connector_schedule_calendar"),
        .taskType = QStringLiteral("live_update"),
        .resultSummary = QStringLiteral("Schedule updated: Sprint planning"),
        .sourceUrls = {},
        .sourceMetadata = metadata,
        .success = true,
        .cooldownState = CooldownState{
            .threadId = QStringLiteral("connector_event_toast::live_update"),
            .activeUntilEpochMs = nowMs + 120000
        },
        .focusMode = FocusModeState{},
        .nowMs = nowMs
    });
    QVERIFY(!proactive.decision.allowed);
    QCOMPARE(proactive.decision.reasonCode, QStringLiteral("cooldown.low_novelty"));

    const QVariantMap desktopContext = mixedDesktopConnectorContext();
    const ActionRiskPermissionEvaluation pending = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), false, {});
    ActionSession session;
    session.id = QStringLiteral("session_novelty_penalty_denied");
    session.userRequest = QStringLiteral("Force action after low novelty");

    BehaviorTraceEvent entry = BehaviorTraceEvent::create(
        QStringLiteral("action_proposal"),
        QStringLiteral("gated"),
        proactive.decision.reasonCode,
        {
            {QStringLiteral("action"), proactive.decision.action},
            {QStringLiteral("noveltyScore"), proactive.noveltyScore},
            {QStringLiteral("cooldownReasonCode"), proactive.cooldownDecision.reasonCode}
        });
    BehaviorTraceEvent permission = ActionRiskPermissionService::permissionEvent(
        pending, QStringLiteral("NoveltyPenalty"), desktopContext);
    BehaviorTraceEvent confirmation = ActionRiskPermissionService::confirmationOutcomeEvent(
        pending, QStringLiteral("NoveltyPenalty"), session, QStringLiteral("denied"), QStringLiteral("no"), desktopContext);

    const QString traceId = QStringLiteral("trace_novelty_penalty_denied");
    entry.traceId = traceId;
    permission.traceId = traceId;
    confirmation.traceId = traceId;
    entry.threadId = desktopContext.value(QStringLiteral("threadId")).toString();
    entry.timestampUtc = QDateTime::fromMSecsSinceEpoch(nowMs, Qt::UTC);
    permission.timestampUtc = entry.timestampUtc.addMSecs(1);
    confirmation.timestampUtc = entry.timestampUtc.addMSecs(2);

    QVERIFY(ledger.recordEvent(entry));
    QVERIFY(ledger.recordEvent(permission));
    QVERIFY(ledger.recordEvent(confirmation));
    const QList<BehaviorTraceEvent> events = ledger.recentEvents(8);
    QCOMPARE(events.size(), 3);
    QCOMPARE(events.first().reasonCode, QStringLiteral("cooldown.low_novelty"));
    QCOMPARE(events.last().stage, QStringLiteral("denied"));
    QCOMPARE(events.last().payload.value(QStringLiteral("executionWillContinue")).toBool(), false);
}

void ProactiveActionCooccurrencePolicyMatrixTests::breakCooldownFastPathStillRunsPermissionAndConfirmationGates()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BehavioralEventLedger ledger(dir.path(), true);
    QVERIFY(ledger.initialize());

    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T21:20:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    CooldownEngine cooldownEngine;
    const BehaviorDecision fastPath = cooldownEngine.evaluate({
        .context = CompanionContextSnapshot{
            .threadId = ContextThreadId{QStringLiteral("connector::urgent_meeting")},
            .appId = QStringLiteral("calendar"),
            .taskId = QStringLiteral("meeting_alert"),
            .topic = QStringLiteral("urgent deadline"),
            .recentIntent = QStringLiteral("notify"),
            .confidence = 0.95
        },
        .state = CooldownState{
            .threadId = QStringLiteral("connector_event_toast::live_update"),
            .activeUntilEpochMs = nowMs + 90000
        },
        .focusMode = FocusModeState{},
        .priority = QStringLiteral("high"),
        .confidence = 0.86,
        .novelty = 0.80,
        .nowMs = nowMs
    });
    QVERIFY(fastPath.allowed);
    QCOMPARE(fastPath.action, QStringLiteral("break_cooldown"));
    QCOMPARE(fastPath.reasonCode, QStringLiteral("cooldown.break_high_novelty"));

    const QVariantMap desktopContext = mixedDesktopConnectorContext();
    const ActionRiskPermissionEvaluation pending = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), false, {});
    const ActionRiskPermissionEvaluation approved = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), true, {});

    ActionSession session;
    session.id = QStringLiteral("session_break_cooldown_gated");
    session.userRequest = QStringLiteral("Proceed urgent action");
    BehaviorTraceEvent entry = BehaviorTraceEvent::create(
        QStringLiteral("action_proposal"),
        QStringLiteral("gated"),
        fastPath.reasonCode,
        {
            {QStringLiteral("action"), fastPath.action},
            {QStringLiteral("priority"), QStringLiteral("high")}
        });
    BehaviorTraceEvent permission = ActionRiskPermissionService::permissionEvent(
        pending, QStringLiteral("BreakCooldownFastPath"), desktopContext);
    BehaviorTraceEvent confirmation = ActionRiskPermissionService::confirmationOutcomeEvent(
        approved, QStringLiteral("BreakCooldownFastPath"), session, QStringLiteral("approved"), QStringLiteral("yes"), desktopContext);

    const QString traceId = QStringLiteral("trace_break_cooldown_gated");
    entry.traceId = traceId;
    permission.traceId = traceId;
    confirmation.traceId = traceId;
    entry.threadId = desktopContext.value(QStringLiteral("threadId")).toString();
    entry.timestampUtc = QDateTime::fromMSecsSinceEpoch(nowMs, Qt::UTC);
    permission.timestampUtc = entry.timestampUtc.addMSecs(1);
    confirmation.timestampUtc = entry.timestampUtc.addMSecs(2);

    QVERIFY(ledger.recordEvent(entry));
    QVERIFY(ledger.recordEvent(permission));
    QVERIFY(ledger.recordEvent(confirmation));
    const QList<BehaviorTraceEvent> events = ledger.recentEvents(8);
    QCOMPARE(events.size(), 3);
    QCOMPARE(events.first().reasonCode, QStringLiteral("cooldown.break_high_novelty"));
    QCOMPARE(events.at(1).family, QStringLiteral("permission"));
    QCOMPARE(events.last().stage, QStringLiteral("approved"));
    QCOMPARE(events.last().payload.value(QStringLiteral("executionWillContinue")).toBool(), true);
}

void ProactiveActionCooccurrencePolicyMatrixTests::mixedPriorityConnectorBurstCrossesRankingCooldownAndActionOrdering()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BehavioralEventLedger ledger(dir.path(), true);
    QVERIFY(ledger.initialize());

    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-19T00:30:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    const CooldownState activeCooldown{
        .threadId = QStringLiteral("connector_event_toast::live_update"),
        .activeUntilEpochMs = nowMs + 120000,
        .lastTopic = QStringLiteral("deadline")
    };
    const QVariantMap burstMetadata{
        {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
        {QStringLiteral("eventTitle"), QStringLiteral("Production deadline")},
        {QStringLiteral("occurredAtUtc"), QStringLiteral("2026-04-18T06:30:00.000Z")},
        {QStringLiteral("historySeenCount"), 6},
        {QStringLiteral("connectorKindRecentSeenCount"), 5},
        {QStringLiteral("connectorKindRecentPresentedCount"), 3},
        {QStringLiteral("taskKey"), QStringLiteral("schedule:prod_deadline")}
    };
    const ProactiveSuggestionPlan highCandidate = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("connector_schedule_calendar"),
        .taskType = QStringLiteral("live_update"),
        .resultSummary = QStringLiteral("Connector update failed for production deadline"),
        .sourceUrls = {},
        .sourceMetadata = burstMetadata,
        .success = false,
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("editor_document")},
            {QStringLiteral("threadId"), QStringLiteral("connector_event_toast::live_update")},
            {QStringLiteral("documentContext"), QStringLiteral("PLAN.md")}
        },
        .desktopContextAtMs = nowMs - 2000,
        .cooldownState = activeCooldown,
        .focusMode = FocusModeState{},
        .nowMs = nowMs
    });
    QVERIFY(!highCandidate.rankedProposals.isEmpty());
    QCOMPARE(highCandidate.rankedProposals.first().proposal.priority, QStringLiteral("high"));
    QVERIFY(!highCandidate.decision.allowed);
    QCOMPARE(highCandidate.decision.reasonCode, QStringLiteral("cooldown.low_novelty"));
    const QStringList noveltyReasons = highCandidate.cooldownDecision.details.value(
        QStringLiteral("noveltyReasonCodes")).toStringList();
    QVERIFY(noveltyReasons.contains(QStringLiteral("novelty.connector_burst")));

    CooldownEngine cooldownEngine;
    const BehaviorDecision criticalCandidate = cooldownEngine.evaluate({
        .context = CompanionContextSnapshot{
            .threadId = ContextThreadId{QStringLiteral("connector_event_toast::live_update")},
            .appId = QStringLiteral("calendar"),
            .taskId = QStringLiteral("meeting_alert"),
            .topic = QStringLiteral("deadline"),
            .recentIntent = QStringLiteral("notify"),
            .confidence = 0.93
        },
        .state = activeCooldown,
        .focusMode = FocusModeState{},
        .priority = QStringLiteral("critical"),
        .confidence = 0.92,
        .novelty = 0.85,
        .nowMs = nowMs + 1000
    });
    QVERIFY(criticalCandidate.allowed);
    QCOMPARE(criticalCandidate.reasonCode, QStringLiteral("cooldown.break_high_novelty"));

    const QVariantMap desktopContext = mixedDesktopConnectorContext();
    const ActionRiskPermissionEvaluation pending = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), false, {});
    const ActionRiskPermissionEvaluation approved = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), true, {});
    ActionSession session;
    session.id = QStringLiteral("session_mixed_priority_connector_burst");
    session.userRequest = QStringLiteral("Proceed with critical deadline action");

    BehaviorTraceEvent highSuppressed = BehaviorTraceEvent::create(
        QStringLiteral("action_proposal"),
        QStringLiteral("ranked"),
        highCandidate.decision.reasonCode,
        {{QStringLiteral("action"), highCandidate.decision.action},
         {QStringLiteral("priority"), QStringLiteral("high")},
         {QStringLiteral("proposalId"), highCandidate.selectedProposal.proposalId}});
    BehaviorTraceEvent criticalAllowed = BehaviorTraceEvent::create(
        QStringLiteral("action_proposal"),
        QStringLiteral("gated"),
        criticalCandidate.reasonCode,
        {{QStringLiteral("action"), criticalCandidate.action},
         {QStringLiteral("priority"), QStringLiteral("critical")}});
    BehaviorTraceEvent permission = ActionRiskPermissionService::permissionEvent(
        pending, QStringLiteral("MixedPriorityBurst"), desktopContext);
    BehaviorTraceEvent confirmation = ActionRiskPermissionService::confirmationOutcomeEvent(
        approved, QStringLiteral("MixedPriorityBurst"), session, QStringLiteral("approved"), QStringLiteral("yes"), desktopContext);

    const QString traceId = QStringLiteral("trace_mixed_priority_connector_burst");
    const QString threadId = desktopContext.value(QStringLiteral("threadId")).toString();
    for (BehaviorTraceEvent *event : {&highSuppressed, &criticalAllowed, &permission, &confirmation}) {
        event->traceId = traceId;
        event->threadId = threadId;
    }
    highSuppressed.timestampUtc = QDateTime::fromMSecsSinceEpoch(nowMs, Qt::UTC);
    criticalAllowed.timestampUtc = highSuppressed.timestampUtc.addMSecs(1);
    permission.timestampUtc = highSuppressed.timestampUtc.addMSecs(2);
    confirmation.timestampUtc = highSuppressed.timestampUtc.addMSecs(3);

    QVERIFY(ledger.recordEvent(highSuppressed));
    QVERIFY(ledger.recordEvent(criticalAllowed));
    QVERIFY(ledger.recordEvent(permission));
    QVERIFY(ledger.recordEvent(confirmation));

    const QList<BehaviorTraceEvent> events = ledger.recentEvents(8);
    QCOMPARE(events.size(), 4);
    QCOMPARE(events.at(0).reasonCode, QStringLiteral("cooldown.low_novelty"));
    QCOMPARE(events.at(0).payload.value(QStringLiteral("priority")).toString(), QStringLiteral("high"));
    QCOMPARE(events.at(1).reasonCode, QStringLiteral("cooldown.break_high_novelty"));
    QCOMPARE(events.at(1).payload.value(QStringLiteral("priority")).toString(), QStringLiteral("critical"));
    QCOMPARE(events.at(2).family, QStringLiteral("permission"));
    QCOMPARE(events.at(3).stage, QStringLiteral("approved"));
    QCOMPARE(events.at(3).payload.value(QStringLiteral("executionWillContinue")).toBool(), true);
}

QTEST_APPLESS_MAIN(ProactiveActionCooccurrencePolicyMatrixTests)
#include "ProactiveActionCooccurrencePolicyMatrixTests.moc"
