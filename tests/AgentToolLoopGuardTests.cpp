#include <QtTest>

#include "core/tools/AgentToolLoopGuard.h"

namespace {
AgentToolResult makeResult(const QString &toolName, bool success, const QString &output = {})
{
    AgentToolResult result;
    result.toolName = toolName;
    result.callId = toolName + QStringLiteral("_call");
    result.success = success;
    result.output = output;
    result.summary = output;
    return result;
}
}

class AgentToolLoopGuardTests : public QObject
{
    Q_OBJECT

private slots:
    void doesNotStopAfterUsefulResult();
    void stopsAfterRepeatedFailedTools();
    void stopsAfterRepeatedLowSignalSameFamily();
    void classifiesToolFamilies();
};

void AgentToolLoopGuardTests::doesNotStopAfterUsefulResult()
{
    AgentToolLoopGuardState state;
    const AgentToolLoopGuardDecision decision = AgentToolLoopGuard::evaluateResults(
        {makeResult(QStringLiteral("web_search"), true, QStringLiteral("Useful result with enough evidence to continue."))},
        &state);
    QVERIFY(!decision.stop);
    QCOMPARE(state.totalToolCalls, 1);
    QCOMPARE(state.failedToolAttempts, 0);
}

void AgentToolLoopGuardTests::stopsAfterRepeatedFailedTools()
{
    AgentToolLoopGuardState state;
    AgentToolLoopGuardConfig config;
    config.maxFailedToolCallsPerTurn = 2;

    QVERIFY(!AgentToolLoopGuard::evaluateResults(
                 {makeResult(QStringLiteral("web_search"), false)}, &state, config).stop);
    const AgentToolLoopGuardDecision decision = AgentToolLoopGuard::evaluateResults(
        {makeResult(QStringLiteral("browser_fetch_text"), false)}, &state, config);

    QVERIFY(decision.stop);
    QCOMPARE(decision.reasonCode, QStringLiteral("tool_loop.failed_attempts"));
    QCOMPARE(decision.failedToolAttemptCount, 2);
    QVERIFY(decision.reasonCodes.contains(QStringLiteral("technical_guard.tool_loop_breaker")));
}

void AgentToolLoopGuardTests::stopsAfterRepeatedLowSignalSameFamily()
{
    AgentToolLoopGuardState state;
    AgentToolLoopGuardConfig config;
    config.maxSameFamilyAttemptsPerTurn = 2;
    config.maxLowSignalToolResultsPerTurn = 10;

    QVERIFY(!AgentToolLoopGuard::evaluateResults(
                 {makeResult(QStringLiteral("browser_fetch_text"), true)}, &state, config).stop);
    const AgentToolLoopGuardDecision decision = AgentToolLoopGuard::evaluateResults(
        {makeResult(QStringLiteral("browser_fetch_text"), true)}, &state, config);

    QVERIFY(decision.stop);
    QCOMPARE(decision.reasonCode, QStringLiteral("tool_loop.same_family_repeated_low_signal"));
    QCOMPARE(decision.sameFamilyAttemptCount, 2);
}

void AgentToolLoopGuardTests::classifiesToolFamilies()
{
    QCOMPARE(AgentToolLoopGuard::toolFamily(QStringLiteral("web_search")), QStringLiteral("web"));
    QCOMPARE(AgentToolLoopGuard::toolFamily(QStringLiteral("browser_fetch_text")), QStringLiteral("browser"));
    QCOMPARE(AgentToolLoopGuard::toolFamily(QStringLiteral("file_read")), QStringLiteral("file"));
    QCOMPARE(AgentToolLoopGuard::toolFamily(QStringLiteral("computer_open_url")), QStringLiteral("browser"));
}

QTEST_APPLESS_MAIN(AgentToolLoopGuardTests)
#include "AgentToolLoopGuardTests.moc"
