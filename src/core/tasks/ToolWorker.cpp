#include "core/tasks/ToolWorker.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include "core/ComputerControl.h"
#include "logging/LoggingService.h"
#include "memory/MemoryManager.h"
#include "settings/AppSettings.h"

namespace {
constexpr qint64 kDirListCooldownMs = 2000;
constexpr qint64 kDirListCacheMs = 2500;

struct NetworkFetchResult
{
    bool ok = false;
    int statusCode = 0;
    QString error;
    QByteArray body;
};

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

NetworkFetchResult httpGet(const QUrl &url,
                           const QList<QPair<QByteArray, QByteArray>> &headers = {},
                           int timeoutMs = 20000)
{
    NetworkFetchResult result;
    if (!url.isValid()) {
        result.error = QStringLiteral("Invalid URL.");
        return result;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VAXIL/1.0"));
    for (const auto &header : headers) {
        request.setRawHeader(header.first, header.second);
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (timer.isActive()) {
        timer.stop();
    } else {
        reply->abort();
        result.error = QStringLiteral("Request timed out.");
        reply->deleteLater();
        return result;
    }

    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    if (reply->error() == QNetworkReply::NoError) {
        result.ok = true;
    } else {
        result.error = reply->errorString();
    }
    reply->deleteLater();
    return result;
}

QString latestAiLogPath()
{
    const QDir logDir(QDir::currentPath() + QStringLiteral("/bin/logs/AI"));
    const QFileInfoList files = logDir.entryInfoList({QStringLiteral("*.log")}, QDir::Files | QDir::Readable, QDir::Time);
    return files.isEmpty() ? QString{} : files.first().absoluteFilePath();
}

QString simplifySearchQuery(QString query)
{
    query = query.trimmed();
    query.remove(QRegularExpression(QStringLiteral("^(about|regarding|tell me about|what about)\\s+"),
                                    QRegularExpression::CaseInsensitiveOption));
    query.remove(QRegularExpression(QStringLiteral("^(latest\\s+)?(models?|news)\\s+(made\\s+by|from|of)\\s+"),
                                    QRegularExpression::CaseInsensitiveOption));
    query = query.trimmed();
    query.remove(QRegularExpression(QStringLiteral("^[\\s,.:;!?-]+|[\\s,.:;!?-]+$")));
    return query;
}

QStringList buildSearchVariants(const QString &query)
{
    const QString base = simplifySearchQuery(query).isEmpty() ? query.trimmed() : simplifySearchQuery(query);
    QStringList variants;
    variants << base;
    variants << (base + QStringLiteral(" official announcement"));
    variants << (base + QStringLiteral(" latest update official source"));

    const QString lowered = base.toLower();
    if (lowered.contains(QStringLiteral("openai"))) {
        variants << QStringLiteral("site:openai.com %1").arg(base);
        variants << QStringLiteral("site:platform.openai.com %1").arg(base);
    }

    QStringList cleaned;
    QSet<QString> seen;
    for (QString variant : variants) {
        variant = variant.simplified();
        if (!variant.isEmpty()) {
            const QString key = variant.toLower();
            if (!seen.contains(key)) {
                seen.insert(key);
                cleaned.push_back(variant);
            }
        }
    }
    return cleaned;
}

QString hostFromUrl(const QString &url)
{
    const QUrl parsed(url);
    return parsed.host().toLower();
}

bool isAuthoritativeHost(const QString &host)
{
    static const QStringList authoritative{
        QStringLiteral("openai.com"),
        QStringLiteral("platform.openai.com"),
        QStringLiteral("developers.openai.com"),
        QStringLiteral("learn.microsoft.com"),
        QStringLiteral("azure.microsoft.com"),
        QStringLiteral("anthropic.com"),
        QStringLiteral("arxiv.org")
    };
    for (const QString &entry : authoritative) {
        if (host == entry || host.endsWith(QStringLiteral(".") + entry)) {
            return true;
        }
    }
    return false;
}

bool isLowSignalHost(const QString &host)
{
    static const QStringList lowSignal{
        QStringLiteral("youtube.com"),
        QStringLiteral("m.youtube.com"),
        QStringLiteral("tiktok.com"),
        QStringLiteral("x.com"),
        QStringLiteral("twitter.com")
    };
    for (const QString &entry : lowSignal) {
        if (host == entry || host.endsWith(QStringLiteral(".") + entry)) {
            return true;
        }
    }
    return false;
}

QJsonArray extractBraveSources(const QString &jsonText)
{
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
    if (!doc.isObject()) {
        return {};
    }

    const QJsonObject root = doc.object();
    const QJsonArray results = root.value(QStringLiteral("web")).toObject().value(QStringLiteral("results")).toArray();
    QJsonArray sources;
    for (const QJsonValue &value : results) {
        const QJsonObject row = value.toObject();
        const QString title = row.value(QStringLiteral("title")).toString().trimmed();
        const QString url = row.value(QStringLiteral("url")).toString().trimmed();
        const QString description = row.value(QStringLiteral("description")).toString().trimmed();
        if (!url.isEmpty()) {
            sources.push_back(QJsonObject{
                {QStringLiteral("title"), title},
                {QStringLiteral("url"), url},
                {QStringLiteral("snippet"), description}
            });
        }
    }
    return sources;
}

QJsonArray extractDuckSources(const QString &jsonText)
{
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
    if (!doc.isObject()) {
        return {};
    }

    const QJsonObject root = doc.object();
    QJsonArray sources;

    const QString abstractUrl = root.value(QStringLiteral("AbstractURL")).toString().trimmed();
    const QString abstractText = root.value(QStringLiteral("AbstractText")).toString().trimmed();
    const QString heading = root.value(QStringLiteral("Heading")).toString().trimmed();
    if (!abstractUrl.isEmpty()) {
        sources.push_back(QJsonObject{
            {QStringLiteral("title"), heading},
            {QStringLiteral("url"), abstractUrl},
            {QStringLiteral("snippet"), abstractText}
        });
    }

    const QJsonArray topics = root.value(QStringLiteral("RelatedTopics")).toArray();
    for (const QJsonValue &topicValue : topics) {
        const QJsonObject topic = topicValue.toObject();
        const QString firstUrl = topic.value(QStringLiteral("FirstURL")).toString().trimmed();
        const QString text = topic.value(QStringLiteral("Text")).toString().trimmed();
        if (!firstUrl.isEmpty()) {
            sources.push_back(QJsonObject{
                {QStringLiteral("title"), text.left(100)},
                {QStringLiteral("url"), firstUrl},
                {QStringLiteral("snippet"), text}
            });
        }
    }

    return sources;
}

QString compactSourcesContext(const QJsonArray &sources)
{
    QStringList lines;
    int index = 1;
    for (const QJsonValue &value : sources) {
        const QJsonObject source = value.toObject();
        const QString title = source.value(QStringLiteral("title")).toString().trimmed();
        const QString url = source.value(QStringLiteral("url")).toString().trimmed();
        const QString snippet = source.value(QStringLiteral("snippet")).toString().trimmed();
        if (url.isEmpty()) {
            continue;
        }
        lines << QStringLiteral("[%1] %2 | %3 | %4")
                     .arg(index++)
                     .arg(title.isEmpty() ? QStringLiteral("untitled") : title)
                     .arg(url)
                     .arg(snippet.left(260));
        if (lines.size() >= 8) {
            break;
        }
    }
    return lines.join(QStringLiteral("\n"));
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

    const QString content = readText(path, 64000);
    if (content.isEmpty()) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Log read failed"),
                           QStringLiteral("I couldn't read the log tail."),
                           QStringLiteral("Failed to tail: %1").arg(QDir::cleanPath(path)));
    }

    const QStringList lineList = content.split(QRegularExpression(QStringLiteral("\r?\n")), Qt::KeepEmptyParts);
    const qsizetype lineCount = lineList.size();
    const qsizetype requestedLines = std::max<qsizetype>(1, lines);
    const qsizetype startIndex = std::max<qsizetype>(0, lineCount - requestedLines);
    const QString output = lineList.mid(startIndex).join(QStringLiteral("\n"));

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

    QStringList matches;
    auto searchFile = [&matches, &query](const QString &filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return;
        }

        QTextStream stream(&file);
        int lineNumber = 0;
        while (!stream.atEnd() && matches.size() < 150) {
            const QString line = stream.readLine();
            ++lineNumber;
            if (line.contains(query, Qt::CaseInsensitive)) {
                matches.push_back(QStringLiteral("%1:%2: %3").arg(QDir::cleanPath(filePath)).arg(lineNumber).arg(line.trimmed()));
            }
        }
    };

    const QFileInfo info(path);
    if (info.isDir()) {
        QDirIterator it(path, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
        while (it.hasNext() && matches.size() < 150) {
            searchFile(it.next());
        }
    } else {
        searchFile(path);
    }

    if (matches.isEmpty()) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Log search failed"),
                           QStringLiteral("No matching log lines were found."),
                           QStringLiteral("No matches in %1 for %2").arg(QDir::cleanPath(path), query));
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
                           {QStringLiteral("content"), matches.join(QStringLiteral("\n"))}
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

    const QStringList variants = buildSearchVariants(query);
    QJsonArray mergedSources;
    QSet<QString> seenUrls;
    QString providerUsed = provider;
    QString detailSuffix;
    QString latestRawOutput;

    auto appendUniqueSources = [&mergedSources, &seenUrls](const QJsonArray &sources) {
        for (const QJsonValue &value : sources) {
            const QJsonObject source = value.toObject();
            const QString url = source.value(QStringLiteral("url")).toString().trimmed();
            if (url.isEmpty()) {
                continue;
            }
            const QString key = url.toLower();
            if (seenUrls.contains(key)) {
                continue;
            }
            seenUrls.insert(key);
            mergedSources.push_back(source);
        }
    };

    bool anyQuerySucceeded = false;
    for (const QString &variant : variants) {
        bool ok = false;
        QString output;

        if (provider == QStringLiteral("brave")) {
            QString apiKey = m_settings ? m_settings->braveSearchApiKey().trimmed() : QString();
            if (apiKey.isEmpty()) {
                apiKey = qEnvironmentVariable("BRAVE_SEARCH_API_KEY");
            }

            if (!apiKey.isEmpty()) {
                QString uri = QStringLiteral("https://api.search.brave.com/res/v1/web/search?q=%1&count=8")
                                  .arg(QString::fromUtf8(QUrl::toPercentEncoding(variant)));
                if (!freshness.isEmpty()) {
                    uri += QStringLiteral("&freshness=%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(freshness)));
                }

                const NetworkFetchResult fetch = httpGet(
                    QUrl(uri),
                    {
                        {QByteArray("Accept"), QByteArray("application/json")},
                        {QByteArray("X-Subscription-Token"), apiKey.toUtf8()}
                    },
                    20000);
                if (fetch.ok) {
                    output = QString::fromUtf8(fetch.body);
                    ok = true;
                    appendUniqueSources(extractBraveSources(output));
                }
            }

            if (!ok) {
                providerUsed = QStringLiteral("duckduckgo");
                detailSuffix = QStringLiteral(" Brave search was unavailable for at least one query variant; fell back to DuckDuckGo where needed.");
            }
        }

        if (!ok) {
            const QString duckUrl = QStringLiteral("https://api.duckduckgo.com/?q=%1&format=json&no_html=1&skip_disambig=1")
                .arg(QString::fromUtf8(QUrl::toPercentEncoding(variant)));
            const NetworkFetchResult fetch = httpGet(QUrl(duckUrl), {}, 20000);
            if (fetch.ok) {
                output = QString::fromUtf8(fetch.body);
                ok = true;
                appendUniqueSources(extractDuckSources(output));
            }
        }

        if (ok) {
            anyQuerySucceeded = true;
            latestRawOutput = output;
        }
    }

    if (!anyQuerySucceeded) {
        return buildResult(task,
                           false,
                           TaskState::Finished,
                           QStringLiteral("Web search failed"),
                           QStringLiteral("I couldn't search the web right now."),
                           QStringLiteral("Search query failed: %1").arg(query));
    }

    int authoritativeCount = 0;
    int lowSignalCount = 0;
    int neutralCount = 0;
    for (const QJsonValue &value : mergedSources) {
        const QString host = hostFromUrl(value.toObject().value(QStringLiteral("url")).toString());
        if (host.isEmpty()) {
            continue;
        }
        if (isAuthoritativeHost(host)) {
            authoritativeCount++;
        } else if (isLowSignalHost(host)) {
            lowSignalCount++;
        } else {
            neutralCount++;
        }
    }

    const int qualityScore = authoritativeCount * 3 + neutralCount - lowSignalCount;
    const bool reliable = mergedSources.size() >= 2 && qualityScore >= 3;
    const QString reliabilityReason = reliable
        ? QStringLiteral("Sufficient high-signal results found.")
        : QStringLiteral("Search results are low confidence (weak or low-authority source mix).");

    const QString compactContext = compactSourcesContext(mergedSources);
    QString output = compactContext;
    if (output.trimmed().isEmpty()) {
        output = latestRawOutput;
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
                       reliable ? QStringLiteral("Web search ready") : QStringLiteral("Web search low confidence"),
                       summary,
                       QStringLiteral("Web search completed for %1 using %2. variants=%3 results=%4 score=%5 reliable=%6.%7")
                           .arg(query,
                                providerUsed,
                                QString::number(variants.size()),
                                QString::number(mergedSources.size()),
                                QString::number(qualityScore),
                                reliable ? QStringLiteral("true") : QStringLiteral("false"),
                                detailSuffix),
                       QJsonObject{
                           {QStringLiteral("query"), query},
                           {QStringLiteral("query_rewritten"), simplifySearchQuery(query)},
                           {QStringLiteral("query_variants"), QJsonArray::fromStringList(variants)},
                           {QStringLiteral("provider"), providerUsed},
                           {QStringLiteral("freshness"), freshness},
                           {QStringLiteral("result_count"), static_cast<int>(mergedSources.size())},
                           {QStringLiteral("quality_score"), qualityScore},
                           {QStringLiteral("reliable"), reliable},
                           {QStringLiteral("reliability_reason"), reliabilityReason},
                           {QStringLiteral("sources"), mergedSources},
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
