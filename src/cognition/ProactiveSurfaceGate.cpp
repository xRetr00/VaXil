#include "cognition/ProactiveSurfaceGate.h"

namespace {
bool isWorkspaceInspectionTask(const QString &taskType)
{
    return taskType == QStringLiteral("web_search")
        || taskType == QStringLiteral("file_read")
        || taskType == QStringLiteral("file_search")
        || taskType == QStringLiteral("dir_list")
        || taskType == QStringLiteral("browser_fetch_text");
}

QString desktopThreadId(const QVariantMap &desktopContext)
{
    return desktopContext.value(QStringLiteral("threadId")).toString().trimmed();
}
}

BehaviorDecision ProactiveSurfaceGate::evaluateTaskToast(const Input &input)
{
    BehaviorDecision decision;
    decision.action = QStringLiteral("show_toast");
    decision.allowed = true;
    decision.reasonCode = QStringLiteral("surface.default_allow");
    decision.score = 0.72;
    decision.details.insert(QStringLiteral("taskType"), input.result.type);
    decision.details.insert(QStringLiteral("success"), input.result.success);
    decision.details.insert(QStringLiteral("focusModeEnabled"), input.focusMode.enabled);
    decision.details.insert(QStringLiteral("desktopTaskId"), input.desktopContext.value(QStringLiteral("taskId")).toString());
    decision.details.insert(QStringLiteral("desktopThreadId"), input.desktopContext.value(QStringLiteral("threadId")).toString());

    if (!input.result.success) {
        decision.reasonCode = QStringLiteral("surface.error_always_visible");
        decision.score = 0.95;
        return decision;
    }

    if (input.focusMode.enabled) {
        decision.allowed = false;
        decision.action = QStringLiteral("suppress_toast");
        decision.reasonCode = QStringLiteral("surface.focus_mode_suppressed");
        decision.score = 0.98;
        return decision;
    }

    if (shouldSuppressForFocusedDesktopWork(input)) {
        decision.allowed = false;
        decision.action = QStringLiteral("suppress_toast");
        decision.reasonCode = QStringLiteral("surface.focused_context_suppressed");
        decision.score = 0.88;
        return decision;
    }

    if (input.cooldownState.isActive(input.nowMs) && !hasMeaningfulThreadShift(input)) {
        decision.allowed = false;
        decision.action = QStringLiteral("suppress_toast");
        decision.reasonCode = QStringLiteral("surface.cooldown_suppressed");
        decision.score = 0.9;
        return decision;
    }

    if (hasFreshDesktopContext(input)) {
        if (hasMeaningfulThreadShift(input)) {
            decision.reasonCode = QStringLiteral("surface.cooldown_thread_shift");
            decision.score = 0.86;
        } else {
            decision.reasonCode = QStringLiteral("surface.context_checked_allow");
            decision.score = 0.8;
        }
    }

    return decision;
}

BehaviorDecision ProactiveSurfaceGate::evaluateCompletionFollowUp(const Input &input,
                                                                 bool hasArtifacts,
                                                                 bool userRequested)
{
    BehaviorDecision decision;
    decision.action = hasArtifacts
        ? QStringLiteral("start_completion_request")
        : QStringLiteral("deliver_follow_up");
    decision.allowed = true;
    decision.reasonCode = QStringLiteral("surface.follow_up_allow");
    decision.score = 0.74;
    decision.details.insert(QStringLiteral("taskType"), input.result.type);
    decision.details.insert(QStringLiteral("success"), input.result.success);
    decision.details.insert(QStringLiteral("hasArtifacts"), hasArtifacts);
    decision.details.insert(QStringLiteral("userRequested"), userRequested);
    decision.details.insert(QStringLiteral("focusModeEnabled"), input.focusMode.enabled);
    decision.details.insert(QStringLiteral("desktopTaskId"), input.desktopContext.value(QStringLiteral("taskId")).toString());
    decision.details.insert(QStringLiteral("desktopThreadId"), input.desktopContext.value(QStringLiteral("threadId")).toString());

    if (!input.result.success) {
        decision.reasonCode = QStringLiteral("surface.follow_up_error_visible");
        decision.score = 0.93;
        return decision;
    }

    if (userRequested) {
        decision.reasonCode = QStringLiteral("surface.user_requested_completion_allow");
        decision.score = 0.9;
        return decision;
    }

    if (input.focusMode.enabled) {
        decision.allowed = false;
        decision.action = QStringLiteral("suppress_follow_up");
        decision.reasonCode = QStringLiteral("surface.follow_up_focus_mode_suppressed");
        decision.score = 0.98;
        return decision;
    }

    if (shouldSuppressForFocusedDesktopWork(input)) {
        decision.reasonCode = QStringLiteral("surface.follow_up_focused_context_soft_allow");
        decision.score = 0.76;
    }

    if (input.cooldownState.isActive(input.nowMs) && !hasMeaningfulThreadShift(input)) {
        decision.reasonCode = QStringLiteral("surface.follow_up_cooldown_soft_allow");
        decision.score = 0.78;
    }

    if (hasFreshDesktopContext(input)) {
        if (hasMeaningfulThreadShift(input)) {
            decision.reasonCode = QStringLiteral("surface.follow_up_thread_shift");
            decision.score = 0.87;
        } else {
            decision.reasonCode = QStringLiteral("surface.follow_up_context_checked_allow");
            decision.score = 0.82;
        }
    }

    return decision;
}

bool ProactiveSurfaceGate::hasFreshDesktopContext(const Input &input)
{
    return input.desktopContextAtMs > 0 && (input.nowMs - input.desktopContextAtMs) <= 90000;
}

bool ProactiveSurfaceGate::hasMeaningfulThreadShift(const Input &input)
{
    const QString threadId = desktopThreadId(input.desktopContext);
    return !threadId.isEmpty() && threadId != input.cooldownState.threadId;
}

bool ProactiveSurfaceGate::shouldSuppressForFocusedDesktopWork(const Input &input)
{
    if (!hasFreshDesktopContext(input) || !isWorkspaceInspectionTask(input.result.type)) {
        return false;
    }

    const QString desktopTaskId = input.desktopContext.value(QStringLiteral("taskId")).toString().trimmed();
    return desktopTaskId == QStringLiteral("editor_document")
        || desktopTaskId == QStringLiteral("browser_tab");
}
