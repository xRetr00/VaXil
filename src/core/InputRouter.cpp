#include "core/InputRouter.h"

InputRouteDecision InputRouter::decide(const InputRouterContext &context) const
{
    InputRouteDecision decision;
    if (context.wakeOnly) {
        decision.kind = InputRouteKind::LocalResponse;
        decision.message = QStringLiteral("Listening");
        decision.status = QStringLiteral("Listening");
        decision.speak = false;
        return decision;
    }

    if (context.shouldEndConversation) {
        decision.kind = InputRouteKind::LocalResponse;
        decision.message = QStringLiteral("Standing by.");
        decision.status = QStringLiteral("Conversation ended");
        return decision;
    }

    if (context.isTimeQuery || context.isDateQuery) {
        decision.kind = InputRouteKind::LocalResponse;
        decision.status = context.isTimeQuery ? QStringLiteral("Local time response") : QStringLiteral("Local date response");
        return decision;
    }

    if (context.hasDeterministicTask) {
        decision.kind = InputRouteKind::DeterministicTasks;
        decision.tasks = {context.deterministicTask};
        decision.message = context.deterministicSpoken;
        decision.status = QStringLiteral("Background task queued");
        return decision;
    }

    if (context.agentEnabled && context.explicitComputerControl) {
        decision.kind = InputRouteKind::AgentConversation;
        decision.intent = IntentType::GENERAL_CHAT;
        return decision;
    }

    if (context.backgroundIntentReady && !context.backgroundTasks.isEmpty()) {
        decision.kind = InputRouteKind::BackgroundTasks;
        decision.tasks = context.backgroundTasks;
        decision.message = context.backgroundSpokenMessage;
        decision.status = QStringLiteral("Background task queued");
        return decision;
    }

    if (context.localIntent == LocalIntent::Greeting || context.localIntent == LocalIntent::SmallTalk) {
        decision.kind = InputRouteKind::LocalResponse;
        decision.status = QStringLiteral("Local response");
        return decision;
    }

    if (context.visionRelevant && !context.explicitComputerControl && !context.directVisionResponse.trimmed().isEmpty()) {
        decision.kind = InputRouteKind::LocalResponse;
        decision.message = context.directVisionResponse;
        decision.status = QStringLiteral("Vision response");
        return decision;
    }

    if (!context.aiAvailable) {
        decision.kind = InputRouteKind::AgentCapabilityError;
        decision.status = QStringLiteral("AI unavailable");
        return decision;
    }

    if (context.explicitToolInventory) {
        decision.kind = InputRouteKind::LocalResponse;
        decision.message = context.toolInventoryText;
        decision.status = QStringLiteral("Tool inventory");
        return decision;
    }

    if (!context.visionRelevant && context.explicitWebSearch) {
        decision.kind = InputRouteKind::BackgroundTasks;
        AgentTask task;
        task.type = QStringLiteral("web_search");
        task.args = QJsonObject{{QStringLiteral("query"), context.explicitWebQuery}};
        task.priority = 85;
        decision.tasks = {task};
        decision.message = QStringLiteral("All right, I'm searching the web now. The result will show up in the panel.");
        decision.status = QStringLiteral("Background task queued");
        return decision;
    }

    if (!context.visionRelevant && context.agentEnabled && context.likelyKnowledgeLookup) {
        decision.kind = InputRouteKind::BackgroundTasks;
        AgentTask task;
        task.type = QStringLiteral("web_search");
        task.args = QJsonObject{{QStringLiteral("query"), context.explicitWebQuery}};
        task.priority = 83;
        decision.tasks = {task};
        decision.message = QStringLiteral("I'll verify that on the web and summarize what I find.");
        decision.status = QStringLiteral("Background task queued");
        return decision;
    }

    if (!context.visionRelevant && context.agentEnabled && context.freshnessSensitive) {
        decision.kind = InputRouteKind::BackgroundTasks;
        return decision;
    }

    if (context.agentEnabled && context.explicitAgentWorldQuery) {
        decision.kind = InputRouteKind::AgentConversation;
        decision.intent = context.effectiveIntent;
        return decision;
    }

    if (context.visionRelevant && !context.explicitComputerControl) {
        decision.kind = InputRouteKind::Conversation;
        decision.useVisionContext = true;
        return decision;
    }

    if (context.localIntent == LocalIntent::Command || context.likelyCommand) {
        decision.kind = InputRouteKind::CommandExtraction;
        return decision;
    }

    if (context.effectiveIntent != IntentType::GENERAL_CHAT
        && context.effectiveIntentConfidence > 0.4f
        && context.effectiveIntentConfidence <= 0.8f
        && context.agentEnabled) {
        decision.kind = InputRouteKind::AgentConversation;
        decision.intent = context.effectiveIntent;
        return decision;
    }

    decision.kind = InputRouteKind::Conversation;
    decision.useVisionContext = context.visionRelevant;
    return decision;
}
