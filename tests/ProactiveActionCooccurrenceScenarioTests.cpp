#include <QtTest>

#include <QSqlDatabase>
#include <QTemporaryDir>

#include "cognition/CooldownEngine.h"
#include "cognition/ProactiveSuggestionPlanner.h"
#include "core/ActionRiskPermissionService.h"
#include "telemetry/BehavioralEventLedger.h"

class ProactiveActionCooccurrenceScenarioTests : public QObject
{
    Q_OBJECT

private slots:
    void proactiveSuppressedThenDeniedActionSharesTrace();
    void proactiveThenPendingPermissionCanceledSharesTrace();
    void proactiveDeferredThenDeniedActionSharesTrace();
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

void assertTimelineSummaryFieldsForNonHappyPath(const BehaviorTraceEvent &event)
{
    const QVariantMap row = event.toVariantMap();
    QVERIFY(!row.value(QStringLiteral("family")).toString().isEmpty());
    QVERIFY(!row.value(QStringLiteral("stage")).toString().isEmpty());
    QVERIFY(!row.value(QStringLiteral("reasonCode")).toString().isEmpty());
    QVERIFY(!row.value(QStringLiteral("traceId")).toString().isEmpty());
    QVERIFY(!row.value(QStringLiteral("threadId")).toString().isEmpty());

    if (row.value(QStringLiteral("family")).toString() == QStringLiteral("action_proposal")) {
        QVERIFY(!row.value(QStringLiteral("action")).toString().isEmpty());
    } else if (row.value(QStringLiteral("family")).toString() == QStringLiteral("permission")) {
        QVERIFY(row.contains(QStringLiteral("permissions")));
        QVERIFY(row.contains(QStringLiteral("riskLevel")));
        QVERIFY(row.contains(QStringLiteral("confirmationRequired")));
    } else if (row.value(QStringLiteral("family")).toString() == QStringLiteral("confirmation")) {
        QVERIFY(row.contains(QStringLiteral("executionWillContinue")));
        QVERIFY(row.contains(QStringLiteral("permissions")));
    }
}
}

void ProactiveActionCooccurrenceScenarioTests::proactiveSuppressedThenDeniedActionSharesTrace()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BehavioralEventLedger ledger(dir.path(), true);
    QVERIFY(ledger.initialize());
    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T19:00:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    const ProactiveSuggestionPlan proactive = ProactiveSuggestionPlanner::plan({
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
        .nowMs = nowMs + 1000
    });
    QVERIFY(!proactive.decision.allowed);
    QCOMPARE(proactive.decision.action, QStringLiteral("suppress_proposal"));

    const QVariantMap desktopContext = mixedDesktopConnectorContext();
    const ActionRiskPermissionEvaluation pending = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), false, {});

    ActionSession session; session.id = QStringLiteral("session_proactive_suppressed_then_denied");
    session.userRequest = QStringLiteral("Proceed despite suppression");

    BehaviorTraceEvent proposalGate = BehaviorTraceEvent::create(
        QStringLiteral("action_proposal"),
        QStringLiteral("gated"),
        proactive.decision.reasonCode,
        {
            {QStringLiteral("action"), proactive.decision.action},
            {QStringLiteral("taskType"), QStringLiteral("active_window")},
            {QStringLiteral("sourceLabel"), QStringLiteral("PLAN.md")}
        });
    BehaviorTraceEvent risk = ActionRiskPermissionService::riskEvent(pending, QStringLiteral("SuppressedThenDenied"), desktopContext);
    BehaviorTraceEvent permission = ActionRiskPermissionService::permissionEvent(pending, QStringLiteral("SuppressedThenDenied"), desktopContext);
    BehaviorTraceEvent confirmation = ActionRiskPermissionService::confirmationOutcomeEvent(
        pending,
        QStringLiteral("SuppressedThenDenied"),
        session,
        QStringLiteral("denied"),
        QStringLiteral("no"),
        desktopContext);

    const QString traceId = QStringLiteral("trace_proactive_suppressed_then_denied");
    const QString threadId = desktopContext.value(QStringLiteral("threadId")).toString();
    proposalGate.traceId = traceId;
    risk.traceId = traceId;
    permission.traceId = traceId;
    confirmation.traceId = traceId;
    proposalGate.threadId = threadId;
    proposalGate.capabilityId = QStringLiteral("desktop_context");
    proposalGate.timestampUtc = QDateTime::fromMSecsSinceEpoch(nowMs, Qt::UTC);
    risk.timestampUtc = proposalGate.timestampUtc.addMSecs(1);
    permission.timestampUtc = proposalGate.timestampUtc.addMSecs(2);
    confirmation.timestampUtc = proposalGate.timestampUtc.addMSecs(3);

    QVERIFY(ledger.recordEvent(proposalGate));
    QVERIFY(ledger.recordEvent(risk));
    QVERIFY(ledger.recordEvent(permission));
    QVERIFY(ledger.recordEvent(confirmation));

    const QList<BehaviorTraceEvent> events = ledger.recentEvents(8);
    QCOMPARE(events.size(), 4);

    bool sawSuppressedProposal = false;
    bool sawDenied = false;
    for (const BehaviorTraceEvent &event : events) {
        QCOMPARE(event.traceId, traceId);
        QCOMPARE(event.threadId, threadId);
        assertTimelineSummaryFieldsForNonHappyPath(event);
        if (event.family == QStringLiteral("action_proposal")) {
            sawSuppressedProposal = true;
            QCOMPARE(event.payload.value(QStringLiteral("action")).toString(),
                     QStringLiteral("suppress_proposal"));
        }
        if (event.family == QStringLiteral("confirmation")) {
            sawDenied = true;
            QCOMPARE(event.stage, QStringLiteral("denied"));
            QCOMPARE(event.payload.value(QStringLiteral("executionWillContinue")).toBool(), false);
        }
    }

    QVERIFY(sawSuppressedProposal);
    QVERIFY(sawDenied);
}

void ProactiveActionCooccurrenceScenarioTests::proactiveThenPendingPermissionCanceledSharesTrace()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BehavioralEventLedger ledger(dir.path(), true);
    QVERIFY(ledger.initialize());
    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T19:15:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    const ProactiveSuggestionPlan proactive = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("connector_schedule_calendar"),
        .taskType = QStringLiteral("live_update"),
        .resultSummary = QStringLiteral("Schedule updated: Sprint planning"),
        .sourceUrls = {},
        .sourceMetadata = {
            {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
            {QStringLiteral("eventTitle"), QStringLiteral("Sprint planning")},
            {QStringLiteral("occurredAtUtc"), QStringLiteral("2026-04-18T19:13:00.000Z")},
            {QStringLiteral("taskKey"), QStringLiteral("schedule:team")}
        },
        .presentationKey = QStringLiteral("connector:schedule:team"),
        .lastPresentedKey = QString(),
        .lastPresentedAtMs = 0,
        .success = true,
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("editor_document")},
            {QStringLiteral("threadId"), QStringLiteral("desktop::editor_document::calendar::sprint_plan")},
            {QStringLiteral("documentContext"), QStringLiteral("PLAN.md")}
        },
        .desktopContextAtMs = nowMs - 200000,
        .cooldownState = CooldownState{},
        .focusMode = FocusModeState{},
        .nowMs = nowMs
    });
    QVERIFY(proactive.decision.allowed);

    const QVariantMap desktopContext = mixedDesktopConnectorContext();
    const ActionRiskPermissionEvaluation pending = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), false, {});

    ActionSession session; session.id = QStringLiteral("session_proactive_then_canceled");
    session.userRequest = QStringLiteral("Try patch and open links");

    BehaviorTraceEvent proposalGate = BehaviorTraceEvent::create(
        QStringLiteral("action_proposal"),
        QStringLiteral("gated"),
        proactive.decision.reasonCode,
        {
            {QStringLiteral("action"), proactive.decision.action},
            {QStringLiteral("proposalId"), proactive.selectedProposal.proposalId}
        });
    BehaviorTraceEvent permission = ActionRiskPermissionService::permissionEvent(
        pending, QStringLiteral("PendingThenCanceled"), desktopContext);
    BehaviorTraceEvent confirmation = ActionRiskPermissionService::confirmationOutcomeEvent(
        pending,
        QStringLiteral("PendingThenCanceled"),
        session,
        QStringLiteral("canceled"),
        QStringLiteral("cancel"),
        desktopContext);

    const QString traceId = QStringLiteral("trace_proactive_pending_canceled");
    const QString threadId = desktopContext.value(QStringLiteral("threadId")).toString();
    proposalGate.traceId = traceId;
    permission.traceId = traceId;
    confirmation.traceId = traceId;
    proposalGate.threadId = threadId;
    proposalGate.capabilityId = proactive.selectedProposal.capabilityId;
    proposalGate.timestampUtc = QDateTime::fromMSecsSinceEpoch(nowMs, Qt::UTC);
    permission.timestampUtc = proposalGate.timestampUtc.addMSecs(1);
    confirmation.timestampUtc = proposalGate.timestampUtc.addMSecs(2);

    QVERIFY(ledger.recordEvent(proposalGate));
    QVERIFY(ledger.recordEvent(permission));
    QVERIFY(ledger.recordEvent(confirmation));

    const QList<BehaviorTraceEvent> events = ledger.recentEvents(8);
    QCOMPARE(events.size(), 3);

    bool sawPendingPermission = false;
    bool sawCanceled = false;
    for (const BehaviorTraceEvent &event : events) {
        QCOMPARE(event.traceId, traceId);
        QCOMPARE(event.threadId, threadId);
        assertTimelineSummaryFieldsForNonHappyPath(event);
        if (event.family == QStringLiteral("permission")) {
            const QVariantList permissions = event.payload.value(QStringLiteral("permissions")).toList();
            QVERIFY(!permissions.isEmpty());
            sawPendingPermission = !permissions.first().toMap().value(QStringLiteral("granted")).toBool();
        } else if (event.family == QStringLiteral("confirmation")) {
            sawCanceled = true;
            QCOMPARE(event.stage, QStringLiteral("canceled"));
            QCOMPARE(event.payload.value(QStringLiteral("executionWillContinue")).toBool(), false);
        }
    }

    QVERIFY(sawPendingPermission);
    QVERIFY(sawCanceled);
}

void ProactiveActionCooccurrenceScenarioTests::proactiveDeferredThenDeniedActionSharesTrace()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BehavioralEventLedger ledger(dir.path(), true);
    QVERIFY(ledger.initialize());
    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T19:30:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    CooldownEngine cooldownEngine;
    const BehaviorDecision deferDecision = cooldownEngine.evaluate({
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
    QVERIFY(!deferDecision.allowed);
    QCOMPARE(deferDecision.action, QStringLiteral("defer"));
    QCOMPARE(deferDecision.reasonCode, QStringLiteral("confidence.low"));

    const QVariantMap desktopContext = mixedDesktopConnectorContext();
    const ActionRiskPermissionEvaluation pending = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), false, {});

    ActionSession session; session.id = QStringLiteral("session_proactive_deferred_then_denied");
    session.userRequest = QStringLiteral("Run patch even though proactive deferred");

    BehaviorTraceEvent proposalGate = BehaviorTraceEvent::create(
        QStringLiteral("action_proposal"),
        QStringLiteral("gated"),
        deferDecision.reasonCode,
        {
            {QStringLiteral("action"), deferDecision.action},
            {QStringLiteral("taskType"), QStringLiteral("background_sync")},
            {QStringLiteral("sourceLabel"), QStringLiteral("Maintenance task")}
        });
    BehaviorTraceEvent permission = ActionRiskPermissionService::permissionEvent(
        pending, QStringLiteral("DeferredThenDenied"), desktopContext);
    BehaviorTraceEvent confirmation = ActionRiskPermissionService::confirmationOutcomeEvent(
        pending,
        QStringLiteral("DeferredThenDenied"),
        session,
        QStringLiteral("denied"),
        QStringLiteral("no"),
        desktopContext);

    const QString traceId = QStringLiteral("trace_proactive_deferred_then_denied"), threadId = desktopContext.value(QStringLiteral("threadId")).toString();
    proposalGate.traceId = traceId;
    permission.traceId = traceId;
    confirmation.traceId = traceId;
    proposalGate.threadId = threadId;
    proposalGate.capabilityId = QStringLiteral("cooldown_engine");
    proposalGate.timestampUtc = QDateTime::fromMSecsSinceEpoch(nowMs, Qt::UTC);
    permission.timestampUtc = proposalGate.timestampUtc.addMSecs(1);
    confirmation.timestampUtc = proposalGate.timestampUtc.addMSecs(2);

    QVERIFY(ledger.recordEvent(proposalGate));
    QVERIFY(ledger.recordEvent(permission));
    QVERIFY(ledger.recordEvent(confirmation));

    const QList<BehaviorTraceEvent> events = ledger.recentEvents(8);
    QCOMPARE(events.size(), 3);

    bool sawDeferredProposal = false;
    bool sawDenied = false;
    for (const BehaviorTraceEvent &event : events) {
        QCOMPARE(event.traceId, traceId);
        QCOMPARE(event.threadId, threadId);
        assertTimelineSummaryFieldsForNonHappyPath(event);
        if (event.family == QStringLiteral("action_proposal")) {
            sawDeferredProposal = true;
            QCOMPARE(event.payload.value(QStringLiteral("action")).toString(), QStringLiteral("defer"));
            QCOMPARE(event.reasonCode, QStringLiteral("confidence.low"));
        } else if (event.family == QStringLiteral("confirmation")) {
            sawDenied = true;
            QCOMPARE(event.stage, QStringLiteral("denied"));
            QCOMPARE(event.payload.value(QStringLiteral("executionWillContinue")).toBool(), false);
        }
    }

    QVERIFY(sawDeferredProposal);
    QVERIFY(sawDenied);
}

QTEST_APPLESS_MAIN(ProactiveActionCooccurrenceScenarioTests)
#include "ProactiveActionCooccurrenceScenarioTests.moc"
