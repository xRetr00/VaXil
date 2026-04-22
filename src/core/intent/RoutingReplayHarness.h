#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>

#include "core/AssistantTypes.h"
#include "core/intent/ExecutionIntentPlanner.h"
#include "core/intent/IntentConfidenceCalculator.h"
#include "core/intent/LocalIntentAdvisor.h"
#include "core/intent/RouteArbitrator.h"
#include "core/intent/TurnSignalExtractor.h"
#include "core/intent/TurnStateAnalyzer.h"
#include "core/intent/UserGoalInferer.h"

struct RoutingReplayFixture
{
    QString name;
    QString input;
    bool hasDeterministicTask = false;
    QString deterministicTaskType;
    bool hasPendingConfirmation = false;
    bool hasUsableActionThread = false;
    bool hasAnyActionThread = false;
    bool includeContextResolution = false;
    QVariantMap desktopContext;
    qint64 desktopContextAtMs = 0;
    qint64 nowMs = 0;
    QString workspaceRoot;
    float ambiguityOverride = -1.0f;
    float confidenceOverride = -1.0f;
    InputRouteKind expectedFinalRoute = InputRouteKind::None;
    bool expectedClarification = false;
    bool expectedBackendEscalation = false;
    InputRouteKind expectedTopCandidateRoute = InputRouteKind::None;
    QStringList requiredReasonCodes;
};

struct RoutingReplayResult
{
    TurnSignals extractedSignals;
    TurnState state;
    TurnGoalSet goals;
    QList<ExecutionIntentCandidate> candidates;
    IntentConfidence confidence;
    float ambiguityScore = 0.0f;
    IntentAdvisorSuggestion advisorSuggestion;
    RouteArbitrationResult arbitration;
};

class RoutingReplayHarness
{
public:
    RoutingReplayFixture fixtureFromJson(const QJsonObject &object) const;
    QList<RoutingReplayFixture> fixturesFromJsonArray(const QJsonArray &array) const;
    RoutingReplayResult replay(const RoutingReplayFixture &fixture,
                               IntentAdvisorMode advisorMode = IntentAdvisorMode::Heuristic) const;
};
