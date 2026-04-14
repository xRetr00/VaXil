#include <QtTest>

#include "cognition/ProactiveSurfaceGate.h"

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
    void suppressesCompletionFollowUpDuringActiveCooldown();
    void allowsFailureFollowUpEvenInFocusMode();
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

void ProactiveSurfaceGateTests::suppressesCompletionFollowUpDuringActiveCooldown()
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
    QVERIFY(!decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("surface.follow_up_cooldown_suppressed"));
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

QTEST_APPLESS_MAIN(ProactiveSurfaceGateTests)
#include "ProactiveSurfaceGateTests.moc"
