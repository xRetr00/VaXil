#include <QtTest>

#include <QSqlDatabase>
#include <QTemporaryDir>

#include "cognition/ProactiveSuggestionPlanner.h"
#include "core/ActionRiskPermissionService.h"
#include "telemetry/BehavioralEventLedger.h"

class ProactiveActionCooccurrenceHappyPathScenarioTests : public QObject
{
    Q_OBJECT

private slots:
    void deniedAndCanceledConfirmationOutcomesAreTraceableWithMixedContext();
    void proactiveGateThenActionConfirmationSharesTrace();
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

void ProactiveActionCooccurrenceHappyPathScenarioTests::deniedAndCanceledConfirmationOutcomesAreTraceableWithMixedContext()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BehavioralEventLedger ledger(dir.path(), true);
    QVERIFY(ledger.initialize());
    const QVariantMap desktopContext = mixedDesktopConnectorContext();
    const ActionRiskPermissionEvaluation pending = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), false, {});
    ActionSession session; session.id = QStringLiteral("session_mixed_context_denied_canceled");
    session.userRequest = QStringLiteral("Patch docs and open linked references");

    BehaviorTraceEvent denied = ActionRiskPermissionService::confirmationOutcomeEvent(
        pending,
        QStringLiteral("MixedContextAction"),
        session,
        QStringLiteral("denied"),
        QStringLiteral("no"),
        desktopContext);
    BehaviorTraceEvent canceled = ActionRiskPermissionService::confirmationOutcomeEvent(
        pending,
        QStringLiteral("MixedContextAction"),
        session,
        QStringLiteral("canceled"),
        QStringLiteral("cancel"),
        desktopContext);

    const QString traceId = QStringLiteral("trace_mixed_context_denied_canceled");
    denied.traceId = traceId;
    canceled.traceId = traceId;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    denied.timestampUtc = now;
    canceled.timestampUtc = now.addMSecs(1);

    QVERIFY(ledger.recordEvent(denied));
    QVERIFY(ledger.recordEvent(canceled));

    const QList<BehaviorTraceEvent> events = ledger.recentEvents(4);
    QCOMPARE(events.size(), 2);
    QStringList stages;
    for (const BehaviorTraceEvent &event : events) {
        QCOMPARE(event.family, QStringLiteral("confirmation"));
        QCOMPARE(event.traceId, traceId);
        QCOMPARE(event.threadId, desktopContext.value(QStringLiteral("threadId")).toString());
        QCOMPARE(event.payload.value(QStringLiteral("executionWillContinue")).toBool(), false);
        QCOMPARE(event.payload.value(QStringLiteral("riskReasonCode")).toString(),
                 QStringLiteral("action_policy.mixed_context_confirmation"));
        stages.push_back(event.stage);
    }

    QVERIFY(stages.contains(QStringLiteral("denied")));
    QVERIFY(stages.contains(QStringLiteral("canceled")));
}

void ProactiveActionCooccurrenceHappyPathScenarioTests::proactiveGateThenActionConfirmationSharesTrace()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BehavioralEventLedger ledger(dir.path(), true);
    QVERIFY(ledger.initialize());
    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T18:00:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    const ProactiveSuggestionPlan proactive = ProactiveSuggestionPlanner::plan({
        .sourceKind = QStringLiteral("connector_schedule_calendar"),
        .taskType = QStringLiteral("live_update"),
        .resultSummary = QStringLiteral("Schedule updated: Sprint planning"),
        .sourceUrls = {},
        .sourceMetadata = {
            {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
            {QStringLiteral("eventTitle"), QStringLiteral("Sprint planning")},
            {QStringLiteral("occurredAtUtc"), QStringLiteral("2026-04-18T17:58:00.000Z")},
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
    QVERIFY(!proactive.selectedProposal.proposalId.isEmpty());

    const QVariantMap desktopContext = mixedDesktopConnectorContext();
    const ActionRiskPermissionEvaluation pending = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), false, {});
    const ActionRiskPermissionEvaluation approved = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), true, {});
    ActionSession session; session.id = QStringLiteral("session_proactive_action_cooccurrence");
    session.userRequest = QStringLiteral("Proceed with patch after proactive suggestion");

    BehaviorTraceEvent proposalGate = BehaviorTraceEvent::create(
        QStringLiteral("action_proposal"),
        QStringLiteral("gated"),
        proactive.decision.reasonCode,
        {
            {QStringLiteral("action"), proactive.decision.action},
            {QStringLiteral("proposalId"), proactive.selectedProposal.proposalId},
            {QStringLiteral("sourceLabel"), proactive.selectedProposal.arguments.value(QStringLiteral("sourceLabel")).toString()},
            {QStringLiteral("presentationKeyHint"), proactive.selectedProposal.arguments.value(QStringLiteral("presentationKeyHint")).toString()}
        });
    BehaviorTraceEvent risk = ActionRiskPermissionService::riskEvent(pending, QStringLiteral("Cooccurrence"), desktopContext);
    BehaviorTraceEvent permission = ActionRiskPermissionService::permissionEvent(pending, QStringLiteral("Cooccurrence"), desktopContext);
    BehaviorTraceEvent confirmation = ActionRiskPermissionService::confirmationOutcomeEvent(
        approved,
        QStringLiteral("Cooccurrence"),
        session,
        QStringLiteral("approved"),
        QStringLiteral("yes"),
        desktopContext);

    const QString traceId = QStringLiteral("trace_proactive_action_cooccurrence");
    const QString threadId = desktopContext.value(QStringLiteral("threadId")).toString();
    proposalGate.traceId = traceId;
    risk.traceId = traceId;
    permission.traceId = traceId;
    confirmation.traceId = traceId;
    proposalGate.threadId = threadId;
    proposalGate.capabilityId = proactive.selectedProposal.capabilityId;
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
    bool sawProposal = false;
    bool sawRisk = false;
    bool sawPermission = false;
    bool sawConfirmation = false;
    for (const BehaviorTraceEvent &event : events) {
        QCOMPARE(event.traceId, traceId);
        QCOMPARE(event.threadId, threadId);
        if (event.family == QStringLiteral("action_proposal")) {
            sawProposal = true;
            QCOMPARE(event.stage, QStringLiteral("gated"));
            QVERIFY(!event.payload.value(QStringLiteral("proposalId")).toString().isEmpty());
        } else if (event.family == QStringLiteral("risk_check")) {
            sawRisk = true;
            QCOMPARE(event.payload.value(QStringLiteral("level")).toString(), QStringLiteral("high"));
        } else if (event.family == QStringLiteral("permission")) {
            sawPermission = true;
            QVERIFY(!event.payload.value(QStringLiteral("permissions")).toList().isEmpty());
        } else if (event.family == QStringLiteral("confirmation")) {
            sawConfirmation = true;
            QCOMPARE(event.stage, QStringLiteral("approved"));
            QCOMPARE(event.payload.value(QStringLiteral("executionWillContinue")).toBool(), true);
        }
    }

    QVERIFY(sawProposal);
    QVERIFY(sawRisk);
    QVERIFY(sawPermission);
    QVERIFY(sawConfirmation);
}

QTEST_APPLESS_MAIN(ProactiveActionCooccurrenceHappyPathScenarioTests)
#include "ProactiveActionCooccurrenceHappyPathScenarioTests.moc"
