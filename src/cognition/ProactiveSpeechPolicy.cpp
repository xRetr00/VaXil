#include "cognition/ProactiveSpeechPolicy.h"

ProactiveSpeechDecision ProactiveSpeechPolicy::evaluate(const QString &message,
                                                        const QString &surfaceKind,
                                                        const FocusModeState &focusMode,
                                                        const CooldownState &cooldownState,
                                                        qint64 nowMs)
{
    ProactiveSpeechDecision decision;
    decision.cooldownActive = cooldownState.isActive(nowMs);
    if (message.trimmed().isEmpty()) {
        decision.reasonCode = QStringLiteral("proactive_speech.empty_message");
        return decision;
    }
    if (focusMode.enabled && !focusMode.allowCriticalAlerts) {
        decision.reasonCode = QStringLiteral("proactive_speech.suppressed_focus_mode");
        return decision;
    }
    if (decision.cooldownActive) {
        // Soft cooldown: keep proactive speech available during tuning and rely on focus mode
        // plus downstream dedupe for spam control.
        decision.shouldSpeak = true;
        decision.reasonCode = QStringLiteral("proactive_speech.cooldown_soft_allow");
        return decision;
    }
    decision.shouldSpeak = true;
    decision.reasonCode = surfaceKind.trimmed().isEmpty()
        ? QStringLiteral("proactive_speech.allowed")
        : QStringLiteral("proactive_speech.allowed.%1").arg(surfaceKind.trimmed());
    return decision;
}
