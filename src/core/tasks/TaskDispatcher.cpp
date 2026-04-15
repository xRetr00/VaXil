#include "core/tasks/TaskDispatcher.h"

#include <QDateTime>
#include <QJsonDocument>

#include "logging/LoggingService.h"

namespace {
constexpr qint64 kTaskRetentionMs = 15000;
}

TaskDispatcher::TaskDispatcher(LoggingService *loggingService, QObject *parent)
    : QObject(parent)
    , m_loggingService(loggingService)
{
}

void TaskDispatcher::handleConnectorEvent(const ConnectorEvent &event)
{
    emit connectorEventReady(event);
}

void TaskDispatcher::enqueue(const AgentTask &incomingTask)
{
    cleanupFinishedTasks();

    AgentTask task = incomingTask;
    task.taskKey = taskKeyFor(task);
    if (task.createdAtMs <= 0) {
        task.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    }
    task.state = TaskState::Pending;

    if (isDuplicate(task)) {
        if (m_loggingService) {
            m_loggingService->infoFor(QStringLiteral("tools_mcp"),
                                      QStringLiteral("[TaskDispatcher] deduplicated %1 id=%2 key=%3")
                                          .arg(task.type)
                                          .arg(task.id)
                                          .arg(task.taskKey));
        }
        return;
    }

    m_tasksById.insert(task.id, task);
    m_activeTaskIdByKey.insert(task.taskKey, task.id);
    emit activeTaskChanged(task.taskKey, task.id);

    m_pending.push_back(task);
    std::sort(m_pending.begin(), m_pending.end(), [](const AgentTask &left, const AgentTask &right) {
        if (left.priority != right.priority) {
            return left.priority > right.priority;
        }
        return left.createdAtMs < right.createdAtMs;
    });

    if (m_loggingService) {
        m_loggingService->infoFor(QStringLiteral("tools_mcp"), QStringLiteral("[TaskDispatcher] queued %1 id=%2").arg(task.type).arg(task.id));
    }

    dispatchNext();
}

bool TaskDispatcher::isDuplicate(const AgentTask &task) const
{
    if (task.taskKey.isEmpty()) {
        return false;
    }

    if (m_runningTaskIdByKey.contains(task.taskKey)) {
        return true;
    }

    for (const AgentTask &pendingTask : m_pending) {
        if (pendingTask.taskKey == task.taskKey && pendingTask.state != TaskState::Canceled) {
            return true;
        }
    }

    return false;
}

int TaskDispatcher::activeTaskId(const QString &taskKey) const
{
    return m_activeTaskIdByKey.value(taskKey, -1);
}

void TaskDispatcher::handleTaskStarted(int taskId, const QString &type)
{
    if (!m_tasksById.contains(taskId)) {
        return;
    }

    AgentTask task = m_tasksById.value(taskId);
    task.state = TaskState::Running;
    m_tasksById.insert(taskId, task);
    m_busy = true;
    m_runningTaskIds.insert(taskId);
    if (!task.taskKey.isEmpty()) {
        m_runningTaskIdByKey.insert(task.taskKey, taskId);
    }

    if (m_loggingService) {
        m_loggingService->infoFor(QStringLiteral("tools_mcp"), QStringLiteral("[ToolWorker] started id=%1 type=%2").arg(taskId).arg(type));
    }
}

void TaskDispatcher::handleTaskFinished(int taskId, const QJsonObject &result)
{
    QJsonObject normalizedResult = result;

    if (m_tasksById.contains(taskId)) {
        AgentTask task = m_tasksById.value(taskId);
        const bool canceled = m_canceledTaskIds.contains(taskId);
        task.state = canceled ? TaskState::Canceled : static_cast<TaskState>(normalizedResult.value(QStringLiteral("state")).toInt(static_cast<int>(TaskState::Finished)));
        m_tasksById.insert(taskId, task);
        m_runningTaskIds.remove(taskId);
        m_runningTaskIdByKey.remove(task.taskKey);
        if (m_activeTaskIdByKey.value(task.taskKey) == taskId) {
            m_activeTaskIdByKey.remove(task.taskKey);
        }
        m_busy = false;

        if (canceled) {
            normalizedResult.insert(QStringLiteral("state"), static_cast<int>(TaskState::Canceled));
            normalizedResult.insert(QStringLiteral("success"), false);
            normalizedResult.insert(QStringLiteral("summary"), QStringLiteral("Canceled"));
            normalizedResult.insert(QStringLiteral("detail"), QStringLiteral("Task was replaced by a newer request."));
        }
    } else {
        m_busy = false;
    }

    if (m_loggingService) {
        m_loggingService->infoFor(QStringLiteral("tools_mcp"), QStringLiteral("[ToolWorker] finished id=%1").arg(taskId));
    }

    emit taskResultReady(normalizedResult);
    dispatchNext();
    cleanupFinishedTasks();
}

QString TaskDispatcher::taskKeyFor(const AgentTask &task) const
{
    return task.type + QStringLiteral("|")
        + QString::fromUtf8(QJsonDocument(task.args).toJson(QJsonDocument::Compact));
}

void TaskDispatcher::dispatchNext()
{
    if (m_busy) {
        return;
    }

    while (!m_pending.isEmpty()) {
        AgentTask task = m_pending.takeFirst();
        if (m_canceledTaskIds.contains(task.id)) {
            continue;
        }

        emit taskReady(task);
        return;
    }
}

void TaskDispatcher::cleanupFinishedTasks()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QList<int> expiredIds;
    for (auto it = m_tasksById.cbegin(); it != m_tasksById.cend(); ++it) {
        const AgentTask &task = it.value();
        if ((task.state == TaskState::Finished || task.state == TaskState::Canceled || task.state == TaskState::Expired)
            && nowMs - task.createdAtMs > kTaskRetentionMs) {
            expiredIds.push_back(it.key());
        }
    }

    for (int taskId : expiredIds) {
        const AgentTask task = m_tasksById.take(taskId);
        m_canceledTaskIds.remove(taskId);
        if (m_activeTaskIdByKey.value(task.taskKey) == taskId) {
            m_activeTaskIdByKey.remove(task.taskKey);
        }
    }
}
