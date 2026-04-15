#pragma once

#include <QHash>
#include <QObject>
#include <QSet>

#include "core/AssistantTypes.h"

class LoggingService;

class TaskDispatcher : public QObject
{
    Q_OBJECT

public:
    explicit TaskDispatcher(LoggingService *loggingService = nullptr, QObject *parent = nullptr);

    void enqueue(const AgentTask &task);
    bool isDuplicate(const AgentTask &task) const;
    int activeTaskId(const QString &taskKey) const;

public slots:
    void handleConnectorEvent(const ConnectorEvent &event);
    void handleTaskStarted(int taskId, const QString &type);
    void handleTaskFinished(int taskId, const QJsonObject &result);

signals:
    void connectorEventReady(const ConnectorEvent &event);
    void taskReady(const AgentTask &task);
    void taskCanceled(int taskId);
    void taskResultReady(const QJsonObject &result);
    void activeTaskChanged(const QString &taskKey, int taskId);

private:
    QString taskKeyFor(const AgentTask &task) const;
    void dispatchNext();
    void cleanupFinishedTasks();

    LoggingService *m_loggingService = nullptr;
    QList<AgentTask> m_pending;
    QHash<int, AgentTask> m_tasksById;
    QHash<QString, int> m_activeTaskIdByKey;
    QHash<QString, int> m_runningTaskIdByKey;
    QSet<int> m_runningTaskIds;
    QSet<int> m_canceledTaskIds;
    bool m_busy = false;
};
