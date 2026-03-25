#include "core/tasks/ToolWorker.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

#include "logging/LoggingService.h"

namespace {
constexpr qint64 kDirListCooldownMs = 2000;
constexpr qint64 kDirListCacheMs = 2500;

QString readText(const QString &path, int maxChars = 16000)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QTextStream stream(&file);
    const QString text = stream.readAll();
    if (text.size() > maxChars) {
        return text.left(maxChars) + QStringLiteral("\n...[truncated]");
    }
    return text;
}
}

ToolWorker::ToolWorker(const QStringList &allowedRoots, LoggingService *loggingService, QObject *parent)
    : QObject(parent)
    , m_allowedRoots(allowedRoots)
    , m_loggingService(loggingService)
{
}

void ToolWorker::processTask(const AgentTask &task)
{
    emit taskStarted(task.id, task.type);

    if (m_canceledTaskIds.contains(task.id)) {
        emit taskFinished(task.id, buildResult(task,
                                               false,
                                               TaskState::Canceled,
                                               QStringLiteral("Task canceled"),
                                               QStringLiteral("Canceled"),
                                               QStringLiteral("Task was canceled before execution.")));
        return;
    }

    if (const auto cached = cachedResultFor(task); cached.has_value()) {
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("[ToolWorker] cache hit id=%1 type=%2").arg(task.id).arg(task.type));
        }

        QJsonObject cachedResult = *cached;
        cachedResult.insert(QStringLiteral("taskId"), task.id);
        cachedResult.insert(QStringLiteral("finishedAt"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        emit taskFinished(task.id, cachedResult);
        return;
    }

    if (isCooldownActive(task)) {
        emit taskFinished(task.id, buildResult(task,
                                               false,
                                               TaskState::Expired,
                                               QStringLiteral("Task rate-limited"),
                                               QStringLiteral("Please wait before repeating %1").arg(task.type),
                                               QStringLiteral("Cooldown is still active for %1").arg(task.type)));
        return;
    }

    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("[ToolWorker] executed %1 id=%2").arg(task.type).arg(task.id));
    }

    QJsonObject result;
    if (task.type == QStringLiteral("dir_list")) {
        result = processDirList(task);
    } else if (task.type == QStringLiteral("file_read")) {
        result = processFileRead(task);
    } else if (task.type == QStringLiteral("file_write")) {
        result = processFileWrite(task);
    } else if (task.type == QStringLiteral("memory_write")) {
        result = processMemoryWrite(task);
    } else {
        result = buildResult(task,
                             false,
                             TaskState::Finished,
                             QStringLiteral("Unsupported task"),
                             QStringLiteral("That task type is not supported yet."),
                             QStringLiteral("Unsupported task type: %1").arg(task.type));
    }

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
        result = buildResult(task,
                             false,
                             TaskState::Canceled,
                             QStringLiteral("Task canceled"),
                             QStringLiteral("Canceled"),
                             QStringLiteral("Task completed after cancellation and was ignored."));
    }

    emit taskFinished(task.id, result);
}

void ToolWorker::cancelTask(int taskId)
{
    m_canceledTaskIds.insert(taskId);
}

bool ToolWorker::isAllowedPath(const QString &path) const
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
    QFileInfo info(path);
    if (info.exists()) {
        return info.canonicalFilePath();
    }
    return QDir::cleanPath(info.absoluteFilePath());
}

QString ToolWorker::cacheKeyFor(const AgentTask &task) const
{
    return task.taskKey.isEmpty()
        ? task.type + QStringLiteral("|") + QString::fromUtf8(QJsonDocument(task.args).toJson(QJsonDocument::Compact))
        : task.taskKey;
}

bool ToolWorker::isCooldownActive(const AgentTask &task) const
{
    if (task.type != QStringLiteral("dir_list")) {
        return false;
    }

    const auto it = m_lastExecution.constFind(task.type);
    if (it == m_lastExecution.cend() || !it->isValid()) {
        return false;
    }

    return it->elapsed() < kDirListCooldownMs;
}

std::optional<QJsonObject> ToolWorker::cachedResultFor(const AgentTask &task) const
{
    if (task.type != QStringLiteral("dir_list")) {
        return std::nullopt;
    }

    const QString key = cacheKeyFor(task);
    const auto it = m_cache.constFind(key);
    if (it == m_cache.cend()) {
        return std::nullopt;
    }

    if (QDateTime::currentMSecsSinceEpoch() - it->cachedAtMs > kDirListCacheMs) {
        return std::nullopt;
    }

    QJsonObject result = it->result;
    result.insert(QStringLiteral("summary"), result.value(QStringLiteral("summary")).toString() + QStringLiteral(" (cached)"));
    result.insert(QStringLiteral("detail"), result.value(QStringLiteral("detail")).toString() + QStringLiteral(" [cached]"));
    return result;
}

QJsonObject ToolWorker::buildResult(const AgentTask &task,
                                    bool success,
                                    TaskState state,
                                    const QString &title,
                                    const QString &summary,
                                    const QString &detail,
                                    const QJsonObject &payload) const
{
    return QJsonObject{
        {QStringLiteral("taskId"), task.id},
        {QStringLiteral("type"), task.type},
        {QStringLiteral("success"), success},
        {QStringLiteral("state"), static_cast<int>(state)},
        {QStringLiteral("title"), title},
        {QStringLiteral("summary"), summary},
        {QStringLiteral("detail"), detail},
        {QStringLiteral("payload"), payload},
        {QStringLiteral("finishedAt"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
        {QStringLiteral("taskKey"), cacheKeyFor(task)}
    };
}

QJsonObject ToolWorker::processDirList(const AgentTask &task)
{
    const QString path = task.args.value(QStringLiteral("path")).toString();
    if (!isAllowedPath(path)) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Directory blocked"),
                           QStringLiteral("That folder is outside the allowed roots."),
                           QStringLiteral("Rejected path: %1").arg(path));
    }

    QDir dir(path);
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    QJsonArray renderedEntries;
    for (const QFileInfo &entry : entries.mid(0, 200)) {
        renderedEntries.push_back(QJsonObject{
            {QStringLiteral("name"), entry.fileName()},
            {QStringLiteral("path"), entry.absoluteFilePath()},
            {QStringLiteral("kind"), entry.isDir() ? QStringLiteral("dir") : QStringLiteral("file")},
            {QStringLiteral("size"), static_cast<qint64>(entry.size())}
        });
    }

    const QString summary = QStringLiteral("%1 items listed from %2")
        .arg(entries.size())
        .arg(QDir::cleanPath(path));
    return buildResult(task,
                       true,
                       TaskState::Finished,
                       QStringLiteral("Files listed successfully"),
                       summary,
                       summary,
                       QJsonObject{
                           {QStringLiteral("path"), QDir::cleanPath(path)},
                           {QStringLiteral("entries"), renderedEntries}
                       });
}

QJsonObject ToolWorker::processFileRead(const AgentTask &task)
{
    const QString path = task.args.value(QStringLiteral("path")).toString();
    if (!isAllowedPath(path)) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("File blocked"),
                           QStringLiteral("That file is outside the allowed roots."),
                           QStringLiteral("Rejected path: %1").arg(path));
    }

    const QString content = readText(path);
    if (content.isEmpty()) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Read failed"),
                           QStringLiteral("I couldn't read that file."),
                           QStringLiteral("Failed to read: %1").arg(QDir::cleanPath(path)));
    }

    const QString summary = QStringLiteral("Read %1").arg(QFileInfo(path).fileName());
    return buildResult(task,
                       true,
                       TaskState::Finished,
                       QStringLiteral("File opened"),
                       summary,
                       QStringLiteral("Read file: %1").arg(QDir::cleanPath(path)),
                       QJsonObject{
                           {QStringLiteral("path"), QDir::cleanPath(path)},
                           {QStringLiteral("content"), content}
                       });
}

QJsonObject ToolWorker::processFileWrite(const AgentTask &task)
{
    const QString path = task.args.value(QStringLiteral("path")).toString();
    const QString content = task.args.value(QStringLiteral("content")).toString();
    if (!isAllowedPath(path)) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Write blocked"),
                           QStringLiteral("That destination is outside the allowed roots."),
                           QStringLiteral("Rejected path: %1").arg(path));
    }

    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Write failed"),
                           QStringLiteral("I couldn't write that file."),
                           QStringLiteral("Failed to open for writing: %1").arg(QDir::cleanPath(path)));
    }

    QTextStream stream(&file);
    stream << content;
    stream.flush();

    const QString summary = QStringLiteral("Wrote %1").arg(QFileInfo(path).fileName());
    return buildResult(task,
                       true,
                       TaskState::Finished,
                       QStringLiteral("File written"),
                       summary,
                       QStringLiteral("Saved %1 characters to %2").arg(content.size()).arg(QDir::cleanPath(path)),
                       QJsonObject{
                           {QStringLiteral("path"), QDir::cleanPath(path)},
                           {QStringLiteral("bytes"), content.toUtf8().size()}
                       });
}

QJsonObject ToolWorker::processMemoryWrite(const AgentTask &task) const
{
    const QString title = task.args.value(QStringLiteral("title")).toString();
    const QString content = task.args.value(QStringLiteral("content")).toString();
    return buildResult(task,
                       true,
                       TaskState::Finished,
                       QStringLiteral("Memory write queued"),
                       QStringLiteral("Memory storage stub acknowledged the request."),
                       QStringLiteral("Stub only. Title: %1 | Content: %2").arg(title, content),
                       QJsonObject{
                           {QStringLiteral("title"), title},
                           {QStringLiteral("content"), content}
                       });
}
