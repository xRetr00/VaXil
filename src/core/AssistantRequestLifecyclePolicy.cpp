#include "core/AssistantRequestLifecyclePolicy.h"

#include <algorithm>

int AssistantRequestLifecyclePolicy::timeoutMs(RequestKind kind, int configuredTimeoutMs)
{
    int minimumMs = 45000;
    switch (kind) {
    case RequestKind::CommandExtraction:
        minimumMs = 20000;
        break;
    case RequestKind::AgentConversation:
        minimumMs = 75000;
        break;
    case RequestKind::Conversation:
    default:
        minimumMs = 45000;
        break;
    }
    return std::max(minimumMs, configuredTimeoutMs > 0 ? configuredTimeoutMs : minimumMs);
}

int AssistantRequestLifecyclePolicy::heartbeatDelayMs(int heartbeatIndex)
{
    if (heartbeatIndex <= 0) {
        return 6000;
    }
    if (heartbeatIndex == 1) {
        return 14000;
    }
    return 0;
}

QString AssistantRequestLifecyclePolicy::heartbeatMessage(RequestKind kind, int heartbeatIndex)
{
    Q_UNUSED(heartbeatIndex);
    if (kind == RequestKind::AgentConversation) {
        return QStringLiteral("I'm still working on it and checking the available evidence.");
    }
    if (kind == RequestKind::CommandExtraction) {
        return QStringLiteral("I'm still preparing that action.");
    }
    return QStringLiteral("I'm still working on it.");
}

bool AssistantRequestLifecyclePolicy::isProviderRateLimitError(const QString &errorText)
{
    const QString lowered = errorText.toLower();
    return lowered.contains(QStringLiteral("rate limit"))
        || lowered.contains(QStringLiteral("rate_limit"))
        || lowered.contains(QStringLiteral("too many requests"))
        || lowered.contains(QStringLiteral("http 429"))
        || lowered.contains(QStringLiteral(" 429"));
}
