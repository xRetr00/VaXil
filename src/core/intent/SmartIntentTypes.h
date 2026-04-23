#pragma once

#include <optional>

#include <QList>
#include <QString>
#include <QStringList>

#include "core/AssistantTypes.h"

enum class TurnSignalOwner {
    TurnSignalExtractor,
    TurnStateAnalyzer,
    UserGoalInferer,
    ExecutionIntentPlanner,
    RouteArbitrator
};

enum class UserGoalKind {
    Social,
    InfoQuery,
    CommandRequest,
    DeterministicAction,
    ContextReference,
    Continuation,
    Confirmation,
    Correction,
    MemoryUpdate,
    ToolInventory,
    Unknown
};

enum class ExecutionIntentKind {
    LocalResponse,
    DeterministicTask,
    BackgroundTask,
    CommandExtraction,
    AgentConversation,
    BackendReasoning,
    AskClarification,
    CapabilityError
};

enum class IntentAdvisorMode {
    Heuristic,
    ShadowLearned,
    Learned
};

struct TurnSignals {
    QString rawInput;
    QString normalizedInput;
    bool hasGreeting = false;
    bool hasSmallTalk = false;
    bool socialOnly = false;
    bool hasCommandCue = false;
    bool hasQuestionCue = false;
    bool hasNegation = false;
    bool hasUrgency = false;
    bool hasDeterministicCue = false;
    bool hasContextReference = false;
    bool hasContinuationCue = false;
    QStringList matchedCues;
};

struct TurnState {
    bool isNewTurn = true;
    bool isContinuation = false;
    bool isConfirmationReply = false;
    bool isCorrection = false;
    bool correctionDetected = false;
    float correctionConfidence = 0.0f;
    bool refersToPreviousTask = false;
    QStringList reasonCodes;
};

struct TurnStateInput {
    QString normalizedInput;
    TurnSignals turnSignals;
    bool hasPendingConfirmation = false;
    bool hasUsableActionThread = false;
    bool hasAnyActionThread = false;
};

struct UserGoal {
    UserGoalKind kind = UserGoalKind::Unknown;
    QString label;
    float confidence = 0.0f;
    QStringList reasonCodes;
};

struct TurnGoalSet {
    UserGoal primaryGoal;
    std::optional<UserGoal> secondaryGoal;
    bool mixedIntent = false;
    float ambiguity = 0.0f;
    float confidence = 0.0f;
};

struct ExecutionIntentCandidate {
    ExecutionIntentKind kind = ExecutionIntentKind::BackendReasoning;
    InputRouteDecision route;
    QList<AgentTask> tasks;
    float score = 0.0f;
    bool requiresBackend = false;
    bool canRunLocal = false;
    int backendPriority = 0;
    float confidencePenalty = 0.0f;
    QStringList reasonCodes;
};

struct IntentConfidence {
    float signalConfidence = 0.0f;
    float goalConfidence = 0.0f;
    float executionConfidence = 0.0f;
    float finalConfidence = 0.0f;
};

struct IntentAdvisorSuggestion {
    bool available = false;
    float ambiguityBoost = 0.0f;
    float continuationLikelihood = 0.0f;
    float backendNecessity = 0.0f;
    QStringList reasonCodes;
};

struct IntentAdvisorEvaluation {
    float baseAmbiguity = 0.0f;
    float adjustedAmbiguity = 0.0f;
    bool ambiguityPreferenceChanged = false;
    float baseBackendPreference = 0.0f;
    float adjustedBackendPreference = 0.0f;
    bool backendPreferenceChanged = false;
    QStringList reasonCodes;
};

struct RouteScores {
    float socialScore = 0.0f;
    float commandScore = 0.0f;
    float infoQueryScore = 0.0f;
    float deterministicScore = 0.0f;
    float continuationScore = 0.0f;
    float contextReferenceScore = 0.0f;
    float backendNeedScore = 0.0f;
};

struct RouteArbitrationResult {
    InputRouteDecision decision;
    RouteScores scores;
    QString finalRoute;
    float confidence = 0.0f;
    QStringList reasonCodes;
    QStringList overridesApplied;
};

struct LegacyRoutingSignals {
    LocalIntent localIntent = LocalIntent::Unknown;
    bool likelyCommand = false;
    bool hasDeterministicTask = false;
    bool explicitWebSearch = false;
    bool likelyKnowledgeLookup = false;
    bool freshnessSensitive = false;
    bool desktopContextRecall = false;
    bool explicitAgentWorldQuery = false;
    bool explicitComputerControl = false;
};

struct IntentInferenceSnapshot {
    IntentType mlIntent = IntentType::GENERAL_CHAT;
    float mlConfidence = 0.0f;
    IntentType detectorIntent = IntentType::GENERAL_CHAT;
    float detectorConfidence = 0.0f;
    IntentType effectiveIntent = IntentType::GENERAL_CHAT;
    float effectiveConfidence = 0.0f;
};

struct RoutingTrace {
    QString turnId;
    QString rawInput;
    QString normalizedInput;
    TurnSignals turnSignals;
    LegacyRoutingSignals legacySignals;
    TurnState turnState;
    TurnGoalSet goals;
    QList<ExecutionIntentCandidate> candidates;
    IntentInferenceSnapshot intentSnapshot;
    bool deterministicMatched = false;
    QString deterministicTaskType;
    bool deterministicTaskPayloadPresent = false;
    QString deterministicTaskPayloadLostReason;
    float ambiguityScore = 0.0f;
    IntentConfidence intentConfidence;
    IntentAdvisorMode advisorMode = IntentAdvisorMode::Heuristic;
    IntentAdvisorSuggestion advisorSuggestion;
    IntentAdvisorEvaluation advisorEvaluation;
    InputRouteDecision policyDecision;
    RouteArbitrationResult arbitratorResult;
    InputRouteDecision finalDecision;
    bool usedArbitratorAuthority = false;
    bool confirmationGateTriggered = false;
    QString confirmationOutcome;
    QString finalExecutedRoute;
    QString toolSelectionReason;
    QString toolSuppressionReason;
    int toolsAvailableCount = 0;
    QString clarificationTriggerReason;
    float ambiguityThresholdUsed = 0.0f;
    bool evidenceSufficient = false;
    bool toolDriftDetected = false;
    bool budgetEnforcementEnabled = true;
    QString budgetEnforcementDisabledReason;
    bool technicalGuardTriggered = false;
    bool toolLoopBreakerTriggered = false;
    QString toolLoopBreakerReason;
    int failedToolAttemptCount = 0;
    int sameFamilyAttemptCount = 0;
    int consecutiveFailureCount = 0;
    bool lastToolSuccess = false;
    QString gracefulFallbackReason;
    bool backendFailureDetected = false;
    QString fallbackReason;
    bool proactiveSuggestionSpoken = false;
    QString proactiveSpeechReason;
    QString proactiveSpeechSurface;
    bool proactiveSpeechCooldownActive = false;
    QString providerToolFilterReason;
    QString providerToolCompatibilityMode;
    QStringList toolsRemovedForProvider;
    float contextRelevanceScore = 0.0f;
    QString contextInjectionReason;
    QStringList overridesApplied;
    QStringList reasonCodes;
};
