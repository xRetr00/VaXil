#include "core/tasks/ToolWorker.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

#include "cognition/ConnectorEventBuilder.h"
#include "core/tools/ToolExecutionService.h"
#include "logging/LoggingService.h"
#include "settings/AppSettings.h"

namespace {
constexpr qint64 kDirListCooldownMs = 2000;
constexpr qint64 kDirListCacheMs = 2500;
}

ToolWorker::ToolWorker(const QStringList &allowedRoots,
                       LoggingService *loggingService,
                       AppSettings *settings,
                       QObject *parent)
    : QObject(parent)
    , m_allowedRoots(allowedRoots)
    , m_loggingService(loggingService)
    , m_settings(settings)
    , m_toolExecutionService(new ToolExecutionService(allowedRoots, settings, nullptr, loggingService, this))
{
}

void ToolWorker::processTask(const AgentTask &task)
{
    emit taskStarted(task.id, task.type);

    if (m_canceledTaskIds.contains(task.id)) {
        AgentTask canceledTask = task;
        emit taskFinished(task.id,
                          ToolExecutionService::taskResultObject(
                              canceledTask,
                              ToolExecutionResult{
                                  .toolName = task.type,
                                  .success = false,
                                  .errorKind = ToolErrorKind::Unknown,
                                  .summary = QStringLiteral("Canceled"),
                                  .detail = QStringLiteral("Task was canceled before execution.")
                              },
                              TaskState::Canceled));
        return;
    }

    if (const auto cached = cachedResultFor(task); cached.has_value()) {
        if (m_loggingService) {
            m_loggingService->infoFor(QStringLiteral("tools_mcp"), QStringLiteral("[ToolWorker] cache hit id=%1 type=%2").arg(task.id).arg(task.type));
        }

        QJsonObject cachedResult = *cached;
        cachedResult.insert(QStringLiteral("taskId"), task.id);
        cachedResult.insert(QStringLiteral("finishedAt"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        emit taskFinished(task.id, cachedResult);
        return;
    }

    if (isCooldownActive(task)) {
        emit taskFinished(task.id,
                          ToolExecutionService::taskResultObject(
                              task,
                              ToolExecutionResult{
                                  .toolName = task.type,
                                  .success = false,
                                  .errorKind = ToolErrorKind::Timeout,
                                  .summary = QStringLiteral("Task rate-limited"),
                                  .detail = QStringLiteral("Cooldown is still active for %1").arg(task.type)
                              },
                              TaskState::Expired));
        return;
    }

    if (m_loggingService) {
        m_loggingService->infoFor(QStringLiteral("tools_mcp"), QStringLiteral("[ToolWorker] executed %1 id=%2").arg(task.type).arg(task.id));
    }

    ToolExecutionRequest request{
        .toolName = task.type,
        .args = task.args
    };
    ToolExecutionResult execution = m_toolExecutionService->execute(request);
    QJsonObject result = ToolExecutionService::taskResultObject(task, execution);

    if (task.type == QStringLiteral("dir_list")) {
        CachedResult cached;
        cached.result = result;
        cached.cachedAtMs = QDateTime::currentMSecsSinceEpoch();
        m_cache.insert(cacheKeyFor(task), cached);
    }

    if (!m_lastExecution.contains(task.type)) {
        QElapsedTimer timer;
        timer.start();
        m_lastExecution.insert(task.type, timer);
    } else {
        m_lastExecution[task.type].restart();
    }

    if (m_canceledTaskIds.contains(task.id)) {
        result = ToolExecutionService::taskResultObject(
            task,
            ToolExecutionResult{
                .toolName = task.type,
                .success = false,
                .errorKind = ToolErrorKind::Unknown,
                .summary = QStringLiteral("Canceled"),
                .detail = QStringLiteral("Task completed after cancellation and was ignored.")
            },
            TaskState::Canceled);
    } else {
        const ConnectorEvent connectorEvent = ConnectorEventBuilder::fromTaskExecution(task, execution);
        if (connectorEvent.isValid()) {
            result.insert(QStringLiteral("connectorEventId"), connectorEvent.eventId);
            result.insert(QStringLiteral("connectorEventLive"), true);
            emit connectorEventReady(connectorEvent);
        }
    }

    emit taskFinished(task.id, result);
}

void ToolWorker::cancelTask(int taskId)
{
    m_canceledTaskIds.insert(taskId);
}

bool ToolWorker::isReadablePath(const QString &path) const
{
    QFileInfo info(path);
    if (!info.exists()) {
        return false;
    }

    if (info.isDir()) {
        return QDir(info.absoluteFilePath()).exists();
    }

    return info.isFile() && info.isReadable();
}

bool ToolWorker::isWritablePath(const QString &path) const
{
    const QString resolved = normalizePath(path);
    for (const QString &root : m_allowedRoots) {
        if (!root.isEmpty() && resolved.startsWith(root, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QString ToolWorker::normalizePath(const QString &path) const
{
    const QFileInfo info(path);
    if (info.exists()) {
        return info.canonicalFilePath();
    }
    return QDir::cleanPath(info.absoluteFilePath());
}

QString ToolWorker::cacheKeyFor(const AgentTask &task) const
{
    return task.type + QStringLiteral("|")
        + QString::fromUtf8(QJsonDocument(task.args).toJson(QJsonDocument::Compact));
}

bool ToolWorker::isCooldownActive(const AgentTask &task) const
{
    if (task.type != QStringLiteral("dir_list")) {
        return false;
    }
    if (!m_lastExecution.contains(task.type)) {
        return false;
    }
    const QElapsedTimer timer = m_lastExecution.value(task.type);
    return timer.isValid() && timer.elapsed() < kDirListCooldownMs;
}

std::optional<QJsonObject> ToolWorker::cachedResultFor(const AgentTask &task) const
{
    if (task.type != QStringLiteral("dir_list")) {
        return std::nullopt;
    }

    const auto it = m_cache.constFind(cacheKeyFor(task));
    if (it == m_cache.cend()) {
        return std::nullopt;
    }

    if (QDateTime::currentMSecsSinceEpoch() - it->cachedAtMs > kDirListCacheMs) {
        return std::nullopt;
    }

    return it->result;
}
