#include <QtTest>

#include "cognition/CooldownEngine.h"

class CooldownEngineTests : public QObject
{
    Q_OBJECT

private slots:
    void suppressesNonCriticalDuringFocusMode();
    void allowsMeaningfulThreadShift();
    void breaksCooldownForHighNoveltyHighConfidence();
};

void CooldownEngineTests::suppressesNonCriticalDuringFocusMode()
{
    CooldownEngine engine;
    CooldownEngine::Input input;
    input.focusMode.enabled = true;
    input.focusMode.allowCriticalAlerts = true;
    input.priority = QStringLiteral("medium");
    input.nowMs = 1000;

    const BehaviorDecision decision = engine.evaluate(input);
    QVERIFY(!decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("focus_mode.suppressed"));
}

void CooldownEngineTests::allowsMeaningfulThreadShift()
{
    CooldownEngine engine;
    CooldownEngine::Input input;
    input.state.threadId = QStringLiteral("browser::research");
    input.state.activeUntilEpochMs = 4000;
    input.context.threadId = ContextThreadId::fromParts(
        { QStringLiteral("editor"), QStringLiteral("coding"), QStringLiteral("planner") });
    input.confidence = 0.90;
    input.nowMs = 1000;

    const BehaviorDecision decision = engine.evaluate(input);
    QVERIFY(decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("cooldown.thread_shift"));
}

void CooldownEngineTests::breaksCooldownForHighNoveltyHighConfidence()
{
    CooldownEngine engine;
    CooldownEngine::Input input;
    input.state.threadId = QStringLiteral("browser::research");
    input.state.activeUntilEpochMs = 5000;
    input.context.threadId = ContextThreadId::fromParts(
        { QStringLiteral("browser"), QStringLiteral("research") });
    input.priority = QStringLiteral("high");
    input.confidence = 0.90;
    input.novelty = 0.90;
    input.nowMs = 1000;

    const BehaviorDecision decision = engine.evaluate(input);
    QVERIFY(decision.allowed);
    QCOMPARE(decision.action, QStringLiteral("break_cooldown"));
}

QTEST_APPLESS_MAIN(CooldownEngineTests)
#include "CooldownEngineTests.moc"
