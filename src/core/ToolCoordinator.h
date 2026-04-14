#pragma once

#include <functional>
#include <optional>

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

#include "core/AssistantTypes.h"

class AgentToolbox;
class ExecutionNarrator;
class LoggingService;
class TaskDispatcher;

struct ToolResultHandling {
    bool ignored = false;
    bool resultsChanged = false;
    bool toastChanged = false;
    bool surfaceChanged = false;
    bool appendTrace = false;
    QString traceKind;
    QString traceTitle;
    QString traceDetail;
    bool traceSuccess = false;
    std::optional<BackgroundTaskResult> completedResult;
    std::optional<ConnectorEvent> connectorEvent;
};

struct WebSearchFollowUp {
    bool deliverLocalResponse = false;
    QString localResponseText;
    QString localResponseStatus;
    QString synthesisInput;
    QString logPrompt;
};

class ToolCoordinator
{
public:
    explicit ToolCoordinator(LoggingService *loggingService = nullptr,
                             const ExecutionNarrator *executionNarrator = nullptr);

    QList<BackgroundTaskResult> backgroundTaskResults() const;
    QString latestTaskToast() const;
    QString latestTaskToastTone() const;
    int latestTaskToastTaskId() const;
    QString latestTaskToastType() const;
    int surfaceBackgroundTaskId() const;
    QString surfaceBackgroundPrimary() const;
    QString surfaceBackgroundSecondary() const;

    void dispatchBackgroundTasks(TaskDispatcher *dispatcher, const QList<AgentTask> &tasks);
    bool handleTaskCanceled(int taskId);
    bool handleTaskActivated(const QString &taskKey, int taskId);
    ToolResultHandling handleTaskResult(const QJsonObject &resultObject, bool backgroundPanelVisible);

    QList<AgentToolResult> executeAgentToolCalls(
        const QList<AgentToolCall> &toolCalls,
        AgentToolbox *agentToolbox,
        const std::function<void(const QString &, const QString &, const QString &, bool)> &traceCallback) const;

    QList<AgentToolCall> filterAllowedToolCalls(
        const QList<AgentToolCall> &toolCalls,
        const QStringList &allowedToolNames,
        const std::function<void(const QString &, const QString &, const QString &, bool)> &traceCallback) const;

    std::optional<WebSearchFollowUp> buildWebSearchFollowUp(const BackgroundTaskResult &result) const;

private:
    bool refreshBackgroundTaskSurface();
    QPair<QString, QString> backgroundTaskSurfaceCopy(const AgentTask &task) const;

    LoggingService *m_loggingService = nullptr;
    const ExecutionNarrator *m_executionNarrator = nullptr;
    QList<BackgroundTaskResult> m_backgroundTaskResults;
    QString m_latestTaskToast;
    QString m_latestTaskToastTone = QStringLiteral("status");
    int m_latestTaskToastTaskId = -1;
    QString m_latestTaskToastType = QStringLiteral("background");
    int m_nextTaskId = 1;
    QHash<QString, int> m_activeBackgroundTaskIds;
    QHash<int, AgentTask> m_knownBackgroundTasks;
    int m_surfaceBackgroundTaskId = -1;
    QString m_surfaceBackgroundPrimary;
    QString m_surfaceBackgroundSecondary;
};
