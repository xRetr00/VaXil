#include <QtTest>

#include <QSqlDatabase>
#include <QTemporaryDir>

#include "cognition/CooldownEngine.h"
#include "cognition/ProactiveSuggestionPlanner.h"
#include "core/ActionRiskPermissionService.h"
#include "telemetry/BehavioralEventLedger.h"

class ProactiveActionFocusModePolicyRegressionTests : public QObject
{
    Q_OBJECT

private slots:
    void focusModeCooccurrenceCoversCriticalVsNonCriticalWithActionGates();
    void policyRegressionKeepsBreakAndNoveltyReasonCodesStable();
    void focusModeCompetesWithConnectorUrgencyInSameTrace();
    void reasonCodesStayStableUnderDesktopContextVariations();
    void timedFocusModeExpiryTransitionsFromSuppressedToAllowedInOneTrace();
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

void ProactiveActionFocusModePolicyRegressionTests::focusModeCooccurrenceCoversCriticalVsNonCriticalWithActionGates()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }
    const QVariantMap desktopContext = mixedDesktopConnectorContext();
    const ActionRiskPermissionEvaluation pending = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), false, {});

    struct FocusRow {
        QString priority;
        bool allowCriticalAlerts;
        QString expectedReasonCode;
        QString confirmationOutcome;
    };
    const QList<FocusRow> rows = {
        {QStringLiteral("medium"), true, QStringLiteral("focus_mode.suppressed"), QStringLiteral("denied")},
        {QStringLiteral("critical"), false, QStringLiteral("focus_mode.critical_blocked"), QStringLiteral("canceled")},
        {QStringLiteral("critical"), true, QStringLiteral("cooldown.break_high_novelty"), QStringLiteral("approved")}
    };

    for (int i = 0; i < rows.size(); ++i) {
        const FocusRow row = rows.at(i);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        BehavioralEventLedger ledger(dir.path(), true);
        QVERIFY(ledger.initialize());

        const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T22:00:00.000Z"),
                                                   Qt::ISODateWithMs).toMSecsSinceEpoch() + (i * 1000);
        CooldownEngine cooldownEngine;
        const BehaviorDecision decision = cooldownEngine.evaluate({
            .context = CompanionContextSnapshot{
                .threadId = ContextThreadId{QStringLiteral("focus_mode::row_%1").arg(i)},
                .appId = QStringLiteral("calendar"),
                .taskId = QStringLiteral("meeting_alert"),
                .topic = QStringLiteral("deadline"),
                .recentIntent = QStringLiteral("notify"),
                .confidence = 0.90
            },
            .state = CooldownState{
                .threadId = QStringLiteral("connector_event_toast::live_update"),
                .activeUntilEpochMs = nowMs + 90000
            },
            .focusMode = FocusModeState{.enabled = true, .allowCriticalAlerts = row.allowCriticalAlerts},
            .priority = row.priority,
            .confidence = 0.86,
            .novelty = 0.80,
            .nowMs = nowMs
        });
        QCOMPARE(decision.reasonCode, row.expectedReasonCode);

        ActionSession session;
        session.id = QStringLiteral("session_focus_mode_row_%1").arg(i);
        session.userRequest = QStringLiteral("focus mode cooccurrence");
        const ActionRiskPermissionEvaluation approved = ActionRiskPermissionService::evaluate(
            sideEffectingPlan(), highRiskTrust(), true, {});
        const ActionRiskPermissionEvaluation evalForOutcome = row.confirmationOutcome == QStringLiteral("approved")
            ? approved
            : pending;

        BehaviorTraceEvent entry = BehaviorTraceEvent::create(
            QStringLiteral("action_proposal"),
            QStringLiteral("gated"),
            decision.reasonCode,
            {
                {QStringLiteral("action"), decision.action},
                {QStringLiteral("priority"), row.priority},
                {QStringLiteral("focusModeEnabled"), true},
                {QStringLiteral("allowCriticalAlerts"), row.allowCriticalAlerts}
            });
        BehaviorTraceEvent permission = ActionRiskPermissionService::permissionEvent(
            pending, QStringLiteral("FocusModeCooccurrence"), desktopContext);
        BehaviorTraceEvent confirmation = ActionRiskPermissionService::confirmationOutcomeEvent(
            evalForOutcome,
            QStringLiteral("FocusModeCooccurrence"),
            session,
            row.confirmationOutcome,
            row.confirmationOutcome,
            desktopContext);

        const QString traceId = QStringLiteral("trace_focus_mode_cooccurrence_%1").arg(i);
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
        QCOMPARE(events.first().reasonCode, row.expectedReasonCode);
        QCOMPARE(events.last().stage, row.confirmationOutcome);
        QCOMPARE(events.last().payload.value(QStringLiteral("executionWillContinue")).toBool(),
                 row.confirmationOutcome == QStringLiteral("approved"));
    }
}

void ProactiveActionFocusModePolicyRegressionTests::policyRegressionKeepsBreakAndNoveltyReasonCodesStable()
{
    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T22:20:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    const QList<QVariantMap> noveltyVariants = {
        {
            {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
            {QStringLiteral("eventTitle"), QStringLiteral("Sprint planning")},
            {QStringLiteral("occurredAtUtc"), QStringLiteral("2026-04-18T08:00:00.000Z")},
            {QStringLiteral("historySeenCount"), 5},
            {QStringLiteral("connectorKindRecentSeenCount"), 5},
            {QStringLiteral("connectorKindRecentPresentedCount"), 2}
        },
        {
            {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
            {QStringLiteral("eventTitle"), QStringLiteral("Sprint planning updated")},
            {QStringLiteral("occurredAtUtc"), QStringLiteral("2026-04-18T08:15:00.000Z")},
            {QStringLiteral("historySeenCount"), 6},
            {QStringLiteral("connectorKindRecentSeenCount"), 5},
            {QStringLiteral("connectorKindRecentPresentedCount"), 3}
        }
    };
    for (const QVariantMap &metadata : noveltyVariants) {
        const ProactiveSuggestionPlan plan = ProactiveSuggestionPlanner::plan({
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
        QVERIFY(!plan.decision.allowed);
        QCOMPARE(plan.decision.reasonCode, QStringLiteral("cooldown.low_novelty"));
    }

    CooldownEngine cooldownEngine;
    const QList<QPair<double, double>> breakVariants = {{0.86, 0.80}, {0.90, 0.76}, {0.80, 0.90}};
    for (const auto &scores : breakVariants) {
        const BehaviorDecision decision = cooldownEngine.evaluate({
            .context = CompanionContextSnapshot{
                .threadId = ContextThreadId{QStringLiteral("connector_event_toast::live_update")},
                .appId = QStringLiteral("calendar"),
                .taskId = QStringLiteral("meeting_alert")
            },
            .state = CooldownState{
                .threadId = QStringLiteral("connector_event_toast::live_update"),
                .activeUntilEpochMs = nowMs + 90000
            },
            .focusMode = FocusModeState{},
            .priority = QStringLiteral("high"),
            .confidence = scores.first,
            .novelty = scores.second,
            .nowMs = nowMs
        });
        QVERIFY(decision.allowed);
        QCOMPARE(decision.action, QStringLiteral("break_cooldown"));
        QCOMPARE(decision.reasonCode, QStringLiteral("cooldown.break_high_novelty"));
    }
}

void ProactiveActionFocusModePolicyRegressionTests::focusModeCompetesWithConnectorUrgencyInSameTrace()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BehavioralEventLedger ledger(dir.path(), true);
    QVERIFY(ledger.initialize());

    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T23:00:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    CooldownEngine cooldownEngine;
    const CooldownState activeCooldown{
        .threadId = QStringLiteral("connector_event_toast::live_update"),
        .activeUntilEpochMs = nowMs + 90000
    };
    const CompanionContextSnapshot connectorContext{
        .threadId = ContextThreadId{QStringLiteral("connector_event_toast::live_update")},
        .appId = QStringLiteral("calendar"),
        .taskId = QStringLiteral("meeting_alert"),
        .topic = QStringLiteral("urgent"),
        .recentIntent = QStringLiteral("notify"),
        .confidence = 0.92
    };

    const BehaviorDecision suppressed = cooldownEngine.evaluate({
        .context = connectorContext,
        .state = activeCooldown,
        .focusMode = FocusModeState{.enabled = true, .allowCriticalAlerts = true},
        .priority = QStringLiteral("medium"),
        .confidence = 0.86,
        .novelty = 0.80,
        .nowMs = nowMs
    });
    QVERIFY(!suppressed.allowed);
    QCOMPARE(suppressed.reasonCode, QStringLiteral("focus_mode.suppressed"));

    const BehaviorDecision urgencyAllowed = cooldownEngine.evaluate({
        .context = connectorContext,
        .state = activeCooldown,
        .focusMode = FocusModeState{.enabled = true, .allowCriticalAlerts = true},
        .priority = QStringLiteral("critical"),
        .confidence = 0.86,
        .novelty = 0.80,
        .nowMs = nowMs + 1000
    });
    QVERIFY(urgencyAllowed.allowed);
    QCOMPARE(urgencyAllowed.reasonCode, QStringLiteral("cooldown.break_high_novelty"));

    const QVariantMap desktopContext = mixedDesktopConnectorContext();
    const ActionRiskPermissionEvaluation pending = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), false, {});
    const ActionRiskPermissionEvaluation approved = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), true, {});
    ActionSession session;
    session.id = QStringLiteral("session_focus_vs_urgency_trace");
    session.userRequest = QStringLiteral("Urgent connector under focus mode");

    BehaviorTraceEvent suppressedEvent = BehaviorTraceEvent::create(
        QStringLiteral("action_proposal"),
        QStringLiteral("gated"),
        suppressed.reasonCode,
        {{QStringLiteral("action"), suppressed.action}, {QStringLiteral("priority"), QStringLiteral("medium")}});
    BehaviorTraceEvent urgentEvent = BehaviorTraceEvent::create(
        QStringLiteral("action_proposal"),
        QStringLiteral("gated"),
        urgencyAllowed.reasonCode,
        {{QStringLiteral("action"), urgencyAllowed.action}, {QStringLiteral("priority"), QStringLiteral("critical")}});
    BehaviorTraceEvent permission = ActionRiskPermissionService::permissionEvent(
        pending, QStringLiteral("FocusUrgencyCompetition"), desktopContext);
    BehaviorTraceEvent confirmation = ActionRiskPermissionService::confirmationOutcomeEvent(
        approved, QStringLiteral("FocusUrgencyCompetition"), session, QStringLiteral("approved"), QStringLiteral("yes"), desktopContext);

    const QString traceId = QStringLiteral("trace_focus_urgency_competition");
    for (BehaviorTraceEvent *event : {&suppressedEvent, &urgentEvent, &permission, &confirmation}) {
        event->traceId = traceId;
        event->threadId = desktopContext.value(QStringLiteral("threadId")).toString();
    }
    suppressedEvent.timestampUtc = QDateTime::fromMSecsSinceEpoch(nowMs, Qt::UTC);
    urgentEvent.timestampUtc = suppressedEvent.timestampUtc.addMSecs(1);
    permission.timestampUtc = suppressedEvent.timestampUtc.addMSecs(2);
    confirmation.timestampUtc = suppressedEvent.timestampUtc.addMSecs(3);

    QVERIFY(ledger.recordEvent(suppressedEvent));
    QVERIFY(ledger.recordEvent(urgentEvent));
    QVERIFY(ledger.recordEvent(permission));
    QVERIFY(ledger.recordEvent(confirmation));

    const QList<BehaviorTraceEvent> events = ledger.recentEvents(8);
    QCOMPARE(events.size(), 4);
    QCOMPARE(events.at(0).reasonCode, QStringLiteral("focus_mode.suppressed"));
    QCOMPARE(events.at(1).reasonCode, QStringLiteral("cooldown.break_high_novelty"));
    QCOMPARE(events.last().stage, QStringLiteral("approved"));
    QCOMPARE(events.last().payload.value(QStringLiteral("executionWillContinue")).toBool(), true);
}

void ProactiveActionFocusModePolicyRegressionTests::reasonCodesStayStableUnderDesktopContextVariations()
{
    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-18T23:20:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    const QList<QVariantMap> desktopVariants = {
        {{QStringLiteral("threadId"), QStringLiteral("desktop::editor::one")},
         {QStringLiteral("taskId"), QStringLiteral("editor_document")},
         {QStringLiteral("topic"), QStringLiteral("planning")},
         {QStringLiteral("appId"), QStringLiteral("vscode")}},
        {{QStringLiteral("threadId"), QStringLiteral("desktop::editor::two")},
         {QStringLiteral("taskId"), QStringLiteral("editor_document")},
         {QStringLiteral("topic"), QStringLiteral("planning notes")},
         {QStringLiteral("appId"), QStringLiteral("notepad")}},
        {{QStringLiteral("threadId"), QStringLiteral("desktop::editor::three")},
         {QStringLiteral("taskId"), QStringLiteral("editor_document")},
         {QStringLiteral("topic"), QStringLiteral("task graph")},
         {QStringLiteral("appId"), QStringLiteral("cursor")}}
    };

    for (const QVariantMap &desktop : desktopVariants) {
        const ProactiveSuggestionPlan plan = ProactiveSuggestionPlanner::plan({
            .sourceKind = QStringLiteral("desktop_context"),
            .taskType = QStringLiteral("active_window"),
            .resultSummary = QStringLiteral("User is editing PLAN.md"),
            .sourceUrls = {},
            .sourceMetadata = {},
            .success = true,
            .desktopContext = desktop,
            .desktopContextAtMs = nowMs,
            .cooldownState = CooldownState{},
            .focusMode = FocusModeState{},
            .nowMs = nowMs + 500
        });
        QVERIFY(!plan.decision.allowed);
        QCOMPARE(plan.decision.reasonCode, QStringLiteral("proposal.focused_context_suppressed"));
        QCOMPARE(plan.decision.action, QStringLiteral("suppress_proposal"));
    }
}

void ProactiveActionFocusModePolicyRegressionTests::timedFocusModeExpiryTransitionsFromSuppressedToAllowedInOneTrace()
{
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        QSKIP("QSQLITE driver is not available in this runtime.");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BehavioralEventLedger ledger(dir.path(), true);
    QVERIFY(ledger.initialize());

    const qint64 nowMs = QDateTime::fromString(QStringLiteral("2026-04-19T00:05:00.000Z"),
                                               Qt::ISODateWithMs).toMSecsSinceEpoch();
    const QVariantMap desktopContext = mixedDesktopConnectorContext();
    const CooldownState activeCooldown{
        .threadId = QStringLiteral("connector_event_toast::live_update"),
        .activeUntilEpochMs = nowMs + 180000
    };
    const CompanionContextSnapshot context{
        .threadId = ContextThreadId{QStringLiteral("connector_event_toast::live_update")},
        .appId = QStringLiteral("calendar"),
        .taskId = QStringLiteral("meeting_alert"),
        .topic = QStringLiteral("deadline"),
        .recentIntent = QStringLiteral("notify"),
        .confidence = 0.91
    };
    CooldownEngine cooldownEngine;
    const FocusModeState focusTimedActive{
        .enabled = true,
        .allowCriticalAlerts = true,
        .durationMinutes = 60,
        .untilEpochMs = nowMs + 45000,
        .source = QStringLiteral("manual")
    };
    const BehaviorDecision beforeExpiry = cooldownEngine.evaluate({
        .context = context,
        .state = activeCooldown,
        .focusMode = focusTimedActive,
        .priority = QStringLiteral("medium"),
        .confidence = 0.90,
        .novelty = 0.83,
        .nowMs = nowMs
    });
    QVERIFY(!beforeExpiry.allowed);
    QCOMPARE(beforeExpiry.reasonCode, QStringLiteral("focus_mode.suppressed"));

    const BehaviorDecision afterExpiry = cooldownEngine.evaluate({
        .context = context,
        .state = activeCooldown,
        .focusMode = FocusModeState{},
        .priority = QStringLiteral("high"),
        .confidence = 0.88,
        .novelty = 0.82,
        .nowMs = focusTimedActive.untilEpochMs + 1000
    });
    QVERIFY(afterExpiry.allowed);
    QCOMPARE(afterExpiry.reasonCode, QStringLiteral("cooldown.break_high_novelty"));

    const ActionRiskPermissionEvaluation pending = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), false, {});
    const ActionRiskPermissionEvaluation approved = ActionRiskPermissionService::evaluate(
        sideEffectingPlan(), highRiskTrust(), true, {});
    ActionSession session;
    session.id = QStringLiteral("session_focus_timed_expiry_transition");
    session.userRequest = QStringLiteral("Proceed after focus timer expires");

    BehaviorTraceEvent suppressedEvent = BehaviorTraceEvent::create(
        QStringLiteral("action_proposal"),
        QStringLiteral("gated"),
        beforeExpiry.reasonCode,
        {{QStringLiteral("action"), beforeExpiry.action},
         {QStringLiteral("priority"), QStringLiteral("medium")},
         {QStringLiteral("focusModeUntilEpochMs"), focusTimedActive.untilEpochMs}});
    BehaviorTraceEvent focusExpired = BehaviorTraceEvent::create(
        QStringLiteral("focus_mode"),
        QStringLiteral("expired"),
        QStringLiteral("focus_mode.timed_expired"),
        {{QStringLiteral("enabledBefore"), true},
         {QStringLiteral("durationMinutes"), focusTimedActive.durationMinutes},
         {QStringLiteral("untilEpochMs"), focusTimedActive.untilEpochMs}});
    BehaviorTraceEvent allowedEvent = BehaviorTraceEvent::create(
        QStringLiteral("action_proposal"),
        QStringLiteral("gated"),
        afterExpiry.reasonCode,
        {{QStringLiteral("action"), afterExpiry.action}, {QStringLiteral("priority"), QStringLiteral("high")}});
    BehaviorTraceEvent permission = ActionRiskPermissionService::permissionEvent(
        pending, QStringLiteral("FocusModeTimedExpiry"), desktopContext);
    BehaviorTraceEvent confirmation = ActionRiskPermissionService::confirmationOutcomeEvent(
        approved, QStringLiteral("FocusModeTimedExpiry"), session, QStringLiteral("approved"), QStringLiteral("yes"), desktopContext);

    const QString traceId = QStringLiteral("trace_focus_timed_expiry_transition");
    const QString threadId = desktopContext.value(QStringLiteral("threadId")).toString();
    for (BehaviorTraceEvent *event : {&suppressedEvent, &focusExpired, &allowedEvent, &permission, &confirmation}) {
        event->traceId = traceId;
        event->threadId = threadId;
    }
    suppressedEvent.timestampUtc = QDateTime::fromMSecsSinceEpoch(nowMs, Qt::UTC);
    focusExpired.timestampUtc = suppressedEvent.timestampUtc.addMSecs(1);
    allowedEvent.timestampUtc = suppressedEvent.timestampUtc.addMSecs(2);
    permission.timestampUtc = suppressedEvent.timestampUtc.addMSecs(3);
    confirmation.timestampUtc = suppressedEvent.timestampUtc.addMSecs(4);

    QVERIFY(ledger.recordEvent(suppressedEvent));
    QVERIFY(ledger.recordEvent(focusExpired));
    QVERIFY(ledger.recordEvent(allowedEvent));
    QVERIFY(ledger.recordEvent(permission));
    QVERIFY(ledger.recordEvent(confirmation));

    const QList<BehaviorTraceEvent> events = ledger.recentEvents(8);
    QCOMPARE(events.size(), 5);
    QCOMPARE(events.at(0).reasonCode, QStringLiteral("focus_mode.suppressed"));
    QCOMPARE(events.at(1).family, QStringLiteral("focus_mode"));
    QCOMPARE(events.at(1).stage, QStringLiteral("expired"));
    QCOMPARE(events.at(2).reasonCode, QStringLiteral("cooldown.break_high_novelty"));
    QCOMPARE(events.at(3).family, QStringLiteral("permission"));
    QCOMPARE(events.at(4).stage, QStringLiteral("approved"));
    QCOMPARE(events.at(4).payload.value(QStringLiteral("executionWillContinue")).toBool(), true);
}

QTEST_APPLESS_MAIN(ProactiveActionFocusModePolicyRegressionTests)
#include "ProactiveActionFocusModePolicyRegressionTests.moc"
