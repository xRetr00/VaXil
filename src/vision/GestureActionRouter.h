#pragma once

#include <QObject>
#include <QTimer>

class LoggingService;

class GestureActionRouter final : public QObject
{
    Q_OBJECT

public:
    explicit GestureActionRouter(LoggingService *loggingService, QObject *parent = nullptr);

public slots:
    void configure(bool enabled, int cooldownMs);
    void routeGesture(const QString &actionName,
                      const QString &sourceGesture,
                      double confidence,
                      qint64 timestampMs,
                      const QString &traceId);

signals:
    void gestureTriggered(const QString &gestureName, qint64 timestampMs);
    void stopSpeakingRequested();
    void cancelCurrentRequestRequested();

private slots:
    void handleGestureEnd();

private:
    enum class State {
        Idle,
        Active,
        Cooldown
    };

    void transitionTo(State state);
    void logGestureEvent(const QString &event,
                         const QString &gestureName,
                         double confidence,
                         const QString &reason = QString(),
                         int intervalMs = 700) const;
    bool isCancelGesture(const QString &actionName, const QString &sourceGesture) const;
    void advanceCooldown(qint64 nowMs);

    LoggingService *m_loggingService = nullptr;
    QTimer m_holdTimer;
    State m_state = State::Idle;
    bool m_enabled = false;
    int m_cooldownMs = 500;
    QString m_activeGesture;
    qint64 m_lastTriggeredAtMs = 0;
};
