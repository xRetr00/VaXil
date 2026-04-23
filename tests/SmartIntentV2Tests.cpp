#include <QtTest>
#include <QDir>

#include "core/intent/ExecutionIntentPlanner.h"
#include "core/intent/IntentConfidenceCalculator.h"
#include "core/intent/LocalIntentAdvisor.h"
#include "core/intent/RouteArbitrator.h"
#include "core/intent/RoutingTraceAnalyzer.h"
#include "core/intent/RoutingTraceEmitter.h"
#include "core/intent/TurnSignalExtractor.h"
#include "core/intent/TurnStateAnalyzer.h"
#include "core/intent/UserGoalInferer.h"
#include "core/CurrentContextReferentResolver.h"

class SmartIntentV2Tests : public QObject
{
    Q_OBJECT

private slots:
    void analyzesContinuationVsNewTurn();
    void detectsConfirmationReplyState();
    void detectsCorrectionAsContinuationWhenThreadExists();
    void infersPrimaryAndSecondaryGoals();
    void plansDeterministicCandidateFromSocialPrefixCommand();
    void deterministicCandidateCarriesTaskPayload();
    void arbitratesCommandVsInfoConflictToConversation();
    void blocksBackendForTerminalIntent();
    void blocksBackendForExpandedTerminalPhrase();
    void blocksBackendForSocialQuestion();
    void arbitratesContinuationAsPriorityRoute();
    void resolvesContextReferenceToExecutionTask();
    void prefersClarificationAtHighAmbiguity();
    void marksBackendVsLocalCandidateCapabilities();
    void routesLowConfidenceToBackendAssist();
    void routesHighConfidenceStrongSignalToLocalCandidate();
    void handlesContinuationAmbiguityWithClarification();
    void keepsBackendWhenAmbiguityHighButBackendClearlyNeeded();
    void summarizesRoutingTracePatterns();
    void buildsRouteFinalTracePayload();
};

void SmartIntentV2Tests::analyzesContinuationVsNewTurn()
{
    TurnSignalExtractor extractor;
    TurnStateAnalyzer analyzer;

    TurnStateInput continuationInput;
    continuationInput.normalizedInput = QStringLiteral("open it");
    continuationInput.turnSignals = extractor.extract(continuationInput.normalizedInput);
    continuationInput.hasUsableActionThread = true;
    continuationInput.hasAnyActionThread = true;
    const TurnState continuation = analyzer.analyze(continuationInput);
    QVERIFY(continuation.isContinuation);
    QVERIFY(!continuation.isNewTurn);

    TurnStateInput newTurnInput;
    newTurnInput.normalizedInput = QStringLiteral("search latest amd news");
    newTurnInput.turnSignals = extractor.extract(newTurnInput.normalizedInput);
    newTurnInput.hasUsableActionThread = false;
    const TurnState newTurn = analyzer.analyze(newTurnInput);
    QVERIFY(newTurn.isNewTurn);
}

void SmartIntentV2Tests::detectsConfirmationReplyState()
{
    TurnSignalExtractor extractor;
    TurnStateAnalyzer analyzer;

    TurnStateInput input;
    input.normalizedInput = QStringLiteral("yes, go ahead");
    input.turnSignals = extractor.extract(input.normalizedInput);
    input.hasPendingConfirmation = true;
    const TurnState state = analyzer.analyze(input);
    QVERIFY(state.isConfirmationReply);
}

void SmartIntentV2Tests::detectsCorrectionAsContinuationWhenThreadExists()
{
    TurnSignalExtractor extractor;
    TurnStateAnalyzer analyzer;

    TurnStateInput input;
    input.normalizedInput = QStringLiteral("no, the latest model released by openai");
    input.turnSignals = extractor.extract(input.normalizedInput);
    input.hasAnyActionThread = true;
    input.hasUsableActionThread = true;
    const TurnState state = analyzer.analyze(input);
    QVERIFY(state.isCorrection);
    QVERIFY(state.correctionDetected);
    QVERIFY(state.correctionConfidence > 0.8f);
    QVERIFY(state.isContinuation);
    QVERIFY(state.reasonCodes.contains(QStringLiteral("correction.bound_previous_context")));
}

void SmartIntentV2Tests::infersPrimaryAndSecondaryGoals()
{
    TurnSignalExtractor extractor;
    TurnStateAnalyzer analyzer;
    UserGoalInferer inferer;

    TurnStateInput input;
    input.normalizedInput = QStringLiteral("hi explain this error");
    input.turnSignals = extractor.extract(input.normalizedInput);
    const TurnState state = analyzer.analyze(input);
    const TurnGoalSet goals = inferer.infer(input.turnSignals, state, false);

    QCOMPARE(goals.primaryGoal.kind, UserGoalKind::InfoQuery);
    QVERIFY(goals.mixedIntent);
    QVERIFY(goals.secondaryGoal.has_value());
}

void SmartIntentV2Tests::plansDeterministicCandidateFromSocialPrefixCommand()
{
    TurnSignalExtractor extractor;
    TurnStateAnalyzer analyzer;
    UserGoalInferer inferer;
    ExecutionIntentPlanner planner;

    const TurnSignals extracted = extractor.extract(QStringLiteral("hey open youtube"));
    TurnStateInput stateInput;
    stateInput.normalizedInput = extracted.normalizedInput;
    stateInput.turnSignals = extracted;
    const TurnState state = analyzer.analyze(stateInput);
    const TurnGoalSet goals = inferer.infer(extracted, state, true);
    AgentTask deterministicTask;
    deterministicTask.type = QStringLiteral("browser_open");
    const QList<ExecutionIntentCandidate> candidates = planner.plan(goals, extracted, true, deterministicTask);

    QVERIFY(!candidates.isEmpty());
    QCOMPARE(candidates.first().route.kind, InputRouteKind::DeterministicTasks);
}

void SmartIntentV2Tests::deterministicCandidateCarriesTaskPayload()
{
    TurnSignalExtractor extractor;
    UserGoalInferer inferer;
    ExecutionIntentPlanner planner;

    const TurnSignals extracted = extractor.extract(QStringLiteral("open youtube"));
    TurnState state;
    TurnGoalSet goals = inferer.infer(extracted, state, true);
    AgentTask deterministicTask;
    deterministicTask.type = QStringLiteral("browser_open");
    deterministicTask.args.insert(QStringLiteral("url"), QStringLiteral("https://www.youtube.com"));

    const QList<ExecutionIntentCandidate> candidates = planner.plan(goals, extracted, true, deterministicTask);
    QVERIFY(!candidates.isEmpty());
    QCOMPARE(candidates.first().route.kind, InputRouteKind::DeterministicTasks);
    QCOMPARE(candidates.first().tasks.size(), 1);
    QCOMPARE(candidates.first().route.tasks.size(), 1);
}

void SmartIntentV2Tests::arbitratesCommandVsInfoConflictToConversation()
{
    TurnSignalExtractor extractor;
    TurnStateAnalyzer analyzer;
    UserGoalInferer inferer;
    ExecutionIntentPlanner planner;
    RouteArbitrator arbitrator;

    const TurnSignals extracted = extractor.extract(QStringLiteral("what does open source mean?"));
    TurnStateInput stateInput;
    stateInput.normalizedInput = extracted.normalizedInput;
    stateInput.turnSignals = extracted;
    const TurnState state = analyzer.analyze(stateInput);
    const TurnGoalSet goals = inferer.infer(extracted, state, false);
    const QList<ExecutionIntentCandidate> candidates = planner.plan(goals, extracted, false, AgentTask{});

    InputRouteDecision policyDecision;
    policyDecision.kind = InputRouteKind::CommandExtraction;
    const RouteArbitrationResult result = arbitrator.arbitrate(
        policyDecision,
        extracted,
        state,
        goals,
        candidates,
        IntentConfidence{.signalConfidence = 0.65f, .goalConfidence = 0.62f, .executionConfidence = 0.7f, .finalConfidence = 0.68f},
        0.42f,
        IntentAdvisorSuggestion{},
        false);

    QCOMPARE(result.decision.kind, InputRouteKind::Conversation);
}

void SmartIntentV2Tests::blocksBackendForTerminalIntent()
{
    RouteArbitrator arbitrator;
    TurnSignals turnSignals;
    turnSignals.normalizedInput = QStringLiteral("never mind");
    turnSignals.matchedCues = {QStringLiteral("stop")};
    TurnState state;
    TurnGoalSet goals;
    goals.primaryGoal.kind = UserGoalKind::Unknown;
    QList<ExecutionIntentCandidate> candidates;

    ExecutionIntentCandidate terminal;
    terminal.kind = ExecutionIntentKind::LocalResponse;
    terminal.route.kind = InputRouteKind::LocalResponse;
    terminal.route.status = QStringLiteral("Conversation ended");
    terminal.score = 1.0f;
    terminal.reasonCodes = {QStringLiteral("candidate.end_conversation")};
    candidates.push_back(terminal);

    ExecutionIntentCandidate backend;
    backend.kind = ExecutionIntentKind::BackendReasoning;
    backend.route.kind = InputRouteKind::Conversation;
    backend.score = 0.8f;
    backend.requiresBackend = true;
    backend.backendPriority = 90;
    backend.reasonCodes = {QStringLiteral("candidate.backend")};
    candidates.push_back(backend);

    InputRouteDecision policyDecision;
    policyDecision.kind = InputRouteKind::Conversation;
    const RouteArbitrationResult result = arbitrator.arbitrate(
        policyDecision,
        turnSignals,
        state,
        goals,
        candidates,
        IntentConfidence{.signalConfidence = 0.4f, .goalConfidence = 0.4f, .executionConfidence = 0.6f, .finalConfidence = 0.45f},
        0.4f,
        IntentAdvisorSuggestion{.available = false, .backendNecessity = 0.9f},
        false);

    QCOMPARE(result.decision.kind, InputRouteKind::LocalResponse);
    QVERIFY(result.reasonCodes.contains(QStringLiteral("override.blocked.backend_for_terminal")));
}

void SmartIntentV2Tests::blocksBackendForExpandedTerminalPhrase()
{
    RouteArbitrator arbitrator;
    TurnSignals turnSignals;
    turnSignals.normalizedInput = QStringLiteral("okay you can go");
    TurnState state;
    TurnGoalSet goals;
    QList<ExecutionIntentCandidate> candidates;

    ExecutionIntentCandidate backend;
    backend.kind = ExecutionIntentKind::BackendReasoning;
    backend.route.kind = InputRouteKind::Conversation;
    backend.score = 0.9f;
    backend.requiresBackend = true;
    backend.backendPriority = 99;
    candidates.push_back(backend);

    InputRouteDecision policyDecision;
    policyDecision.kind = InputRouteKind::Conversation;
    const RouteArbitrationResult result = arbitrator.arbitrate(
        policyDecision,
        turnSignals,
        state,
        goals,
        candidates,
        IntentConfidence{.signalConfidence = 0.4f, .goalConfidence = 0.4f, .executionConfidence = 0.5f, .finalConfidence = 0.45f},
        0.2f,
        IntentAdvisorSuggestion{.backendNecessity = 1.0f},
        false);

    QCOMPARE(result.decision.kind, InputRouteKind::LocalResponse);
    QVERIFY(result.reasonCodes.contains(QStringLiteral("override.blocked.backend_for_terminal")));
}

void SmartIntentV2Tests::blocksBackendForSocialQuestion()
{
    RouteArbitrator arbitrator;
    TurnSignals turnSignals;
    turnSignals.hasGreeting = true;
    turnSignals.hasSmallTalk = true;
    turnSignals.hasQuestionCue = true;
    turnSignals.matchedCues = {QStringLiteral("hello"), QStringLiteral("how are you")};
    TurnState state;
    TurnGoalSet goals;
    goals.primaryGoal.kind = UserGoalKind::InfoQuery;
    QList<ExecutionIntentCandidate> candidates;

    ExecutionIntentCandidate backend;
    backend.kind = ExecutionIntentKind::BackendReasoning;
    backend.route.kind = InputRouteKind::Conversation;
    backend.score = 0.84f;
    backend.requiresBackend = true;
    backend.backendPriority = 90;
    backend.reasonCodes = {QStringLiteral("candidate.backend")};
    candidates.push_back(backend);

    InputRouteDecision policyDecision;
    policyDecision.kind = InputRouteKind::Conversation;
    const RouteArbitrationResult result = arbitrator.arbitrate(
        policyDecision,
        turnSignals,
        state,
        goals,
        candidates,
        IntentConfidence{.signalConfidence = 0.7f, .goalConfidence = 0.62f, .executionConfidence = 0.6f, .finalConfidence = 0.61f},
        0.3f,
        IntentAdvisorSuggestion{.available = false, .backendNecessity = 0.85f},
        false);

    QCOMPARE(result.decision.kind, InputRouteKind::LocalResponse);
    QVERIFY(result.reasonCodes.contains(QStringLiteral("override.blocked.backend_for_social")));
}

void SmartIntentV2Tests::arbitratesContinuationAsPriorityRoute()
{
    RouteArbitrator arbitrator;
    TurnSignals turnSignals;
    turnSignals.hasContinuationCue = true;
    TurnState state;
    state.isContinuation = true;
    TurnGoalSet goals;
    goals.primaryGoal.kind = UserGoalKind::Continuation;
    QList<ExecutionIntentCandidate> candidates;
    ExecutionIntentCandidate continuation;
    continuation.kind = ExecutionIntentKind::AgentConversation;
    continuation.route.kind = InputRouteKind::AgentConversation;
    continuation.score = 0.9f;
    continuation.reasonCodes = {QStringLiteral("candidate.continuation")};
    candidates.push_back(continuation);

    InputRouteDecision policyDecision;
    policyDecision.kind = InputRouteKind::Conversation;
    const RouteArbitrationResult result = arbitrator.arbitrate(
        policyDecision,
        turnSignals,
        state,
        goals,
        candidates,
        IntentConfidence{.signalConfidence = 0.7f, .goalConfidence = 0.72f, .executionConfidence = 0.8f, .finalConfidence = 0.76f},
        0.35f,
        IntentAdvisorSuggestion{.available = false, .ambiguityBoost = 0.0f, .continuationLikelihood = 0.9f, .backendNecessity = 0.2f},
        false);
    QCOMPARE(result.decision.kind, InputRouteKind::AgentConversation);
}

void SmartIntentV2Tests::resolvesContextReferenceToExecutionTask()
{
    const CurrentContextResolution resolution = CurrentContextReferentResolver::resolve({
        .userInput = QStringLiteral("read the current page"),
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("browser_tab")},
            {QStringLiteral("url"), QStringLiteral("https://example.com")}
        },
        .desktopContextAtMs = 1000,
        .nowMs = 1200,
        .workspaceRoot = QDir::currentPath()
    });

    QCOMPARE(resolution.kind, CurrentContextResolutionKind::Task);
    QCOMPARE(resolution.decision.kind, InputRouteKind::BackgroundTasks);
    QCOMPARE(resolution.decision.tasks.first().type, QStringLiteral("browser_fetch_text"));
}

void SmartIntentV2Tests::prefersClarificationAtHighAmbiguity()
{
    RouteArbitrator arbitrator;
    TurnSignals turnSignals;
    TurnState state;
    TurnGoalSet goals;
    goals.ambiguity = 0.8f;
    QList<ExecutionIntentCandidate> candidates;
    ExecutionIntentCandidate clarify;
    clarify.kind = ExecutionIntentKind::AskClarification;
    clarify.route.kind = InputRouteKind::LocalResponse;
    clarify.score = 0.91f;
    clarify.reasonCodes = {QStringLiteral("candidate.high_ambiguity")};
    candidates.push_back(clarify);

    InputRouteDecision policyDecision;
    policyDecision.kind = InputRouteKind::Conversation;
    const RouteArbitrationResult result = arbitrator.arbitrate(
        policyDecision,
        turnSignals,
        state,
        goals,
        candidates,
        IntentConfidence{.signalConfidence = 0.3f, .goalConfidence = 0.3f, .executionConfidence = 0.2f, .finalConfidence = 0.28f},
        0.82f,
        IntentAdvisorSuggestion{.available = false, .ambiguityBoost = 0.1f, .continuationLikelihood = 0.2f, .backendNecessity = 0.8f},
        false);
    QCOMPARE(result.decision.kind, InputRouteKind::LocalResponse);
    QVERIFY(result.reasonCodes.contains(QStringLiteral("arbitrator.ask_clarification")));
}

void SmartIntentV2Tests::marksBackendVsLocalCandidateCapabilities()
{
    TurnSignalExtractor extractor;
    TurnStateAnalyzer analyzer;
    UserGoalInferer inferer;
    ExecutionIntentPlanner planner;

    const TurnSignals extracted = extractor.extract(QStringLiteral("what does caching strategy mean?"));
    TurnStateInput stateInput;
    stateInput.normalizedInput = extracted.normalizedInput;
    stateInput.turnSignals = extracted;
    const TurnState state = analyzer.analyze(stateInput);
    const TurnGoalSet goals = inferer.infer(extracted, state, false);
    const QList<ExecutionIntentCandidate> candidates = planner.plan(goals, extracted, false, AgentTask{});

    QVERIFY(!candidates.isEmpty());
    QVERIFY(candidates.first().requiresBackend);
    QVERIFY(!candidates.first().canRunLocal);
}

void SmartIntentV2Tests::routesLowConfidenceToBackendAssist()
{
    RouteArbitrator arbitrator;
    TurnSignals turnSignals;
    turnSignals.hasQuestionCue = true;
    TurnState state;
    TurnGoalSet goals;
    goals.primaryGoal.kind = UserGoalKind::InfoQuery;
    goals.primaryGoal.confidence = 0.35f;
    QList<ExecutionIntentCandidate> candidates;

    ExecutionIntentCandidate backend;
    backend.kind = ExecutionIntentKind::BackendReasoning;
    backend.route.kind = InputRouteKind::Conversation;
    backend.score = 0.72f;
    backend.requiresBackend = true;
    backend.backendPriority = 90;
    backend.reasonCodes = {QStringLiteral("candidate.backend")};
    candidates.push_back(backend);

    InputRouteDecision policyDecision;
    policyDecision.kind = InputRouteKind::CommandExtraction;
    const RouteArbitrationResult result = arbitrator.arbitrate(
        policyDecision,
        turnSignals,
        state,
        goals,
        candidates,
        IntentConfidence{.signalConfidence = 0.33f, .goalConfidence = 0.35f, .executionConfidence = 0.4f, .finalConfidence = 0.38f},
        0.45f,
        IntentAdvisorSuggestion{.available = false, .ambiguityBoost = 0.0f, .continuationLikelihood = 0.1f, .backendNecessity = 0.75f},
        false);

    QCOMPARE(result.decision.kind, InputRouteKind::Conversation);
    QVERIFY(result.reasonCodes.contains(QStringLiteral("arbitrator.backend_escalation")));
}

void SmartIntentV2Tests::routesHighConfidenceStrongSignalToLocalCandidate()
{
    RouteArbitrator arbitrator;
    TurnSignals turnSignals;
    turnSignals.socialOnly = true;
    turnSignals.hasDeterministicCue = true;
    TurnState state;
    TurnGoalSet goals;
    goals.primaryGoal.kind = UserGoalKind::DeterministicAction;
    QList<ExecutionIntentCandidate> candidates;

    ExecutionIntentCandidate local;
    local.kind = ExecutionIntentKind::DeterministicTask;
    local.route.kind = InputRouteKind::DeterministicTasks;
    local.score = 0.95f;
    local.canRunLocal = true;
    local.requiresBackend = false;
    local.reasonCodes = {QStringLiteral("candidate.local_deterministic")};
    candidates.push_back(local);

    InputRouteDecision policyDecision;
    policyDecision.kind = InputRouteKind::Conversation;
    const RouteArbitrationResult result = arbitrator.arbitrate(
        policyDecision,
        turnSignals,
        state,
        goals,
        candidates,
        IntentConfidence{.signalConfidence = 0.9f, .goalConfidence = 0.88f, .executionConfidence = 0.9f, .finalConfidence = 0.89f},
        0.1f,
        IntentAdvisorSuggestion{},
        true);

    QCOMPARE(result.decision.kind, InputRouteKind::DeterministicTasks);
}

void SmartIntentV2Tests::handlesContinuationAmbiguityWithClarification()
{
    RouteArbitrator arbitrator;
    TurnSignals turnSignals;
    turnSignals.hasContinuationCue = true;
    TurnState state;
    state.isContinuation = true;
    TurnGoalSet goals;
    goals.primaryGoal.kind = UserGoalKind::Continuation;
    QList<ExecutionIntentCandidate> candidates;

    ExecutionIntentCandidate continuation;
    continuation.kind = ExecutionIntentKind::AgentConversation;
    continuation.route.kind = InputRouteKind::AgentConversation;
    continuation.score = 0.7f;
    continuation.reasonCodes = {QStringLiteral("candidate.continuation")};
    candidates.push_back(continuation);

    InputRouteDecision policyDecision;
    policyDecision.kind = InputRouteKind::Conversation;
    const RouteArbitrationResult result = arbitrator.arbitrate(
        policyDecision,
        turnSignals,
        state,
        goals,
        candidates,
        IntentConfidence{.signalConfidence = 0.3f, .goalConfidence = 0.2f, .executionConfidence = 0.3f, .finalConfidence = 0.24f},
        0.75f,
        IntentAdvisorSuggestion{.available = false, .ambiguityBoost = 0.1f, .continuationLikelihood = 0.85f, .backendNecessity = 0.7f},
        false);

    QCOMPARE(result.decision.kind, InputRouteKind::LocalResponse);
    QVERIFY(result.reasonCodes.contains(QStringLiteral("arbitrator.ask_clarification")));
}

void SmartIntentV2Tests::keepsBackendWhenAmbiguityHighButBackendClearlyNeeded()
{
    RouteArbitrator arbitrator;
    TurnSignals turnSignals;
    turnSignals.hasQuestionCue = true;
    TurnState state;
    TurnGoalSet goals;
    goals.primaryGoal.kind = UserGoalKind::InfoQuery;
    QList<ExecutionIntentCandidate> candidates;

    ExecutionIntentCandidate backend;
    backend.kind = ExecutionIntentKind::AgentConversation;
    backend.route.kind = InputRouteKind::AgentConversation;
    backend.score = 0.82f;
    backend.requiresBackend = true;
    backend.backendPriority = 90;
    backend.reasonCodes = {QStringLiteral("candidate.backend_agent")};
    candidates.push_back(backend);

    InputRouteDecision policyDecision;
    policyDecision.kind = InputRouteKind::Conversation;
    const RouteArbitrationResult result = arbitrator.arbitrate(
        policyDecision,
        turnSignals,
        state,
        goals,
        candidates,
        IntentConfidence{.signalConfidence = 0.25f, .goalConfidence = 0.3f, .executionConfidence = 0.3f, .finalConfidence = 0.35f},
        0.74f,
        IntentAdvisorSuggestion{.available = false, .ambiguityBoost = 0.0f, .continuationLikelihood = 0.1f, .backendNecessity = 0.9f},
        false);

    QCOMPARE(result.decision.kind, InputRouteKind::AgentConversation);
    QVERIFY(result.reasonCodes.contains(QStringLiteral("arbitrator.backend_escalation")));
    QVERIFY(result.reasonCodes.contains(QStringLiteral("arbitrator.high_ambiguity_backend_needed")));
}

void SmartIntentV2Tests::summarizesRoutingTracePatterns()
{
    RoutingTraceAnalyzer analyzer;
    QList<RoutingTrace> traces;

    RoutingTrace first;
    first.finalDecision.kind = InputRouteKind::None;
    first.finalExecutedRoute = QStringLiteral("none");
    first.overridesApplied = {QStringLiteral("override.fallback_conversation")};
    first.ambiguityScore = 0.8f;
    traces.push_back(first);

    RoutingTrace second;
    second.finalDecision.kind = InputRouteKind::Conversation;
    second.finalExecutedRoute = QStringLiteral("conversation");
    second.overridesApplied = {QStringLiteral("override.arbitrator_over_policy")};
    traces.push_back(second);

    const RoutingTraceAnalyzerSummary summary = analyzer.summarize(traces);
    QCOMPARE(summary.total, 2);
    QCOMPARE(summary.misroutes, 1);
    QCOMPARE(summary.fallbackCount, 1);
    QVERIFY(summary.overrideCount >= 2);
    QVERIFY(summary.highAmbiguityCount >= 1);
}

void SmartIntentV2Tests::buildsRouteFinalTracePayload()
{
    RoutingTrace trace;
    trace.rawInput = QStringLiteral("thanks, why is this slow?");
    trace.normalizedInput = QStringLiteral("thanks, why is this slow?");
    trace.turnSignals.hasSmallTalk = true;
    trace.turnSignals.hasQuestionCue = true;
    trace.turnState.isNewTurn = true;
    trace.turnState.reasonCodes = {QStringLiteral("turn_state.new_turn")};
    trace.ambiguityScore = 0.62f;
    trace.deterministicTaskPayloadPresent = true;
    trace.intentConfidence = IntentConfidence{.signalConfidence = 0.54f, .goalConfidence = 0.48f, .executionConfidence = 0.41f, .finalConfidence = 0.46f};
    trace.advisorSuggestion = IntentAdvisorSuggestion{.available = false, .ambiguityBoost = 0.2f, .continuationLikelihood = 0.1f, .backendNecessity = 0.7f};
    trace.policyDecision.kind = InputRouteKind::CommandExtraction;
    trace.finalDecision.kind = InputRouteKind::Conversation;
    trace.finalExecutedRoute = QStringLiteral("conversation");
    trace.overridesApplied = {QStringLiteral("arbitrator_override:CommandExtraction->Conversation")};
    trace.reasonCodes = {QStringLiteral("arbitrator.command_vs_info_prefers_info_query")};

    RoutingTraceEmitter emitter;
    const QJsonObject payload = emitter.buildRouteFinalPayload(trace);
    QCOMPARE(payload.value(QStringLiteral("record")).toString(), QStringLiteral("route_final"));
    QCOMPARE(payload.value(QStringLiteral("final_executed_route")).toString(), QStringLiteral("conversation"));
    QVERIFY(payload.contains(QStringLiteral("intent_confidence")));
    QVERIFY(payload.contains(QStringLiteral("advisor_suggestion")));
    QVERIFY(payload.contains(QStringLiteral("ambiguity_score")));
    QVERIFY(payload.contains(QStringLiteral("deterministic_task_payload_present")));
    QVERIFY(payload.contains(QStringLiteral("turn_signals")));
    QVERIFY(payload.contains(QStringLiteral("policy_decision")));
    QVERIFY(payload.contains(QStringLiteral("final_decision")));
    QVERIFY(payload.contains(QStringLiteral("context_relevance_score")));
    QVERIFY(payload.contains(QStringLiteral("provider_tool_compatibility_mode")));
    QVERIFY(payload.contains(QStringLiteral("proactive_suggestion_spoken")));
}

QTEST_APPLESS_MAIN(SmartIntentV2Tests)
#include "SmartIntentV2Tests.moc"
