#include <QtTest>

#include <QSqlDatabase>
#include <QTemporaryDir>

#include "core/ActionRiskPermissionService.h"
#include "telemetry/BehavioralEventLedger.h"

class PermissionOverrideScenarioTests : public QObject
{
    Q_OBJECT

private slots:
    void allowOverrideBypassesPendingConfirmation();
    void denyOverrideBlocksEvenAfterConfirmation();
    void confirmOverrideRequiresThenAllowsAfterConfirmation();
    void clearedOverridesReturnToRegistryDefaults();
    void ledgerTraceReconstructsPermissionOverrideFlow();
    void actionFlowRequiresConfirmationThenApprovesWithTraceableEvents();
    void timelineSummaryFieldsExistForRiskPermissionAndConfirmation();
};

namespace {
ToolPlan planFor(const QStringList &tools)
{
    ToolPlan plan;
    plan.sideEffecting = true;
    plan.orderedToolNames = tools;
    return plan;
}

TrustDecision confirmationTrust()
{
    TrustDecision trust;
    trust.highRisk = true;
    trust.requiresConfirmation = true;
    trust.contextReasonCode = QStringLiteral("action_policy.test_confirmation");
    return trust;
}

PermissionOverrideRule overrideRule(const QString &capabilityId,
                                    const QString &decision,
                                    const QString &scope)
{
    return {
        .capabilityId = capabilityId,
        .decision = decision,
        .scope = scope,
        .reasonCode = QStringLiteral("test.%1.%2").arg(capabilityId, decision)
    };
}
}

void PermissionOverrideScenarioTests::allowOverrideBypassesPendingConfirmation()
{
    const ActionRiskPermissionEvaluation evaluation =
        ActionRiskPermissionService::evaluate(
            planFor({QStringLiteral("file_patch")}),
            confirmationTrust(),
            false,
            {overrideRule(QStringLiteral("filesystem_write"), QStringLiteral("allow"), QStringLiteral("project_workspace"))});

    QCOMPARE(evaluation.permissions.size(), 1);
    QVERIFY(evaluation.permissions.first().granted);
    QCOMPARE(evaluation.permissions.first().scope, QStringLiteral("project_workspace"));
    QCOMPARE(evaluation.permissions.first().reasonCode, QStringLiteral("test.filesystem_write.allow"));
}

void PermissionOverrideScenarioTests::denyOverrideBlocksEvenAfterConfirmation()
{
    const ActionRiskPermissionEvaluation evaluation =
        ActionRiskPermissionService::evaluate(
            planFor({QStringLiteral("browser_open")}),
            confirmationTrust(),
            true,
            {overrideRule(QStringLiteral("desktop_automation"), QStringLiteral("deny"), QStringLiteral("blocked"))});

    QCOMPARE(evaluation.permissions.size(), 1);
    QVERIFY(!evaluation.permissions.first().granted);
    QCOMPARE(evaluation.permissions.first().scope, QStringLiteral("blocked"));
    QCOMPARE(evaluation.permissions.first().reasonCode, QStringLiteral("test.desktop_automation.deny"));
}

void PermissionOverrideScenarioTests::confirmOverrideRequiresThenAllowsAfterConfirmation()
{
    TrustDecision trust;
    trust.highRisk = false;
    trust.requiresConfirmation = false;

    const QList<PermissionOverrideRule> overrides = {
        overrideRule(QStringLiteral("network_grounding"), QStringLiteral("confirm"), QStringLiteral("pending_network_confirm"))
    };
    const ToolPlan plan = planFor({QStringLiteral("web_search")});
    const ActionRiskPermissionEvaluation pending =
        ActionRiskPermissionService::evaluate(plan, trust, false, overrides);
    const ActionRiskPermissionEvaluation approved =
        ActionRiskPermissionService::evaluate(plan, trust, true, overrides);

    QVERIFY(!pending.permissions.first().granted);
    QCOMPARE(pending.permissions.first().scope, QStringLiteral("pending_network_confirm"));
    QVERIFY(approved.permissions.first().granted);
    QCOMPARE(approved.permissions.first().reasonCode, QStringLiteral("permission.allowed_by_registry"));
}

void PermissionOverrideScenarioTests::clearedOverridesReturnToRegistryDefaults()
{
    const ToolPlan plan = planFor({QStringLiteral("file_patch")});
    const TrustDecision trust = confirmationTrust();
    const ActionRiskPermissionEvaluation overridden =
        ActionRiskPermissionService::evaluate(
            plan,
            trust,
            false,
            {overrideRule(QStringLiteral("filesystem_write"), QStringLiteral("allow"), QStringLiteral("project_workspace"))});
    const ActionRiskPermissionEvaluation cleared =
        ActionRiskPermissionService::evaluate(plan, trust, false, {});

    QVERIFY(overridden.permissions.first().granted);
    QVERIFY(!cleared.permissions.first().granted);
    QCOMPARE(cleared.permissions.first().scope, QStringLiteral("pending_confirmation"));
    QCOMPARE(cleared.permissions.first().reasonCode, QStringLiteral("permission.waiting_for_confirmation"));
}

void PermissionOverrideScenarioTests::ledgerTraceReconstructsPermissionOverrideFlow()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BehavioralEventLedger ledger(dir.path(), true);
    QVERIFY(ledger.initialize());

    const QVariantMap desktopContext{
        {QStringLiteral("threadId"), QStringLiteral("desktop::editor_document::vscode::plan_md")},
        {QStringLiteral("taskId"), QStringLiteral("editor_document")}
    };
    const ActionRiskPermissionEvaluation evaluation =
        ActionRiskPermissionService::evaluate(
            planFor({QStringLiteral("file_patch")}),
            confirmationTrust(),
            false,
            {overrideRule(QStringLiteral("filesystem_write"), QStringLiteral("allow"), QStringLiteral("project_workspace"))});
    BehaviorTraceEvent risk = ActionRiskPermissionService::riskEvent(evaluation, QStringLiteral("BackgroundTasks"), desktopContext);
    BehaviorTraceEvent permission = ActionRiskPermissionService::permissionEvent(evaluation, QStringLiteral("BackgroundTasks"), desktopContext);
    risk.timestampUtc = QDateTime::currentDateTimeUtc();
    permission.timestampUtc = risk.timestampUtc.addMSecs(1);

    QVERIFY(ledger.recordEvent(risk));
    QVERIFY(ledger.recordEvent(permission));

    const QList<BehaviorTraceEvent> events = ledger.recentEvents(5);
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.first().family, QStringLiteral("permission"));
    QCOMPARE(events.last().family, QStringLiteral("risk_check"));

    const QVariantList permissions =
        events.first().payload.value(QStringLiteral("permissions")).toList();
    QCOMPARE(permissions.size(), 1);
    const QVariantMap grant = permissions.first().toMap();
    QCOMPARE(grant.value(QStringLiteral("capabilityId")).toString(), QStringLiteral("filesystem_write"));
    QCOMPARE(grant.value(QStringLiteral("granted")).toBool(), true);
    QCOMPARE(grant.value(QStringLiteral("reasonCode")).toString(), QStringLiteral("test.filesystem_write.allow"));
}

void PermissionOverrideScenarioTests::actionFlowRequiresConfirmationThenApprovesWithTraceableEvents()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BehavioralEventLedger ledger(dir.path(), true);
    QVERIFY(ledger.initialize());

    const QVariantMap desktopContext{
        {QStringLiteral("threadId"), QStringLiteral("desktop::editor_document::vscode::action_flow")},
        {QStringLiteral("taskId"), QStringLiteral("editor_document")}
    };
    const ToolPlan toolPlan = planFor({QStringLiteral("file_patch"), QStringLiteral("web_search")});
    const ActionRiskPermissionEvaluation pending =
        ActionRiskPermissionService::evaluate(toolPlan, confirmationTrust(), false, {});
    const ActionRiskPermissionEvaluation approved =
        ActionRiskPermissionService::evaluate(toolPlan, confirmationTrust(), true, {});

    QCOMPARE(pending.risk.level, QStringLiteral("high"));
    QVERIFY(pending.risk.confirmationRequired);
    QVERIFY(!pending.permissions.isEmpty());
    QVERIFY(!pending.permissions.first().granted);
    QCOMPARE(pending.permissions.first().scope, QStringLiteral("pending_confirmation"));
    QCOMPARE(approved.permissions.size(), pending.permissions.size());
    QVERIFY(approved.permissions.first().granted);
    QCOMPARE(approved.permissions.first().reasonCode, QStringLiteral("permission.allowed_by_registry"));

    BehaviorTraceEvent risk = ActionRiskPermissionService::riskEvent(pending, QStringLiteral("ActionFlow"), desktopContext);
    BehaviorTraceEvent permission = ActionRiskPermissionService::permissionEvent(pending, QStringLiteral("ActionFlow"), desktopContext);

    ActionSession session;
    session.id = QStringLiteral("session_action_confirm_approved");
    session.userRequest = QStringLiteral("Patch the file and verify docs");
    BehaviorTraceEvent confirmation = ActionRiskPermissionService::confirmationOutcomeEvent(
        approved,
        QStringLiteral("ActionFlow"),
        session,
        QStringLiteral("approved"),
        QStringLiteral("yes"),
        desktopContext);

    const QString traceId = QStringLiteral("trace_action_flow_confirm_approved");
    risk.traceId = traceId;
    permission.traceId = traceId;
    confirmation.traceId = traceId;

    const QDateTime baseTs = QDateTime::currentDateTimeUtc();
    risk.timestampUtc = baseTs;
    permission.timestampUtc = baseTs.addMSecs(1);
    confirmation.timestampUtc = baseTs.addMSecs(2);

    QVERIFY(ledger.recordEvent(risk));
    QVERIFY(ledger.recordEvent(permission));
    QVERIFY(ledger.recordEvent(confirmation));

    const QList<BehaviorTraceEvent> events = ledger.recentEvents(8);
    QCOMPARE(events.size(), 3);

    bool sawRisk = false;
    bool sawPermission = false;
    bool sawConfirmation = false;
    for (const BehaviorTraceEvent &event : events) {
        QCOMPARE(event.traceId, traceId);
        QCOMPARE(event.threadId, desktopContext.value(QStringLiteral("threadId")).toString());
        if (event.family == QStringLiteral("risk_check")) {
            sawRisk = true;
            QCOMPARE(event.payload.value(QStringLiteral("level")).toString(), QStringLiteral("high"));
            QCOMPARE(event.payload.value(QStringLiteral("permissionCount")).toInt(), pending.permissions.size());
        } else if (event.family == QStringLiteral("permission")) {
            sawPermission = true;
            const QVariantList permissions = event.payload.value(QStringLiteral("permissions")).toList();
            QVERIFY(!permissions.isEmpty());
            QCOMPARE(permissions.first().toMap().value(QStringLiteral("granted")).toBool(), false);
        } else if (event.family == QStringLiteral("confirmation")) {
            sawConfirmation = true;
            QCOMPARE(event.stage, QStringLiteral("approved"));
            QCOMPARE(event.reasonCode, QStringLiteral("confirmation.approved"));
            QCOMPARE(event.payload.value(QStringLiteral("executionWillContinue")).toBool(), true);
        }
    }

    QVERIFY(sawRisk);
    QVERIFY(sawPermission);
    QVERIFY(sawConfirmation);
}

void PermissionOverrideScenarioTests::timelineSummaryFieldsExistForRiskPermissionAndConfirmation()
{
    const QVariantMap desktopContext{
        {QStringLiteral("threadId"), QStringLiteral("desktop::editor_document::vscode::timeline_summary")},
        {QStringLiteral("taskId"), QStringLiteral("editor_document")}
    };
    const ToolPlan toolPlan = planFor({QStringLiteral("file_patch")});
    const ActionRiskPermissionEvaluation pending =
        ActionRiskPermissionService::evaluate(toolPlan, confirmationTrust(), false, {});
    const ActionRiskPermissionEvaluation approved =
        ActionRiskPermissionService::evaluate(toolPlan, confirmationTrust(), true, {});

    const QVariantMap riskRow =
        ActionRiskPermissionService::riskEvent(pending, QStringLiteral("TimelineSummary"), desktopContext).toVariantMap();
    const QVariantMap permissionRow =
        ActionRiskPermissionService::permissionEvent(pending, QStringLiteral("TimelineSummary"), desktopContext).toVariantMap();

    ActionSession session;
    session.id = QStringLiteral("session_timeline_summary");
    session.userRequest = QStringLiteral("Apply code update");
    const QVariantMap confirmationRow = ActionRiskPermissionService::confirmationOutcomeEvent(
        approved,
        QStringLiteral("TimelineSummary"),
        session,
        QStringLiteral("approved"),
        QStringLiteral("confirm"),
        desktopContext).toVariantMap();

    QCOMPARE(riskRow.value(QStringLiteral("family")).toString(), QStringLiteral("risk_check"));
    QVERIFY(!riskRow.value(QStringLiteral("level")).toString().isEmpty());
    QVERIFY(riskRow.contains(QStringLiteral("confirmationRequired")));
    QVERIFY(!riskRow.value(QStringLiteral("toolNames")).toStringList().isEmpty());
    QVERIFY(riskRow.contains(QStringLiteral("permissionCount")));

    QCOMPARE(permissionRow.value(QStringLiteral("family")).toString(), QStringLiteral("permission"));
    QVERIFY(permissionRow.contains(QStringLiteral("permissions")));
    QVERIFY(permissionRow.contains(QStringLiteral("riskLevel")));
    QVERIFY(permissionRow.contains(QStringLiteral("confirmationRequired")));
    const QVariantList permissionRows = permissionRow.value(QStringLiteral("permissions")).toList();
    QVERIFY(!permissionRows.isEmpty());
    const QVariantMap grantRow = permissionRows.first().toMap();
    QVERIFY(!grantRow.value(QStringLiteral("capabilityId")).toString().isEmpty());
    QVERIFY(grantRow.contains(QStringLiteral("granted")));
    QVERIFY(!grantRow.value(QStringLiteral("scope")).toString().isEmpty());

    QCOMPARE(confirmationRow.value(QStringLiteral("family")).toString(), QStringLiteral("confirmation"));
    QCOMPARE(confirmationRow.value(QStringLiteral("stage")).toString(), QStringLiteral("approved"));
    QVERIFY(confirmationRow.contains(QStringLiteral("executionWillContinue")));
    QVERIFY(confirmationRow.contains(QStringLiteral("permissions")));
}

QTEST_APPLESS_MAIN(PermissionOverrideScenarioTests)
#include "PermissionOverrideScenarioTests.moc"
