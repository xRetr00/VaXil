#include <QtTest>

#include "cognition/ProactiveSurfaceGate.h"
#include "cognition/ProactiveSpeechPolicy.h"

class ProactiveSurfaceGateTests : public QObject
{
    Q_OBJECT

private slots:
    void suppressesSuccessToastDuringFocusMode();
    void suppressesInspectionToastDuringFocusedDesktopWork();
    void suppressesSuccessToastDuringActiveCooldown();
    void allowsToastOnMeaningfulThreadShift();
    void keepsFailureToastVisible();
    void suppressesCompletionFollowUpDuringFocusMode();
    void softlyAllowsCompletionFollowUpDuringActiveCooldown();
    void allowsUserRequestedCompletionDuringActiveCooldown();
    void allowsUserRequestedCompletionDuringFocusedDesktopWork();
    void allowsFailureFollowUpEvenInFocusMode();
    void proactiveSpeechAllowsApprovedSuggestion();
    void proactiveSpeechRespectsCooldownAndFocusMode();
};

void ProactiveSurfaceGateTests::suppressesSuccessToastDuringFocusMode()
{
    FocusModeState focusMode;
    focusMode.enabled = true;

    ProactiveSurfaceGate::Input input;
    input.result.type = QStringLiteral("web_search");
    input.result.success = true;
    input.focusMode = focusMode;

    const BehaviorDecision decision = ProactiveSurfaceGate::evaluateTaskToast(input);
    QVERIFY(!decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("surface.focus_mode_suppressed"));
}

void ProactiveSurfaceGateTests::suppressesInspectionToastDuringFocusedDesktopWork()
{
    ProactiveSurfaceGate::Input input;
    input.result.type = QStringLiteral("file_read");
    input.result.success = true;
    input.desktopContext.insert(QStringLiteral("taskId"), QStringLiteral("editor_document"));
    input.desktopContextAtMs = 1000;
    input.nowMs = 1500;

    const BehaviorDecision decision = ProactiveSurfaceGate::evaluateTaskToast(input);
    QVERIFY(!decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("surface.focused_context_suppressed"));
}

void ProactiveSurfaceGateTests::suppressesSuccessToastDuringActiveCooldown()
{
    ProactiveSurfaceGate::Input input;
    input.result.type = QStringLiteral("web_search");
    input.result.success = true;
    input.desktopContext.insert(QStringLiteral("threadId"), QStringLiteral("browser::research"));
    input.desktopContextAtMs = 1000;
    input.cooldownState.threadId = QStringLiteral("browser::research");
    input.cooldownState.activeUntilEpochMs = 3000;
    input.nowMs = 1500;

    const BehaviorDecision decision = ProactiveSurfaceGate::evaluateTaskToast(input);
    QVERIFY(!decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("surface.cooldown_suppressed"));
}

void ProactiveSurfaceGateTests::allowsToastOnMeaningfulThreadShift()
{
    ProactiveSurfaceGate::Input input;
    input.result.type = QStringLiteral("web_search");
    input.result.success = true;
    input.desktopContext.insert(QStringLiteral("threadId"), QStringLiteral("browser::research"));
    input.desktopContextAtMs = 1000;
    input.cooldownState.threadId = QStringLiteral("editor::coding");
    input.cooldownState.activeUntilEpochMs = 3000;
    input.nowMs = 1500;

    const BehaviorDecision decision = ProactiveSurfaceGate::evaluateTaskToast(input);
    QVERIFY(decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("surface.cooldown_thread_shift"));
}

void ProactiveSurfaceGateTests::keepsFailureToastVisible()
{
    FocusModeState focusMode;
    focusMode.enabled = true;

    ProactiveSurfaceGate::Input input;
    input.result.type = QStringLiteral("web_search");
    input.result.success = false;
    input.focusMode = focusMode;

    const BehaviorDecision decision = ProactiveSurfaceGate::evaluateTaskToast(input);
    QVERIFY(decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("surface.error_always_visible"));
}

void ProactiveSurfaceGateTests::suppressesCompletionFollowUpDuringFocusMode()
{
    FocusModeState focusMode;
    focusMode.enabled = true;

    ProactiveSurfaceGate::Input input;
    input.result.type = QStringLiteral("web_search");
    input.result.success = true;
    input.focusMode = focusMode;

    const BehaviorDecision decision = ProactiveSurfaceGate::evaluateCompletionFollowUp(input, true);
    QVERIFY(!decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("surface.follow_up_focus_mode_suppressed"));
}

void ProactiveSurfaceGateTests::softlyAllowsCompletionFollowUpDuringActiveCooldown()
{
    ProactiveSurfaceGate::Input input;
    input.result.type = QStringLiteral("web_search");
    input.result.success = true;
    input.desktopContext.insert(QStringLiteral("threadId"), QStringLiteral("browser::research"));
    input.desktopContextAtMs = 1000;
    input.cooldownState.threadId = QStringLiteral("browser::research");
    input.cooldownState.activeUntilEpochMs = 3000;
    input.nowMs = 1500;

    const BehaviorDecision decision = ProactiveSurfaceGate::evaluateCompletionFollowUp(input, true);
    QVERIFY(decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("surface.follow_up_cooldown_soft_allow"));
}

void ProactiveSurfaceGateTests::allowsUserRequestedCompletionDuringActiveCooldown()
{
    ProactiveSurfaceGate::Input input;
    input.result.type = QStringLiteral("web_search");
    input.result.success = true;
    input.desktopContext.insert(QStringLiteral("threadId"), QStringLiteral("browser::research"));
    input.desktopContextAtMs = 1000;
    input.cooldownState.threadId = QStringLiteral("browser::research");
    input.cooldownState.activeUntilEpochMs = 3000;
    input.nowMs = 1500;

    const BehaviorDecision decision = ProactiveSurfaceGate::evaluateCompletionFollowUp(input, true, true);
    QVERIFY(decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("surface.user_requested_completion_allow"));
}

void ProactiveSurfaceGateTests::allowsUserRequestedCompletionDuringFocusedDesktopWork()
{
    ProactiveSurfaceGate::Input input;
    input.result.type = QStringLiteral("browser_fetch_text");
    input.result.success = true;
    input.desktopContext.insert(QStringLiteral("taskId"), QStringLiteral("browser_tab"));
    input.desktopContextAtMs = 1000;
    input.nowMs = 1200;

    const BehaviorDecision decision = ProactiveSurfaceGate::evaluateCompletionFollowUp(input, false, true);
    QVERIFY(decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("surface.user_requested_completion_allow"));
}

void ProactiveSurfaceGateTests::allowsFailureFollowUpEvenInFocusMode()
{
    FocusModeState focusMode;
    focusMode.enabled = true;

    ProactiveSurfaceGate::Input input;
    input.result.type = QStringLiteral("file_read");
    input.result.success = false;
    input.focusMode = focusMode;

    const BehaviorDecision decision = ProactiveSurfaceGate::evaluateCompletionFollowUp(input, false);
    QVERIFY(decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("surface.follow_up_error_visible"));
}

void ProactiveSurfaceGateTests::proactiveSpeechAllowsApprovedSuggestion()
{
    const ProactiveSpeechDecision decision = ProactiveSpeechPolicy::evaluate(
        QStringLiteral("YouTube search is ready."),
        QStringLiteral("task_result_toast"),
        FocusModeState{},
        CooldownState{},
        1500);

    QVERIFY(decision.shouldSpeak);
    QCOMPARE(decision.reasonCode, QStringLiteral("proactive_speech.allowed.task_result_toast"));
}

void ProactiveSurfaceGateTests::proactiveSpeechRespectsCooldownAndFocusMode()
{
    CooldownState cooldown;
    cooldown.activeUntilEpochMs = 3000;
    ProactiveSpeechDecision cooldownDecision = ProactiveSpeechPolicy::evaluate(
        QStringLiteral("Suggestion"),
        QStringLiteral("task_result_toast"),
        FocusModeState{},
        cooldown,
        1500);
    QVERIFY(cooldownDecision.shouldSpeak);
    QCOMPARE(cooldownDecision.reasonCode, QStringLiteral("proactive_speech.cooldown_soft_allow"));
    QVERIFY(cooldownDecision.cooldownActive);

    FocusModeState focus;
    focus.enabled = true;
    focus.allowCriticalAlerts = false;
    const ProactiveSpeechDecision focusDecision = ProactiveSpeechPolicy::evaluate(
        QStringLiteral("Suggestion"),
        QStringLiteral("task_result_toast"),
        focus,
        CooldownState{},
        1500);
    QVERIFY(!focusDecision.shouldSpeak);
    QCOMPARE(focusDecision.reasonCode, QStringLiteral("proactive_speech.suppressed_focus_mode"));
}

QTEST_APPLESS_MAIN(ProactiveSurfaceGateTests)
#include "ProactiveSurfaceGateTests.moc"
