#include "core/ToolCoordinator.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUrl>

#include "agent/AgentToolbox.h"
#include "core/tasks/TaskDispatcher.h"
#include "logging/LoggingService.h"

namespace {
QString compactSurfaceText(QString text, int maxLength = 72)
{
    text = text.simplified();
    if (text.size() > maxLength) {
        text = text.left(maxLength - 3).trimmed() + QStringLiteral("...");
    }
    return text;
}

QString formatDurationForSurface(int totalSeconds)
{
    if (totalSeconds <= 0) {
        return {};
    }

    if (totalSeconds >= 3600) {
        const int hours = totalSeconds / 3600;
        const int minutes = (totalSeconds % 3600) / 60;
        return minutes > 0
            ? QStringLiteral("%1 hr %2 min").arg(hours).arg(minutes)
            : QStringLiteral("%1 hr").arg(hours);
    }

    if (totalSeconds >= 60) {
        const int minutes = totalSeconds / 60;
        const int seconds = totalSeconds % 60;
        return seconds > 0
            ? QStringLiteral("%1 min %2 sec").arg(minutes).arg(seconds)
            : QStringLiteral("%1 min").arg(minutes);
    }

    return QStringLiteral("%1 sec").arg(totalSeconds);
}

QString firstNonEmptyArg(const QJsonObject &args, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QString value = args.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString compactPathForSurface(const QString &pathText)
{
    const QString trimmed = pathText.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QFileInfo info(trimmed);
    const QString fileName = info.fileName().trimmed();
    if (!fileName.isEmpty()) {
        return compactSurfaceText(fileName, 56);
    }

    return compactSurfaceText(trimmed, 56);
}

QString compactUrlForSurface(const QString &urlText)
{
    const QString trimmed = urlText.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QUrl url(trimmed);
    if (url.isValid() && !url.host().trimmed().isEmpty()) {
        return compactSurfaceText(url.host(), 48);
    }

    return compactSurfaceText(trimmed, 48);
}
}

ToolCoordinator::ToolCoordinator(LoggingService *loggingService)
    : m_loggingService(loggingService)
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
    m_latestTaskToast = result.summary.isEmpty() ? result.title : result.summary;
    m_latestTaskToastTone = result.success ? QStringLiteral("response") : QStringLiteral("error");
    m_latestTaskToastType = result.type;
    handling.toastChanged = true;

    handling.appendTrace = true;
    handling.traceKind = QStringLiteral("tool_result");
    handling.traceTitle = result.type;
    handling.traceDetail = result.detail.left(600);
    handling.traceSuccess = result.success;
    handling.completedResult = result;
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
        if (traceCallback) {
            traceCallback(QStringLiteral("tool_call"), toolCall.name, toolCall.argumentsJson.left(500), true);
        }
        const AgentToolResult result = agentToolbox->execute(toolCall);
        if (traceCallback) {
            traceCallback(QStringLiteral("tool_result"), result.toolName, result.output.left(800), result.success);
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
    const QString query = result.payload.value(QStringLiteral("query")).toString().trimmed();
    const QString provider = result.payload.value(QStringLiteral("provider")).toString().trimmed();
    const QString content = result.payload.value(QStringLiteral("content")).toString();
    const bool reliable = result.payload.value(QStringLiteral("reliable")).toBool(true);
    const QString reliabilityReason = result.payload.value(QStringLiteral("reliability_reason")).toString().trimmed();
    if (query.isEmpty() || content.trimmed().isEmpty()) {
        return std::nullopt;
    }

    WebSearchFollowUp followUp;
    if (!reliable) {
        followUp.deliverLocalResponse = true;
        followUp.localResponseText = QStringLiteral("I couldn't verify reliable web sources for that yet. Please try a more specific query or ask for source details.");
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
    const QJsonObject &args = task.args;
    const QString type = task.type.trimmed().toLower();

    if (type == QStringLiteral("web_search")) {
        return {
            QStringLiteral("Searching the web..."),
            compactSurfaceText(firstNonEmptyArg(args, {QStringLiteral("query"), QStringLiteral("q")}), 64)
        };
    }

    if (type == QStringLiteral("dir_list")) {
        return {
            QStringLiteral("Listing files..."),
            compactPathForSurface(firstNonEmptyArg(args, {QStringLiteral("path"), QStringLiteral("directory"), QStringLiteral("dir")}))
        };
    }

    if (type == QStringLiteral("file_read")) {
        return {
            QStringLiteral("Opening file..."),
            compactPathForSurface(firstNonEmptyArg(args, {QStringLiteral("path"), QStringLiteral("file"), QStringLiteral("filename")}))
        };
    }

    if (type == QStringLiteral("file_write") || type == QStringLiteral("computer_write_file")) {
        return {
            QStringLiteral("Writing file..."),
            compactPathForSurface(firstNonEmptyArg(args, {QStringLiteral("path"), QStringLiteral("file"), QStringLiteral("filename")}))
        };
    }

    if (type == QStringLiteral("memory_write")) {
        return {
            QStringLiteral("Saving memory..."),
            compactSurfaceText(firstNonEmptyArg(args, {QStringLiteral("summary"), QStringLiteral("title"), QStringLiteral("memory"), QStringLiteral("content")}), 60)
        };
    }

    if (type == QStringLiteral("computer_open_app")) {
        return {
            QStringLiteral("Opening app..."),
            compactSurfaceText(firstNonEmptyArg(args, {QStringLiteral("app"), QStringLiteral("name"), QStringLiteral("application")}), 48)
        };
    }

    if (type == QStringLiteral("computer_open_url") || type == QStringLiteral("browser_open")) {
        return {
            QStringLiteral("Opening link..."),
            compactUrlForSurface(firstNonEmptyArg(args, {QStringLiteral("url"), QStringLiteral("link")}))
        };
    }

    if (type == QStringLiteral("computer_set_timer")) {
        QString secondary;
        const int seconds = args.value(QStringLiteral("seconds")).toInt();
        if (seconds > 0) {
            secondary = formatDurationForSurface(seconds);
        } else {
            secondary = compactSurfaceText(firstNonEmptyArg(args, {QStringLiteral("duration"), QStringLiteral("label")}), 48);
        }
        return {QStringLiteral("Setting timer..."), secondary};
    }

    return {
        QStringLiteral("Tool running..."),
        compactSurfaceText(firstNonEmptyArg(args, {QStringLiteral("query"), QStringLiteral("path"), QStringLiteral("file"), QStringLiteral("url"), QStringLiteral("name")}), 56)
    };
}
