#include "core/intent/RoutingReplayHarness.h"

#include <algorithm>

#include "core/CurrentContextReferentResolver.h"

namespace {
InputRouteKind routeKindFromString(const QString &value)
{
    const QString lowered = value.trimmed().toLower();
    if (lowered == QStringLiteral("localresponse") || lowered == QStringLiteral("local_response")) {
        return InputRouteKind::LocalResponse;
    }
    if (lowered == QStringLiteral("deterministictasks") || lowered == QStringLiteral("deterministic_tasks")) {
        return InputRouteKind::DeterministicTasks;
    }
    if (lowered == QStringLiteral("backgroundtasks") || lowered == QStringLiteral("background_tasks")) {
        return InputRouteKind::BackgroundTasks;
    }
    if (lowered == QStringLiteral("conversation")) {
        return InputRouteKind::Conversation;
    }
    if (lowered == QStringLiteral("agentconversation") || lowered == QStringLiteral("agent_conversation")) {
        return InputRouteKind::AgentConversation;
    }
    if (lowered == QStringLiteral("commandextraction") || lowered == QStringLiteral("command_extraction")) {
        return InputRouteKind::CommandExtraction;
    }
    if (lowered == QStringLiteral("agentcapabilityerror") || lowered == QStringLiteral("agent_capability_error")) {
        return InputRouteKind::AgentCapabilityError;
    }
    return InputRouteKind::None;
}
}

RoutingReplayFixture RoutingReplayHarness::fixtureFromJson(const QJsonObject &object) const
{
    RoutingReplayFixture fixture;
    fixture.name = object.value(QStringLiteral("name")).toString();
    fixture.input = object.value(QStringLiteral("input")).toString();
    fixture.hasDeterministicTask = object.value(QStringLiteral("hasDeterministicTask")).toBool(false);
    fixture.deterministicTaskType = object.value(QStringLiteral("deterministicTaskType")).toString();
    fixture.hasPendingConfirmation = object.value(QStringLiteral("hasPendingConfirmation")).toBool(false);
    fixture.hasUsableActionThread = object.value(QStringLiteral("hasUsableActionThread")).toBool(false);
    fixture.hasAnyActionThread = object.value(QStringLiteral("hasAnyActionThread")).toBool(false);
    fixture.includeContextResolution = object.value(QStringLiteral("includeContextResolution")).toBool(false);
    fixture.desktopContext = object.value(QStringLiteral("desktopContext")).toObject().toVariantMap();
    fixture.desktopContextAtMs = object.value(QStringLiteral("desktopContextAtMs")).toInteger(0);
    fixture.nowMs = object.value(QStringLiteral("nowMs")).toInteger(0);
    fixture.workspaceRoot = object.value(QStringLiteral("workspaceRoot")).toString();
    fixture.ambiguityOverride = static_cast<float>(object.value(QStringLiteral("ambiguityOverride")).toDouble(-1.0));
    fixture.confidenceOverride = static_cast<float>(object.value(QStringLiteral("confidenceOverride")).toDouble(-1.0));
    fixture.expectedFinalRoute = routeKindFromString(object.value(QStringLiteral("expectedFinalRoute")).toString());
    fixture.expectedTopCandidateRoute = routeKindFromString(object.value(QStringLiteral("expectedTopCandidateRoute")).toString());
    fixture.expectedClarification = object.value(QStringLiteral("expectedClarification")).toBool(false);
    fixture.expectedBackendEscalation = object.value(QStringLiteral("expectedBackendEscalation")).toBool(false);
    const QJsonArray reasons = object.value(QStringLiteral("requiredReasonCodes")).toArray();
    for (const QJsonValue &value : reasons) {
        fixture.requiredReasonCodes.push_back(value.toString());
    }
    return fixture;
}

QList<RoutingReplayFixture> RoutingReplayHarness::fixturesFromJsonArray(const QJsonArray &array) const
{
    QList<RoutingReplayFixture> fixtures;
    fixtures.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        fixtures.push_back(fixtureFromJson(value.toObject()));
    }
    return fixtures;
}

RoutingReplayResult RoutingReplayHarness::replay(const RoutingReplayFixture &fixture,
                                                 IntentAdvisorMode advisorMode) const
{
    RoutingReplayResult result;
    TurnSignalExtractor signalExtractor;
    TurnStateAnalyzer stateAnalyzer;
    UserGoalInferer goalInferer;
    ExecutionIntentPlanner planner;
    IntentConfidenceCalculator confidenceCalculator;
    LocalIntentAdvisor advisor;
    RouteArbitrator arbitrator;

    result.extractedSignals = signalExtractor.extract(fixture.input);
    TurnStateInput stateInput;
    stateInput.normalizedInput = result.extractedSignals.normalizedInput;
    stateInput.turnSignals = result.extractedSignals;
    stateInput.hasPendingConfirmation = fixture.hasPendingConfirmation;
    stateInput.hasUsableActionThread = fixture.hasUsableActionThread;
    stateInput.hasAnyActionThread = fixture.hasAnyActionThread;
    result.state = stateAnalyzer.analyze(stateInput);
    result.goals = goalInferer.infer(result.extractedSignals, result.state, fixture.hasDeterministicTask);
    if (result.state.isContinuation && !fixture.hasUsableActionThread) {
        result.goals.ambiguity = std::max(result.goals.ambiguity, 0.9f);
    }
    if (fixture.ambiguityOverride >= 0.0f) {
        result.goals.ambiguity = std::clamp(fixture.ambiguityOverride, 0.0f, 1.0f);
    }

    AgentTask deterministicTask;
    deterministicTask.type = fixture.deterministicTaskType.trimmed().isEmpty()
        ? QStringLiteral("browser_open")
        : fixture.deterministicTaskType.trimmed();
    result.candidates = planner.plan(result.goals, result.extractedSignals, fixture.hasDeterministicTask, deterministicTask);
    for (ExecutionIntentCandidate &candidate : result.candidates) {
        if (result.state.isContinuation && !fixture.hasUsableActionThread) {
            candidate.confidencePenalty = std::clamp(candidate.confidencePenalty + 0.1f, 0.0f, 0.4f);
            candidate.backendPriority = std::max(candidate.backendPriority, 80);
            candidate.reasonCodes.push_back(QStringLiteral("replay.continuation_missing_context_penalty"));
        }
    }

    if (fixture.includeContextResolution) {
        const CurrentContextResolution contextResolution = CurrentContextReferentResolver::resolve({
            .userInput = fixture.input,
            .desktopContext = fixture.desktopContext,
            .desktopContextAtMs = fixture.desktopContextAtMs,
            .nowMs = fixture.nowMs,
            .workspaceRoot = fixture.workspaceRoot.trimmed().isEmpty() ? QStringLiteral(".") : fixture.workspaceRoot
        });
        if (contextResolution.kind != CurrentContextResolutionKind::None) {
            ExecutionIntentCandidate contextCandidate;
            contextCandidate.kind = (contextResolution.kind == CurrentContextResolutionKind::Task)
                ? ExecutionIntentKind::BackgroundTask
                : ExecutionIntentKind::AskClarification;
            contextCandidate.route = contextResolution.decision;
            if (contextResolution.kind != CurrentContextResolutionKind::Task) {
                contextCandidate.route.kind = InputRouteKind::LocalResponse;
                contextCandidate.route.message = contextResolution.message;
                contextCandidate.route.status = contextResolution.status;
            }
            contextCandidate.score = (contextResolution.kind == CurrentContextResolutionKind::Task) ? 0.9f : 0.88f;
            contextCandidate.canRunLocal = true;
            contextCandidate.requiresBackend = false;
            contextCandidate.backendPriority = 15;
            contextCandidate.reasonCodes = {contextResolution.reasonCode};
            result.candidates.push_back(contextCandidate);
        }
    }

    std::sort(result.candidates.begin(), result.candidates.end(), [](const ExecutionIntentCandidate &left, const ExecutionIntentCandidate &right) {
        if (left.score == right.score) {
            return left.backendPriority > right.backendPriority;
        }
        return left.score > right.score;
    });

    result.confidence = confidenceCalculator.compute(result.extractedSignals, result.goals, result.candidates);
    if (fixture.confidenceOverride >= 0.0f) {
        result.confidence.finalConfidence = std::clamp(fixture.confidenceOverride, 0.0f, 1.0f);
    }
    result.ambiguityScore = confidenceCalculator.computeAmbiguity(result.extractedSignals, result.goals, result.candidates, result.confidence);
    result.advisorSuggestion = advisor.suggest(result.extractedSignals, result.goals, result.state, result.candidates, advisorMode);

    InputRouteDecision policyDecision;
    if (!result.candidates.isEmpty()) {
        policyDecision = result.candidates.first().route;
    }
    result.arbitration = arbitrator.arbitrate(policyDecision,
                                              result.extractedSignals,
                                              result.state,
                                              result.goals,
                                              result.candidates,
                                              result.confidence,
                                              result.ambiguityScore,
                                              result.advisorSuggestion,
                                              fixture.hasDeterministicTask);
    return result;
}
