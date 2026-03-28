#include "vision/GestureStateMachine.h"

#include <algorithm>

#include <QDateTime>
#include <QSet>

#include "logging/LoggingService.h"

namespace {
constexpr int kGestureEndGraceMs = 120;
}

GestureStateMachine::GestureStateMachine(LoggingService *loggingService, QObject *parent)
    : QObject(parent)
    , m_loggingService(loggingService)
{
}

void GestureStateMachine::configure(bool enabled, double minConfidence, int stabilityMs, int lockMs)
{
    m_enabled = enabled;
    m_minConfidence = std::clamp(minConfidence, 0.05, 1.0);
    m_stabilityMs = std::clamp(stabilityMs, 100, 1000);
    m_lockMs = std::clamp(lockMs, 200, 5000);
    if (!m_enabled) {
        m_tracks.clear();
    }
}

void GestureStateMachine::ingestObservations(const QList<GestureObservation> &observations,
                                             qint64 timestampMs,
                                             const QString &traceId)
{
    if (!m_enabled) {
        return;
    }

    const qint64 nowMs = timestampMs > 0 ? timestampMs : QDateTime::currentMSecsSinceEpoch();
    QHash<QString, GestureObservation> bestByAction;
    for (const GestureObservation &observation : observations) {
        if (observation.actionName.trimmed().isEmpty()
            || observation.sourceGesture.trimmed().isEmpty()
            || observation.confidence < m_minConfidence) {
            continue;
        }

        const auto existing = bestByAction.constFind(observation.actionName);
        if (existing == bestByAction.constEnd() || observation.confidence > existing->confidence) {
            bestByAction.insert(observation.actionName, observation);
        }
    }

    QSet<QString> observedActions;
    for (auto it = bestByAction.cbegin(); it != bestByAction.cend(); ++it) {
        observedActions.insert(it.key());
        updateObservedTrack(it.key(), it.value(), nowMs, traceId);
    }

    advanceUnobservedTracks(observedActions, nowMs);
}

void GestureStateMachine::resetTrack(GestureTrack &track)
{
    track.state = GestureLifecycleState::Idle;
    track.sourceGesture.clear();
    track.confidence = 0.0;
    track.detectingSinceMs = 0;
    track.lastSeenMs = 0;
    track.lockUntilMs = 0;
    track.stableFrameCount = 0;
    track.traceId.clear();
}

void GestureStateMachine::transitionTrack(const QString &actionName,
                                          GestureTrack &track,
                                          GestureLifecycleState nextState,
                                          qint64 timestampMs,
                                          const QString &traceId,
                                          const QString &reason)
{
    if (track.state == nextState) {
        return;
    }

    track.state = nextState;
    track.traceId = traceId;
    if (nextState == GestureLifecycleState::Detecting) {
        track.detectingSinceMs = timestampMs;
        track.lastSeenMs = timestampMs;
        track.stableFrameCount = std::max(1, track.stableFrameCount);
    } else if (nextState == GestureLifecycleState::Active) {
        track.lastSeenMs = timestampMs;
    } else if (nextState == GestureLifecycleState::Cooldown) {
        track.lockUntilMs = timestampMs + m_lockMs;
        track.detectingSinceMs = 0;
        track.stableFrameCount = 0;
    } else if (nextState == GestureLifecycleState::Idle) {
        resetTrack(track);
    }

    logStateChange(actionName, track, reason);
}

void GestureStateMachine::emitGestureEvent(GestureEventType type,
                                           const QString &actionName,
                                           const GestureTrack &track,
                                           qint64 timestampMs,
                                           const QString &traceId)
{
    GestureEvent event;
    event.type = type;
    event.lifecycleState = track.state;
    event.actionName = actionName;
    event.sourceGesture = track.sourceGesture;
    event.confidence = track.confidence;
    event.timestampMs = timestampMs;
    event.stableForMs = track.detectingSinceMs > 0 ? static_cast<int>(std::max<qint64>(0, timestampMs - track.detectingSinceMs)) : 0;
    event.stableFrameCount = track.stableFrameCount;
    event.traceId = traceId;
    emit gestureEventReady(event);
}

void GestureStateMachine::updateObservedTrack(const QString &actionName,
                                              const GestureObservation &observation,
                                              qint64 timestampMs,
                                              const QString &traceId)
{
    GestureTrack &track = m_tracks[actionName];

    if (track.state == GestureLifecycleState::Cooldown && timestampMs < track.lockUntilMs) {
        track.lastSeenMs = timestampMs;
        return;
    }

    if (track.state == GestureLifecycleState::Cooldown && timestampMs >= track.lockUntilMs) {
        transitionTrack(actionName, track, GestureLifecycleState::Idle, timestampMs, traceId, QStringLiteral("lock_expired"));
    }

    if (track.state == GestureLifecycleState::Idle) {
        track.sourceGesture = observation.sourceGesture;
        track.confidence = observation.confidence;
        track.stableFrameCount = 1;
        transitionTrack(actionName, track, GestureLifecycleState::Detecting, timestampMs, traceId, QStringLiteral("candidate_seen"));
        return;
    }

    if (track.sourceGesture != observation.sourceGesture) {
        track.state = GestureLifecycleState::Idle;
        track.sourceGesture = observation.sourceGesture;
        track.confidence = observation.confidence;
        track.detectingSinceMs = 0;
        track.lastSeenMs = 0;
        track.stableFrameCount = 1;
        track.traceId.clear();
        transitionTrack(actionName, track, GestureLifecycleState::Detecting, timestampMs, traceId, QStringLiteral("gesture_changed"));
        return;
    }

    track.confidence = std::max(track.confidence, observation.confidence);
    track.lastSeenMs = timestampMs;

    if (track.state == GestureLifecycleState::Detecting) {
        ++track.stableFrameCount;
        const int stableForMs = static_cast<int>(std::max<qint64>(0, timestampMs - track.detectingSinceMs));
        if (stableForMs >= m_stabilityMs && track.stableFrameCount >= m_minStableFrames) {
            transitionTrack(actionName, track, GestureLifecycleState::Active, timestampMs, traceId, QStringLiteral("stable"));
            emitGestureEvent(GestureEventType::Start, actionName, track, timestampMs, traceId);
        }
        return;
    }

    if (track.state == GestureLifecycleState::Active) {
        emitGestureEvent(GestureEventType::Hold, actionName, track, timestampMs, traceId);
    }
}

void GestureStateMachine::advanceUnobservedTracks(const QSet<QString> &observedActions, qint64 timestampMs)
{
    for (auto it = m_tracks.begin(); it != m_tracks.end(); ++it) {
        if (observedActions.contains(it.key())) {
            continue;
        }

        GestureTrack &track = it.value();
        if (track.state == GestureLifecycleState::Detecting) {
            transitionTrack(it.key(), track, GestureLifecycleState::Idle, timestampMs, QString(), QStringLiteral("candidate_lost"));
            continue;
        }

        if (track.state == GestureLifecycleState::Active
            && track.lastSeenMs > 0
            && (timestampMs - track.lastSeenMs) >= kGestureEndGraceMs) {
            emitGestureEvent(GestureEventType::End, it.key(), track, timestampMs, track.traceId);
            transitionTrack(it.key(), track, GestureLifecycleState::Cooldown, timestampMs, track.traceId, QStringLiteral("gesture_ended"));
            continue;
        }

        if (track.state == GestureLifecycleState::Cooldown && timestampMs >= track.lockUntilMs) {
            transitionTrack(it.key(), track, GestureLifecycleState::Idle, timestampMs, QString(), QStringLiteral("cooldown_complete"));
        }
    }
}

void GestureStateMachine::logStateChange(const QString &actionName,
                                         const GestureTrack &track,
                                         const QString &reason,
                                         int intervalMs) const
{
    if (!m_loggingService) {
        return;
    }

    const QString message = QStringLiteral(
        "gesture_state action=\"%1\" gesture=\"%2\" state=\"%3\" confidence=%4 stable_ms=%5 stable_frames=%6 reason=\"%7\"")
        .arg(actionName,
             track.sourceGesture,
             stateToString(track.state))
        .arg(track.confidence, 0, 'f', 2)
        .arg(track.detectingSinceMs > 0 && track.lastSeenMs >= track.detectingSinceMs ? track.lastSeenMs - track.detectingSinceMs : 0)
        .arg(track.stableFrameCount)
        .arg(reason);
    const QString rateKey = QStringLiteral("gesture_state_%1_%2_%3")
        .arg(actionName, track.sourceGesture, stateToString(track.state));
    m_loggingService->logVisionStatus(message, rateKey, intervalMs);
}

QString GestureStateMachine::stateToString(GestureLifecycleState state)
{
    switch (state) {
    case GestureLifecycleState::Idle:
        return QStringLiteral("idle");
    case GestureLifecycleState::Detecting:
        return QStringLiteral("detecting");
    case GestureLifecycleState::Active:
        return QStringLiteral("active");
    case GestureLifecycleState::Cooldown:
        return QStringLiteral("cooldown");
    }

    return QStringLiteral("idle");
}
