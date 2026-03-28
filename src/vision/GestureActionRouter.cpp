#include "vision/GestureActionRouter.h"

#include "logging/LoggingService.h"

GestureActionRouter::GestureActionRouter(LoggingService *loggingService, QObject *parent)
    : QObject(parent)
    , m_loggingService(loggingService)
{
}

void GestureActionRouter::configure(bool enabled)
{
    m_enabled = enabled;
}

void GestureActionRouter::routeGestureEvent(const GestureEvent &event)
{
    if (!m_enabled) {
        return;
    }

    const QString gestureName = event.sourceGesture.isEmpty() ? event.actionName : event.sourceGesture;
    logGestureEvent(QStringLiteral("gesture_detected"), gestureName, event.confidence);

    if (!isCancelGesture(event.actionName, event.sourceGesture)) {
        logGestureEvent(QStringLiteral("gesture_ignored"), gestureName, event.confidence, QStringLiteral("unsupported"));
        return;
    }

    if (event.type != GestureEventType::Start) {
        const QString reason = event.type == GestureEventType::Hold
            ? QStringLiteral("hold")
            : QStringLiteral("end");
        logGestureEvent(QStringLiteral("gesture_ignored"), gestureName, event.confidence, reason);
        return;
    }

    logGestureEvent(QStringLiteral("gesture_triggered"), gestureName, event.confidence);
    emit gestureTriggered(gestureName, event.timestampMs);
    emit stopSpeakingRequested();
    emit cancelCurrentRequestRequested();
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
