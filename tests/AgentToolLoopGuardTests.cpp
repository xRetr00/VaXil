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
    void doesNotStopAfterSuccessThenSingleFailure();
    void doesNotStopAfterEvidenceThenSingleFailure();
    void stopsAfterEvidenceThenCrossFamilyDrift();
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

void AgentToolLoopGuardTests::doesNotStopAfterSuccessThenSingleFailure()
{
    AgentToolLoopGuardState state;
    AgentToolLoopGuardConfig config;
    config.maxSameFamilyAttemptsPerTurn = 2;
    config.maxLowSignalToolResultsPerTurn = 10;
    config.maxFailedToolCallsPerTurn = 5;

    const AgentToolLoopGuardDecision first = AgentToolLoopGuard::evaluateResults(
        {makeResult(QStringLiteral("computer_open_url"), true, QStringLiteral("opened target url"))},
        &state,
        config);
    QVERIFY(!first.stop);
    QCOMPARE(state.consecutiveFailureCount, 0);
    QVERIFY(state.lastToolSuccess);

    const AgentToolLoopGuardDecision second = AgentToolLoopGuard::evaluateResults(
        {makeResult(QStringLiteral("web_fetch"), false)},
        &state,
        config);
    QVERIFY(!second.stop);
    QCOMPARE(second.consecutiveFailureCount, 1);
    QVERIFY(!second.lastToolSuccess);
}

void AgentToolLoopGuardTests::doesNotStopAfterEvidenceThenSingleFailure()
{
    AgentToolLoopGuardState state;
    AgentToolLoopGuardConfig config;
    config.maxFailedToolCallsPerTurn = 5;

    const AgentToolLoopGuardDecision first = AgentToolLoopGuard::evaluateResults(
        {[] {
            AgentToolResult result = makeResult(QStringLiteral("web_search"), true, QStringLiteral("OpenAI release result with enough evidence."));
            result.payload = QJsonObject{{QStringLiteral("text"), QStringLiteral("OpenAI release result with enough evidence.")}};
            return result;
        }()},
        &state,
        config);
    QVERIFY(!first.stop);
    QVERIFY(first.evidenceSufficient);

    const AgentToolLoopGuardDecision second = AgentToolLoopGuard::evaluateResults(
        {makeResult(QStringLiteral("web_fetch"), false)},
        &state,
        config);
    QVERIFY(!second.stop);
    QCOMPARE(second.consecutiveFailureCount, 1);
}

void AgentToolLoopGuardTests::stopsAfterEvidenceThenCrossFamilyDrift()
{
    AgentToolLoopGuardState state;
    AgentToolLoopGuardConfig config;
    config.maxFailedToolCallsPerTurn = 10;

    const AgentToolLoopGuardDecision first = AgentToolLoopGuard::evaluateResults(
        {[] {
            AgentToolResult result = makeResult(QStringLiteral("web_search"), true, QStringLiteral("OpenAI release result with enough evidence."));
            result.payload = QJsonObject{{QStringLiteral("text"), QStringLiteral("OpenAI release result with enough evidence.")}};
            return result;
        }()},
        &state,
        config);
    QVERIFY(!first.stop);
    QVERIFY(state.evidenceSufficient);

    const AgentToolLoopGuardDecision second = AgentToolLoopGuard::evaluateResults(
        {makeResult(QStringLiteral("file_read"), true, QStringLiteral("Unrelated local file result."))},
        &state,
        config);
    QVERIFY(second.stop);
    QCOMPARE(second.reasonCode, QStringLiteral("tool_loop.cross_family_drift"));
    QVERIFY(second.toolDriftDetected);
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
