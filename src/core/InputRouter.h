#pragma once

#include "core/AssistantTypes.h"

struct InputRouterContext {
    bool wakeOnly = false;
    bool shouldEndConversation = false;
    bool isTimeQuery = false;
    bool isDateQuery = false;
    bool hasDeterministicTask = false;
    AgentTask deterministicTask;
    QString deterministicSpoken;
    bool backgroundIntentReady = false;
    QList<AgentTask> backgroundTasks;
    QString backgroundSpokenMessage;
    LocalIntent localIntent = LocalIntent::Unknown;
    bool visionRelevant = false;
    QString directVisionResponse;
    bool aiAvailable = true;
    bool explicitToolInventory = false;
    QString toolInventoryText;
    bool explicitWebSearch = false;
    QString explicitWebQuery;
    bool likelyKnowledgeLookup = false;
    bool freshnessSensitive = false;
    bool agentEnabled = false;
    bool explicitAgentWorldQuery = false;
    bool likelyCommand = false;
    IntentType effectiveIntent = IntentType::GENERAL_CHAT;
    float effectiveIntentConfidence = 0.0f;
    bool explicitComputerControl = false;
};

class InputRouter
{
public:
    InputRouteDecision decide(const InputRouterContext &context) const;
};
