#include "vision/GestureActionRouter.h"

#include <algorithm>

#include <QDateTime>

#include "logging/LoggingService.h"

namespace {
constexpr int kGestureHoldResetMs = 220;
}

GestureActionRouter::GestureActionRouter(LoggingService *loggingService, QObject *parent)
    : QObject(parent)
    , m_loggingService(loggingService)
{
    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(kGestureHoldResetMs);
    connect(&m_holdTimer, &QTimer::timeout, this, &GestureActionRouter::handleGestureEnd);
}

void GestureActionRouter::configure(bool enabled, int cooldownMs)
{
    m_enabled = enabled;
    m_cooldownMs = std::max(100, cooldownMs);
    if (!m_enabled) {
        transitionTo(State::Idle);
        m_activeGesture.clear();
        m_holdTimer.stop();
    }
}

void GestureActionRouter::routeGesture(const QString &actionName,
                                       const QString &sourceGesture,
                                       double confidence,
                                       qint64 timestampMs,
                                       const QString &traceId)
{
    Q_UNUSED(traceId);

    if (!m_enabled) {
        return;
    }

    if (!isCancelGesture(actionName, sourceGesture)) {
        return;
    }

    const qint64 nowMs = timestampMs > 0 ? timestampMs : QDateTime::currentMSecsSinceEpoch();
    advanceCooldown(nowMs);
    logGestureEvent(QStringLiteral("gesture_detected"), sourceGesture, confidence);

    if (m_state == State::Active) {
        m_holdTimer.start();
        logGestureEvent(QStringLiteral("gesture_ignored"), sourceGesture, confidence, QStringLiteral("duplicate"));
        return;
    }

    if (m_state == State::Cooldown) {
        logGestureEvent(QStringLiteral("gesture_ignored"), sourceGesture, confidence, QStringLiteral("cooldown"));
        return;
    }

    m_activeGesture = sourceGesture;
    m_lastTriggeredAtMs = nowMs;
    transitionTo(State::Active);
    m_holdTimer.start();

    logGestureEvent(QStringLiteral("gesture_triggered"), sourceGesture, confidence);
    emit gestureTriggered(sourceGesture, nowMs);
    emit stopSpeakingRequested();
    emit cancelCurrentRequestRequested();
}

void GestureActionRouter::handleGestureEnd()
{
    if (m_state != State::Active) {
        return;
    }

    m_activeGesture.clear();
    transitionTo(State::Cooldown);
}

void GestureActionRouter::transitionTo(State state)
{
    m_state = state;
}

void GestureActionRouter::logGestureEvent(const QString &event,
                                          const QString &gestureName,
                                          double confidence,
                                          const QString &reason,
                                          int intervalMs) const
{
    if (!m_loggingService) {
        return;
    }

    const QString rateKey = QStringLiteral("gesture_%1_%2_%3")
        .arg(event, gestureName, reason.left(24));
    QString message = QStringLiteral("%1 gesture_name=\"%2\" confidence=%3")
        .arg(event, gestureName)
        .arg(confidence, 0, 'f', 2);
    if (!reason.trimmed().isEmpty()) {
        message += QStringLiteral(" reason=\"%1\"").arg(reason.trimmed());
    }
    m_loggingService->logVisionStatus(message, rateKey, intervalMs);
}

bool GestureActionRouter::isCancelGesture(const QString &actionName, const QString &sourceGesture) const
{
    const QString normalizedAction = actionName.trimmed().toLower();
    const QString normalizedGesture = sourceGesture.trimmed().toLower();
    return normalizedAction == QStringLiteral("cancel")
        || normalizedGesture == QStringLiteral("open_hand")
        || normalizedGesture == QStringLiteral("open_palm")
        || normalizedGesture == QStringLiteral("palm")
        || normalizedGesture == QStringLiteral("stop");
}

void GestureActionRouter::advanceCooldown(qint64 nowMs)
{
    if (m_state == State::Cooldown && (nowMs - m_lastTriggeredAtMs) >= m_cooldownMs) {
        transitionTo(State::Idle);
    }
}
