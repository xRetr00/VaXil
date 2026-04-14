#include "cognition/ProactiveCooldownTracker.h"

#include <algorithm>

#include "cognition/CooldownEngine.h"

namespace {
double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

CompanionContextSnapshot buildContext(const ProactiveCooldownTracker::Input &input)
{
    CompanionContextSnapshot context;
    context.threadId = ContextThreadId{
        input.desktopContext.value(QStringLiteral("threadId")).toString().trimmed()
    };
    context.appId = input.desktopContext.value(QStringLiteral("appId")).toString().trimmed();
    context.taskId = input.desktopContext.value(QStringLiteral("taskId")).toString().trimmed();
    context.topic = input.desktopContext.value(QStringLiteral("topic")).toString().trimmed();
    context.recentIntent = input.desktopContext.value(QStringLiteral("recentIntent")).toString().trimmed();
    context.confidence = input.desktopContext.value(QStringLiteral("confidence")).toDouble();
    context.metadata = input.desktopContext;

    if (context.taskId.isEmpty()) {
        context.taskId = input.taskType.trimmed();
    }
    if (context.topic.isEmpty()) {
        context.topic = !input.taskType.trimmed().isEmpty()
            ? input.taskType.trimmed()
            : input.surfaceKind.trimmed();
    }
    if (context.threadId.value.isEmpty()) {
        context.threadId = ContextThreadId::fromParts({
            QStringLiteral("surface"),
            input.surfaceKind.trimmed().isEmpty() ? QStringLiteral("presentation") : input.surfaceKind.trimmed(),
            input.taskType.trimmed().isEmpty() ? QStringLiteral("general") : input.taskType.trimmed()
        });
    }
    return context;
}

double surfaceConfidence(const ProactiveCooldownTracker::Input &input,
                         const CompanionContextSnapshot &context)
{
    double confidence = context.confidence > 0.0 ? context.confidence : 0.72;
    const QString priority = input.priority.trimmed().toLower();
    if (priority == QStringLiteral("high") || priority == QStringLiteral("critical")) {
        confidence += 0.08;
    }
    return clamp01(confidence);
}

double surfaceNovelty(const ProactiveCooldownTracker::Input &input,
                      const CompanionContextSnapshot &context)
{
    double novelty = 0.52;
    if (!context.threadId.value.isEmpty() && context.threadId.value != input.state.threadId) {
        novelty += 0.24;
    }
    if (input.state.isActive(input.nowMs) && context.threadId.value == input.state.threadId) {
        novelty -= 0.14;
    }
    return clamp01(novelty);
}
}

ProactiveCooldownCommit ProactiveCooldownTracker::commitPresentedSurface(const Input &input)
{
    ProactiveCooldownCommit commit;
    commit.context = buildContext(input);
    commit.confidence = surfaceConfidence(input, commit.context);
    commit.novelty = surfaceNovelty(input, commit.context);

    CooldownEngine engine;
    BehaviorDecision decision;
    decision.allowed = true;
    decision.action = QStringLiteral("surface_presented");
    decision.reasonCode = QStringLiteral("surface.presented");
    decision.score = std::max(commit.confidence, commit.novelty);

    commit.nextState = engine.advanceState({
        .context = commit.context,
        .state = input.state,
        .priority = input.priority,
        .confidence = commit.confidence,
        .novelty = commit.novelty,
        .nowMs = input.nowMs
    }, decision);
    return commit;
}
