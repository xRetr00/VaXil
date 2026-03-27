#include "core/tasks/ToolWorker.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTextStream>
#include <QUrl>

#include "core/ComputerControl.h"
#include "logging/LoggingService.h"
#include "memory/MemoryManager.h"
#include "settings/AppSettings.h"

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

QString quotePowerShell(QString value)
{
    value.replace(QStringLiteral("'"), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
}

QString runPowerShell(const QString &command, int timeoutMs, bool *ok = nullptr)
{
    QProcess process;
    process.start(QStringLiteral("powershell"),
                  {QStringLiteral("-NoProfile"),
                   QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                   QStringLiteral("-Command"),
                   command});
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        if (ok) {
            *ok = false;
        }
        return {};
    }

    const bool success = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (ok) {
        *ok = success;
    }

    return QString::fromUtf8(process.readAllStandardOutput());
}

QString latestAiLogPath()
{
    const QDir logDir(QDir::currentPath() + QStringLiteral("/bin/logs/AI"));
    const QFileInfoList files = logDir.entryInfoList({QStringLiteral("*.log")}, QDir::Files | QDir::Readable, QDir::Time);
    return files.isEmpty() ? QString{} : files.first().absoluteFilePath();
}
}

ToolWorker::ToolWorker(const QStringList &allowedRoots,
                       LoggingService *loggingService,
                       AppSettings *settings,
                       QObject *parent)
    : QObject(parent)
    , m_allowedRoots(allowedRoots)
    , m_loggingService(loggingService)
    , m_settings(settings)
    , m_memoryManager(std::make_unique<MemoryManager>())
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
    } else if (task.type == QStringLiteral("log_tail")) {
        result = processLogTail(task);
    } else if (task.type == QStringLiteral("log_search")) {
        result = processLogSearch(task);
    } else if (task.type == QStringLiteral("ai_log_read")) {
        result = processAiLogRead(task);
    } else if (task.type == QStringLiteral("web_search")) {
        result = processWebSearch(task);
    } else if (task.type == QStringLiteral("memory_write")) {
        result = processMemoryWrite(task);
    } else if (task.type == QStringLiteral("computer_list_apps")) {
        result = processComputerListApps(task);
    } else if (task.type == QStringLiteral("computer_open_app")) {
        result = processComputerOpenApp(task);
    } else if (task.type == QStringLiteral("computer_open_url")) {
        result = processComputerOpenUrl(task);
    } else if (task.type == QStringLiteral("computer_write_file")) {
        result = processComputerWriteFile(task);
    } else if (task.type == QStringLiteral("computer_set_timer")) {
        result = processComputerSetTimer(task);
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
    if (!isReadablePath(path)) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Directory unavailable"),
                           QStringLiteral("That folder is not readable."),
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
    if (!isReadablePath(path)) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("File unavailable"),
                           QStringLiteral("That file is not readable."),
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
    if (!isWritablePath(path)) {
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

QJsonObject ToolWorker::processLogTail(const AgentTask &task)
{
    const QString path = task.args.value(QStringLiteral("path")).toString();
    const int lines = task.args.value(QStringLiteral("lines")).toInt(120);
    if (!isReadablePath(path)) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Log unavailable"),
                           QStringLiteral("That log file is not readable."),
                           QStringLiteral("Rejected path: %1").arg(path));
    }

    bool ok = false;
    const QString output = runPowerShell(
        QStringLiteral("Get-Content %1 -Tail %2")
            .arg(quotePowerShell(path))
            .arg(lines),
        12000,
        &ok);
    if (!ok) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Log read failed"),
                           QStringLiteral("I couldn't read the log tail."),
                           QStringLiteral("Failed to tail: %1").arg(QDir::cleanPath(path)));
    }

    return buildResult(task,
                       true,
                       TaskState::Finished,
                       QStringLiteral("Log tail ready"),
                       QStringLiteral("Read the latest %1 lines from %2").arg(lines).arg(QFileInfo(path).fileName()),
                       QStringLiteral("Read log tail from %1").arg(QDir::cleanPath(path)),
                       QJsonObject{
                           {QStringLiteral("path"), QDir::cleanPath(path)},
                           {QStringLiteral("content"), output}
                       });
}

QJsonObject ToolWorker::processLogSearch(const AgentTask &task)
{
    const QString path = task.args.value(QStringLiteral("path")).toString();
    const QString query = task.args.value(QStringLiteral("query")).toString();
    if (!isReadablePath(path)) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Log unavailable"),
                           QStringLiteral("That log path is not readable."),
                           QStringLiteral("Rejected path: %1").arg(path));
    }

    const QString command = QFileInfo(path).isDir()
        ? QStringLiteral("Get-ChildItem -Path %1 -Recurse | Select-String -Pattern %2")
              .arg(quotePowerShell(path), quotePowerShell(query))
        : QStringLiteral("Select-String -Path %1 -Pattern %2")
              .arg(quotePowerShell(path), quotePowerShell(query));
    bool ok = false;
    const QString output = runPowerShell(command, 15000, &ok);
    if (!ok) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Log search failed"),
                           QStringLiteral("I couldn't search that log path."),
                           QStringLiteral("Failed to search %1 for %2").arg(QDir::cleanPath(path), query));
    }

    return buildResult(task,
                       true,
                       TaskState::Finished,
                       QStringLiteral("Log search ready"),
                       QStringLiteral("Searched %1 for \"%2\"").arg(QFileInfo(path).fileName(), query),
                       QStringLiteral("Search completed in %1").arg(QDir::cleanPath(path)),
                       QJsonObject{
                           {QStringLiteral("path"), QDir::cleanPath(path)},
                           {QStringLiteral("query"), query},
                           {QStringLiteral("content"), output}
                       });
}

QJsonObject ToolWorker::processAiLogRead(const AgentTask &task)
{
    QString path = task.args.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        path = latestAiLogPath();
    }

    if (path.isEmpty() || !isReadablePath(path)) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("AI log unavailable"),
                           QStringLiteral("I couldn't find a readable AI log."),
                           QStringLiteral("No readable AI log was available."));
    }

    const QString content = readText(path, 18000);
    if (content.isEmpty()) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("AI log read failed"),
                           QStringLiteral("I couldn't read that AI log."),
                           QStringLiteral("Failed to read: %1").arg(QDir::cleanPath(path)));
    }

    return buildResult(task,
                       true,
                       TaskState::Finished,
                       QStringLiteral("AI log opened"),
                       QStringLiteral("Read %1").arg(QFileInfo(path).fileName()),
                       QStringLiteral("Read AI log: %1").arg(QDir::cleanPath(path)),
                       QJsonObject{
                           {QStringLiteral("path"), QDir::cleanPath(path)},
                           {QStringLiteral("content"), content}
                       });
}

QJsonObject ToolWorker::processWebSearch(const AgentTask &task)
{
    const QString query = task.args.value(QStringLiteral("query")).toString();
    if (query.trimmed().isEmpty()) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Web search failed"),
                           QStringLiteral("A search query is required."),
                           QStringLiteral("Missing query argument."));
    }

    QString provider = m_settings ? m_settings->webSearchProvider().trimmed().toLower() : QStringLiteral("duckduckgo");
    if (provider.isEmpty()) {
        provider = QStringLiteral("duckduckgo");
    }

    QString freshness = task.args.value(QStringLiteral("freshness")).toString().trimmed().toLower();
    if (freshness.isEmpty() && task.args.value(QStringLiteral("prefer_fresh")).toBool(false)) {
        freshness = QStringLiteral("pw");
    }
    if (!freshness.isEmpty()) {
        const QSet<QString> allowedFreshness = {
            QStringLiteral("pd"),
            QStringLiteral("pw"),
            QStringLiteral("pm"),
            QStringLiteral("py")
        };
        if (!allowedFreshness.contains(freshness)) {
            freshness.clear();
        }
    }

    QString output;
    bool ok = false;
    QString providerUsed = provider;
    QString detailSuffix;

    if (provider == QStringLiteral("brave")) {
        QString apiKey = m_settings ? m_settings->braveSearchApiKey().trimmed() : QString();
        if (apiKey.isEmpty()) {
            apiKey = qEnvironmentVariable("BRAVE_SEARCH_API_KEY");
        }

        if (!apiKey.isEmpty()) {
            QString uri = QStringLiteral("https://api.search.brave.com/res/v1/web/search?q=%1")
                              .arg(QString::fromUtf8(QUrl::toPercentEncoding(query)));
            if (!freshness.isEmpty()) {
                uri += QStringLiteral("&freshness=%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(freshness)));
            }

            output = runPowerShell(
                QStringLiteral("$headers=@{'Accept'='application/json';'X-Subscription-Token'=%1}; "
                               "(Invoke-RestMethod -Headers $headers -Uri %2) | ConvertTo-Json -Depth 8")
                    .arg(quotePowerShell(apiKey), quotePowerShell(uri)),
                20000,
                &ok);
        }

        if (!ok) {
            providerUsed = QStringLiteral("duckduckgo");
            detailSuffix = QStringLiteral(" Brave search was unavailable; fell back to DuckDuckGo instant results.");
        }
    }

    if (!ok) {
        output = runPowerShell(
            QStringLiteral("(Invoke-RestMethod -Uri %1) | ConvertTo-Json -Depth 8")
                .arg(quotePowerShell(QStringLiteral("https://api.duckduckgo.com/?q=%1&format=json&no_html=1&skip_disambig=1")
                                         .arg(QString::fromUtf8(QUrl::toPercentEncoding(query))))),
            20000,
            &ok);
    }

    if (!ok) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Web search failed"),
                           QStringLiteral("I couldn't search the web right now."),
                           QStringLiteral("Search query failed: %1").arg(query));
    }

    QString summary = QStringLiteral("Searched the web for \"%1\"").arg(query);
    if (providerUsed == QStringLiteral("brave")) {
        summary += QStringLiteral(" via Brave");
    } else if (provider == QStringLiteral("brave")) {
        summary += QStringLiteral(" via DuckDuckGo fallback");
    }

    return buildResult(task,
                       true,
                       TaskState::Finished,
                       QStringLiteral("Web search ready"),
                       summary,
                       QStringLiteral("Web search completed for %1 using %2.%3")
                           .arg(query, providerUsed, detailSuffix),
                       QJsonObject{
                           {QStringLiteral("query"), query},
                           {QStringLiteral("provider"), providerUsed},
                           {QStringLiteral("freshness"), freshness},
                           {QStringLiteral("content"), output}
                       });
}

QJsonObject ToolWorker::processMemoryWrite(const AgentTask &task)
{
    MemoryEntry entry;
    entry.kind = task.args.value(QStringLiteral("kind")).toString(QStringLiteral("fact"));
    entry.title = task.args.value(QStringLiteral("title")).toString(task.args.value(QStringLiteral("key")).toString(QStringLiteral("general_fact")));
    entry.content = task.args.value(QStringLiteral("content")).toString(task.args.value(QStringLiteral("value")).toString());
    entry.key = task.args.value(QStringLiteral("key")).toString(entry.title);
    entry.value = task.args.value(QStringLiteral("value")).toString(entry.content);
    entry.source = QStringLiteral("tool_worker");
    entry.createdAt = QDateTime::currentDateTimeUtc();

    const bool ok = m_memoryManager->write(entry);
    if (!ok) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Memory write rejected"),
                           QStringLiteral("That memory entry could not be stored."),
                           QStringLiteral("The entry was empty or looked like a secret/full file."));
    }

    return buildResult(task,
                       true,
                       TaskState::Finished,
                       QStringLiteral("Memory saved"),
                       QStringLiteral("Saved memory for %1").arg(entry.key),
                       QStringLiteral("Stored %1 memory entry.").arg(entry.kind),
                       QJsonObject{
                           {QStringLiteral("key"), entry.key},
                           {QStringLiteral("value"), entry.value},
                           {QStringLiteral("kind"), entry.kind}
                       });
}

QJsonObject ToolWorker::processComputerListApps(const AgentTask &task)
{
    const QString query = task.args.value(QStringLiteral("query")).toString();
    const int limit = task.args.value(QStringLiteral("limit")).toInt(20);
    const auto result = ComputerControl::listApps(query, limit);
    return buildResult(task,
                       result.success,
                       TaskState::Finished,
                       result.success ? QStringLiteral("Apps listed") : QStringLiteral("App listing failed"),
                       result.summary,
                       result.detail,
                       QJsonObject{
                           {QStringLiteral("query"), query},
                           {QStringLiteral("items"), QJsonArray::fromStringList(result.lines)}
                       });
}

QJsonObject ToolWorker::processComputerOpenApp(const AgentTask &task)
{
    QStringList arguments;
    const QJsonArray argArray = task.args.value(QStringLiteral("arguments")).toArray();
    for (const QJsonValue &value : argArray) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            arguments.push_back(text);
        }
    }

    const QString target = task.args.value(QStringLiteral("target")).toString();
    const auto result = ComputerControl::launchApp(target, arguments);
    return buildResult(task,
                       result.success,
                       TaskState::Finished,
                       result.success ? QStringLiteral("App opened") : QStringLiteral("App open failed"),
                       result.summary,
                       result.detail,
                       QJsonObject{
                           {QStringLiteral("target"), target},
                           {QStringLiteral("matches"), QJsonArray::fromStringList(result.lines)}
                       });
}

QJsonObject ToolWorker::processComputerOpenUrl(const AgentTask &task)
{
    const QString url = task.args.value(QStringLiteral("url")).toString();
    const auto result = ComputerControl::openUrl(url);
    return buildResult(task,
                       result.success,
                       TaskState::Finished,
                       result.success ? QStringLiteral("URL opened") : QStringLiteral("URL open failed"),
                       result.summary,
                       result.detail,
                       QJsonObject{{QStringLiteral("url"), url}});
}

QJsonObject ToolWorker::processComputerWriteFile(const AgentTask &task)
{
    const QString path = task.args.value(QStringLiteral("path")).toString();
    const QString content = task.args.value(QStringLiteral("content")).toString();
    const QString baseDir = task.args.value(QStringLiteral("base_dir")).toString();
    const bool overwrite = task.args.value(QStringLiteral("overwrite")).toBool(false);
    const auto result = ComputerControl::writeTextFile(path, content, overwrite, baseDir);
    return buildResult(task,
                       result.success,
                       TaskState::Finished,
                       result.success ? QStringLiteral("Computer file written") : QStringLiteral("Computer file write failed"),
                       result.summary,
                       result.detail,
                       QJsonObject{
                           {QStringLiteral("path"), result.resolvedPath},
                           {QStringLiteral("base_dir"), baseDir}
                       });
}

QJsonObject ToolWorker::processComputerSetTimer(const AgentTask &task)
{
    const int durationSeconds = task.args.value(QStringLiteral("duration_seconds")).toInt(0);
    const QString title = task.args.value(QStringLiteral("title")).toString();
    const QString message = task.args.value(QStringLiteral("message")).toString();
    const auto result = ComputerControl::setTimer(durationSeconds, title, message);
    return buildResult(task,
                       result.success,
                       TaskState::Finished,
                       result.success ? QStringLiteral("Timer set") : QStringLiteral("Timer setup failed"),
                       result.summary,
                       result.detail,
                       QJsonObject{
                           {QStringLiteral("duration_seconds"), durationSeconds},
                           {QStringLiteral("title"), title},
                           {QStringLiteral("message"), message}
                       });
}
