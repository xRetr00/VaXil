#pragma once

#include <QHash>
#include <QObject>
#include <QSet>

#include "core/AssistantTypes.h"

class LoggingService;

class GestureStateMachine final : public QObject
{
    Q_OBJECT

public:
    explicit GestureStateMachine(LoggingService *loggingService, QObject *parent = nullptr);

public slots:
    void configure(bool enabled, double minConfidence, int stabilityMs, int lockMs);
    void ingestObservations(const QList<GestureObservation> &observations,
                            qint64 timestampMs,
                            const QString &traceId);

signals:
    void gestureEventReady(const GestureEvent &event);

private:
    struct GestureTrack {
        GestureLifecycleState state = GestureLifecycleState::Idle;
        QString sourceGesture;
        double confidence = 0.0;
        qint64 detectingSinceMs = 0;
        qint64 lastSeenMs = 0;
        qint64 lockUntilMs = 0;
        int stableFrameCount = 0;
        QString traceId;
    };

    void resetTrack(GestureTrack &track);
    void transitionTrack(const QString &actionName,
                         GestureTrack &track,
                         GestureLifecycleState nextState,
                         qint64 timestampMs,
                         const QString &traceId,
                         const QString &reason = QString());
    void emitGestureEvent(GestureEventType type,
                          const QString &actionName,
                          const GestureTrack &track,
                          qint64 timestampMs,
                          const QString &traceId);
    void updateObservedTrack(const QString &actionName,
                             const GestureObservation &observation,
                             qint64 timestampMs,
                             const QString &traceId);
    void advanceUnobservedTracks(const QSet<QString> &observedActions, qint64 timestampMs);
    void logStateChange(const QString &actionName,
                        const GestureTrack &track,
                        const QString &reason,
                        int intervalMs = 250) const;
    static QString stateToString(GestureLifecycleState state);

    LoggingService *m_loggingService = nullptr;
    bool m_enabled = false;
    double m_minConfidence = 0.70;
    int m_stabilityMs = 180;
    int m_lockMs = 600;
    int m_minStableFrames = 3;
    QHash<QString, GestureTrack> m_tracks;
};
