#include "core/ToolCoordinator.h"

#include <algorithm>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

#include "agent/AgentToolbox.h"
#include "cognition/ConnectorEventBuilder.h"
#include "core/ExecutionNarrator.h"
#include "core/tasks/TaskDispatcher.h"
#include "logging/LoggingService.h"

namespace {
QString clippedWebSearchPayload(const BackgroundTaskResult &result, int maxChars = 12000)
{
    QString content = result.payload.value(QStringLiteral("content")).toString().trimmed();
    if (content.isEmpty()) {
        content = result.payload.value(QStringLiteral("text")).toString().trimmed();
    }
    if (content.isEmpty()) {
        content = result.payload.value(QStringLiteral("summary")).toString().trimmed();
    }
    if (content.isEmpty()) {
        const QJsonArray sources = result.payload.value(QStringLiteral("sources")).toArray();
        QStringList lines;
        for (const QJsonValue &value : sources) {
            const QJsonObject source = value.toObject();
            const QString url = source.value(QStringLiteral("url")).toString().trimmed();
            if (url.isEmpty()) {
                continue;
            }
            lines.push_back(QStringLiteral("%1 | %2 | %3")
                                .arg(source.value(QStringLiteral("title")).toString().trimmed().isEmpty()
                                         ? QStringLiteral("untitled")
                                         : source.value(QStringLiteral("title")).toString().trimmed())
                                .arg(url)
                                .arg(source.value(QStringLiteral("snippet")).toString().trimmed().left(240)));
            if (lines.size() >= 8) {
                break;
            }
        }
        content = lines.join(QStringLiteral("\n")).trimmed();
    }
    if (content.isEmpty() && !result.payload.isEmpty()) {
        content = QString::fromUtf8(QJsonDocument(result.payload).toJson(QJsonDocument::Compact));
    }
    if (content.size() > maxChars) {
        content = content.left(maxChars);
    }
    return content;
}
}

ToolCoordinator::ToolCoordinator(LoggingService *loggingService,
                                 const ExecutionNarrator *executionNarrator)
    : m_loggingService(loggingService)
    , m_executionNarrator(executionNarrator)
{
}

QList<BackgroundTaskResult> ToolCoordinator::backgroundTaskResults() const
{
    return m_backgroundTaskResults;
}

QString ToolCoordinator::latestTaskToast() const
{
    return m_latestTaskToast;
}

QString ToolCoordinator::latestTaskToastTone() const
{
    return m_latestTaskToastTone;
}

int ToolCoordinator::latestTaskToastTaskId() const
{
    return m_latestTaskToastTaskId;
}

QString ToolCoordinator::latestTaskToastType() const
{
    return m_latestTaskToastType;
}

int ToolCoordinator::surfaceBackgroundTaskId() const
{
    return m_surfaceBackgroundTaskId;
}

QString ToolCoordinator::surfaceBackgroundPrimary() const
{
    return m_surfaceBackgroundPrimary;
}

QString ToolCoordinator::surfaceBackgroundSecondary() const
{
    return m_surfaceBackgroundSecondary;
}

void ToolCoordinator::dispatchBackgroundTasks(TaskDispatcher *dispatcher, const QList<AgentTask> &tasks)
{
    if (dispatcher == nullptr) {
        return;
    }

    QList<AgentTask> sortedTasks = tasks;
    std::sort(sortedTasks.begin(), sortedTasks.end(), [](const AgentTask &left, const AgentTask &right) {
        return left.priority > right.priority;
    });

    for (AgentTask task : sortedTasks) {
        task.id = m_nextTaskId++;
        task.createdAtMs = QDateTime::currentMSecsSinceEpoch();
        task.state = TaskState::Pending;
        m_knownBackgroundTasks.insert(task.id, task);
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("[TaskDispatcher] created %1 #%2").arg(task.type).arg(task.id));
        }
        dispatcher->enqueue(task);
    }

    refreshBackgroundTaskSurface();
}

bool ToolCoordinator::handleTaskCanceled(int taskId)
{
    m_knownBackgroundTasks.remove(taskId);

    for (auto it = m_activeBackgroundTaskIds.begin(); it != m_activeBackgroundTaskIds.end();) {
        if (it.value() == taskId) {
            it = m_activeBackgroundTaskIds.erase(it);
        } else {
            ++it;
        }
    }

    return refreshBackgroundTaskSurface();
}

bool ToolCoordinator::handleTaskActivated(const QString &taskKey, int taskId)
{
    m_activeBackgroundTaskIds.insert(taskKey, taskId);
    if (m_knownBackgroundTasks.contains(taskId)) {
        AgentTask task = m_knownBackgroundTasks.value(taskId);
        task.state = TaskState::Running;
        m_knownBackgroundTasks.insert(taskId, task);
    }
    return refreshBackgroundTaskSurface();
}

ToolResultHandling ToolCoordinator::handleTaskResult(const QJsonObject &resultObject, bool backgroundPanelVisible)
{
    ToolResultHandling handling;
    BackgroundTaskResult result;
    result.taskId = resultObject.value(QStringLiteral("taskId")).toInt();
    result.type = resultObject.value(QStringLiteral("type")).toString();
    result.success = resultObject.value(QStringLiteral("success")).toBool();
    result.state = static_cast<TaskState>(resultObject.value(QStringLiteral("state")).toInt(static_cast<int>(TaskState::Finished)));
    result.errorKind = static_cast<ToolErrorKind>(resultObject.value(QStringLiteral("errorKind")).toInt(static_cast<int>(ToolErrorKind::Unknown)));
    result.title = resultObject.value(QStringLiteral("title")).toString();
    result.summary = resultObject.value(QStringLiteral("summary")).toString();
    result.detail = resultObject.value(QStringLiteral("detail")).toString();
    result.payload = resultObject.value(QStringLiteral("payload")).toObject();
    result.finishedAt = resultObject.value(QStringLiteral("finishedAt")).toString();
    result.taskKey = resultObject.value(QStringLiteral("taskKey")).toString();
    result.connectorEventId = resultObject.value(QStringLiteral("connectorEventId")).toString();
    result.connectorEventLive = resultObject.value(QStringLiteral("connectorEventLive")).toBool(false);

    const QString activeKey = result.taskKey.isEmpty() ? result.type : result.taskKey;
    const int activeTaskId = m_activeBackgroundTaskIds.value(activeKey, -1);
    if (activeTaskId != result.taskId) {
        m_knownBackgroundTasks.remove(result.taskId);
        handling.ignored = true;
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("[UI] ignored stale task id=%1 type=%2 key=%3 active=%4")
                                       .arg(result.taskId)
                                       .arg(result.type)
                                       .arg(activeKey)
                                       .arg(activeTaskId));
        }
        return handling;
    }

    if (result.state == TaskState::Canceled) {
        m_knownBackgroundTasks.remove(result.taskId);
        if (m_activeBackgroundTaskIds.value(activeKey, -1) == result.taskId) {
            m_activeBackgroundTaskIds.remove(activeKey);
        }
        handling.ignored = true;
        handling.surfaceChanged = refreshBackgroundTaskSurface();
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("[UI] ignored canceled task id=%1 type=%2 key=%3")
                                       .arg(result.taskId)
                                       .arg(result.type)
                                       .arg(activeKey));
        }
        return handling;
    }

    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("[TaskDispatcher] finished %1 #%2")
                                   .arg(result.type)
                                   .arg(result.taskId));
    }

    m_knownBackgroundTasks.remove(result.taskId);
    if (m_activeBackgroundTaskIds.value(activeKey, -1) == result.taskId) {
        m_activeBackgroundTaskIds.remove(activeKey);
    }
    handling.surfaceChanged = refreshBackgroundTaskSurface();

    m_backgroundTaskResults.prepend(result);
    while (m_backgroundTaskResults.size() > 40) {
        m_backgroundTaskResults.removeLast();
    }
    handling.resultsChanged = backgroundPanelVisible;

    m_latestTaskToastTaskId = result.taskId;
    m_latestTaskToast = m_executionNarrator
        ? m_executionNarrator->summarizeBackgroundResult(result)
        : (result.summary.isEmpty() ? result.title : result.summary);
    m_latestTaskToastTone = result.success ? QStringLiteral("response") : QStringLiteral("error");
    m_latestTaskToastType = result.type;
    handling.toastChanged = true;

    handling.appendTrace = true;
    handling.traceKind = QStringLiteral("tool_result");
    handling.traceTitle = result.type;
    handling.traceDetail = result.detail.left(600);
    handling.traceSuccess = result.success;
    handling.completedResult = result;
    if (!result.connectorEventLive) {
        const ConnectorEvent connectorEvent = ConnectorEventBuilder::fromBackgroundTaskResult(result);
        if (connectorEvent.isValid()) {
            handling.connectorEvent = connectorEvent;
        }
    }
    return handling;
}

QList<AgentToolResult> ToolCoordinator::executeAgentToolCalls(
    const QList<AgentToolCall> &toolCalls,
    AgentToolbox *agentToolbox,
    const std::function<void(const QString &, const QString &, const QString &, bool)> &traceCallback) const
{
    QList<AgentToolResult> results;
    results.reserve(toolCalls.size());
    if (agentToolbox == nullptr) {
        return results;
    }

    for (const AgentToolCall &toolCall : toolCalls) {
        if (m_loggingService) {
            m_loggingService->infoFor(
                QStringLiteral("tool_audit"),
                QStringLiteral("[tool_call] id=%1 name=%2 args=%3")
                    .arg(toolCall.id, toolCall.name, toolCall.argumentsJson.left(8000)));
        }
        if (traceCallback) {
            traceCallback(QStringLiteral("tool_call"), toolCall.name, toolCall.argumentsJson.left(500), true);
        }
        const AgentToolResult result = agentToolbox->execute(toolCall);
        if (m_loggingService) {
            m_loggingService->infoFor(
                QStringLiteral("tool_audit"),
                QStringLiteral("[tool_result] id=%1 name=%2 success=%3 errorKind=%4 summary=%5 output=%6")
                    .arg(result.callId,
                         result.toolName,
                         result.success ? QStringLiteral("true") : QStringLiteral("false"),
                         QString::number(static_cast<int>(result.errorKind)),
                         result.summary.simplified(),
                         result.output.left(8000)));
        }
        if (traceCallback) {
            const QString detail = m_executionNarrator
                ? m_executionNarrator->summarizeToolResult(result)
                : result.output.left(800);
            traceCallback(QStringLiteral("tool_result"), result.toolName, detail.left(800), result.success);
        }
        results.push_back(result);
    }
    return results;
}

QList<AgentToolCall> ToolCoordinator::filterAllowedToolCalls(
    const QList<AgentToolCall> &toolCalls,
    const QStringList &allowedToolNames,
    const std::function<void(const QString &, const QString &, const QString &, bool)> &traceCallback) const
{
    QList<AgentToolCall> accepted;
    for (const AgentToolCall &call : toolCalls) {
        if (!allowedToolNames.isEmpty() && !allowedToolNames.contains(call.name)) {
            if (m_loggingService) {
                m_loggingService->warnFor(
                    QStringLiteral("tool_audit"),
                    QStringLiteral("[tool_call_rejected] name=%1 reason=not_allowed")
                        .arg(call.name));
            }
            if (traceCallback) {
                traceCallback(QStringLiteral("validation"),
                              QStringLiteral("Rejected tool call"),
                              QStringLiteral("Tool %1 is not allowed for this intent.").arg(call.name),
                              false);
            }
            continue;
        }
        accepted.push_back(call);
    }
    return accepted;
}

std::optional<WebSearchFollowUp> ToolCoordinator::buildWebSearchFollowUp(const BackgroundTaskResult &result) const
{
    QString query = result.payload.value(QStringLiteral("query")).toString().trimmed();
    const QString provider = result.payload.value(QStringLiteral("provider")).toString().trimmed();
    const QString content = clippedWebSearchPayload(result);
    const bool reliable = result.payload.value(QStringLiteral("reliable")).toBool(true);
    const QString reliabilityReason = result.payload.value(QStringLiteral("reliability_reason")).toString().trimmed();
    if (query.isEmpty()) {
        query = result.summary.trimmed();
    }
    if (query.isEmpty()) {
        query = result.title.trimmed();
    }
    if (query.isEmpty()) {
        query = QStringLiteral("the latest search result");
    }
    if (content.trimmed().isEmpty()) {
        if (m_loggingService) {
            m_loggingService->warnFor(
                QStringLiteral("tool_audit"),
                QStringLiteral("[web_search_follow_up] skipped because payload content is empty query=%1")
                    .arg(query.left(240)));
        }
        return std::nullopt;
    }

    WebSearchFollowUp followUp;
    if (!reliable) {
        if (m_loggingService) {
            m_loggingService->warnFor(
                QStringLiteral("follow_up_audit"),
                QStringLiteral("[web_search_follow_up] downgraded_to_local_response query=%1 reason=%2")
                    .arg(query.left(240), reliabilityReason.left(240)));
        }
        followUp.deliverLocalResponse = true;
        followUp.localResponseText = m_executionNarrator
            ? m_executionNarrator->webSearchLowConfidence(result)
            : QStringLiteral("I couldn't verify reliable web sources for that yet. Please try a more specific query or ask for source details.");
        followUp.localResponseStatus = reliabilityReason.isEmpty()
            ? QStringLiteral("Web search low confidence")
            : reliabilityReason;
        return followUp;
    }

    const QString clippedContent = content.left(12000);
    const QString lowered = query.toLower();
    const bool wantsDetails = lowered.contains(QStringLiteral("explain"))
        || lowered.contains(QStringLiteral("why"))
        || lowered.contains(QStringLiteral("how"))
        || lowered.contains(QStringLiteral("compare"))
        || lowered.contains(QStringLiteral("details"))
        || lowered.contains(QStringLiteral("summary"));

    followUp.logPrompt = query;
    followUp.synthesisInput = wantsDetails
        ? QStringLiteral(
              "You previously asked me to search the web. "
              "Provide the final answer using only the fetched payload below. "
              "Keep it concise, accurate, and include uncertainty only if needed. "
              "Do not claim hidden browsing beyond this data.\n\n"
              "User query: %1\n"
              "Search provider: %2\n"
              "Fetched payload (JSON/text):\n%3")
              .arg(query, provider.isEmpty() ? QStringLiteral("unknown") : provider, clippedContent)
        : QStringLiteral(
              "You previously asked me to search the web. "
              "Return ONLY the direct answer from the fetched payload below. "
              "Do not provide explanations, context, caveats, or extra sentences unless the user asked for details. "
              "Use one short sentence, ideally 4-12 words.\n\n"
              "User query: %1\n"
              "Search provider: %2\n"
              "Fetched payload (JSON/text):\n%3")
              .arg(query, provider.isEmpty() ? QStringLiteral("unknown") : provider, clippedContent);
    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("follow_up_audit"),
            QStringLiteral("[web_search_follow_up] query=%1 provider=%2 wantsDetails=%3 synthesisInputChars=%4")
                .arg(query.left(240),
                     provider.isEmpty() ? QStringLiteral("unknown") : provider,
                     wantsDetails ? QStringLiteral("true") : QStringLiteral("false"),
                     QString::number(followUp.synthesisInput.size())));
        m_loggingService->infoFor(
            QStringLiteral("ai_prompt"),
            QStringLiteral("[web_search_follow_up_prompt]\n%1")
                .arg(followUp.synthesisInput.left(200000)));
    }
    return followUp;
}

bool ToolCoordinator::refreshBackgroundTaskSurface()
{
    int nextTaskId = -1;
    QString nextPrimary;
    QString nextSecondary;
    qint64 nextCreatedAt = -1;

    for (auto it = m_activeBackgroundTaskIds.cbegin(); it != m_activeBackgroundTaskIds.cend(); ++it) {
        const int taskId = it.value();
        const AgentTask task = m_knownBackgroundTasks.value(taskId);
        if (task.id <= 0 || task.type.isEmpty()) {
            continue;
        }

        const auto copy = backgroundTaskSurfaceCopy(task);
        if (copy.first.isEmpty()) {
            continue;
        }

        if (task.createdAtMs >= nextCreatedAt) {
            nextCreatedAt = task.createdAtMs;
            nextTaskId = taskId;
            nextPrimary = copy.first;
            nextSecondary = copy.second;
        }
    }

    if (m_surfaceBackgroundTaskId == nextTaskId
        && m_surfaceBackgroundPrimary == nextPrimary
        && m_surfaceBackgroundSecondary == nextSecondary) {
        return false;
    }

    m_surfaceBackgroundTaskId = nextTaskId;
    m_surfaceBackgroundPrimary = nextPrimary;
    m_surfaceBackgroundSecondary = nextSecondary;
    return true;
}

QPair<QString, QString> ToolCoordinator::backgroundTaskSurfaceCopy(const AgentTask &task) const
{
    if (m_executionNarrator != nullptr) {
        return m_executionNarrator->describeBackgroundTask(task);
    }
    return {};
}
