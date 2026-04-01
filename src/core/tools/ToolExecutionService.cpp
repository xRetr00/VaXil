#include "core/tools/ToolExecutionService.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
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
#include "python/PythonRuntimeManager.h"
#include "settings/AppSettings.h"
#include "skills/SkillStore.h"

namespace {
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
    const QString simplified = simplifySearchQuery(query);
    const QString base = simplified.isEmpty() ? query.trimmed() : simplified;
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
    return QUrl(url).host().toLower();
}

bool shouldFallbackFromBrowserAutomation(const QString &summary, const QString &detail)
{
    const QString combined = (summary + QStringLiteral(" ") + detail).toLower();
    return combined.contains(QStringLiteral("playwright unavailable"))
        || combined.contains(QStringLiteral("browser binaries are missing"))
        || combined.contains(QStringLiteral("playwright is not installed"));
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

    const QJsonArray results = doc.object().value(QStringLiteral("web")).toObject().value(QStringLiteral("results")).toArray();
    QJsonArray sources;
    for (const QJsonValue &value : results) {
        const QJsonObject row = value.toObject();
        const QString url = row.value(QStringLiteral("url")).toString().trimmed();
        if (url.isEmpty()) {
            continue;
        }
        sources.push_back(QJsonObject{
            {QStringLiteral("title"), row.value(QStringLiteral("title")).toString().trimmed()},
            {QStringLiteral("url"), url},
            {QStringLiteral("snippet"), row.value(QStringLiteral("description")).toString().trimmed()}
        });
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
    if (!abstractUrl.isEmpty()) {
        sources.push_back(QJsonObject{
            {QStringLiteral("title"), root.value(QStringLiteral("Heading")).toString().trimmed()},
            {QStringLiteral("url"), abstractUrl},
            {QStringLiteral("snippet"), root.value(QStringLiteral("AbstractText")).toString().trimmed()}
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
        const QString url = source.value(QStringLiteral("url")).toString().trimmed();
        if (url.isEmpty()) {
            continue;
        }
        lines.push_back(QStringLiteral("[%1] %2 | %3 | %4")
                            .arg(index++)
                            .arg(source.value(QStringLiteral("title")).toString().trimmed().isEmpty()
                                     ? QStringLiteral("untitled")
                                     : source.value(QStringLiteral("title")).toString().trimmed())
                            .arg(url)
                            .arg(source.value(QStringLiteral("snippet")).toString().trimmed().left(260)));
        if (lines.size() >= 8) {
            break;
        }
    }
    return lines.join(QStringLiteral("\n"));
}
}

ToolExecutionService::ToolExecutionService(const QStringList &allowedRoots,
                                           AppSettings *settings,
                                           SkillStore *skillStore,
                                           LoggingService *loggingService,
                                           QObject *parent)
    : QObject(parent)
    , m_allowedRoots(allowedRoots)
    , m_settings(settings)
    , m_skillStore(skillStore)
    , m_loggingService(loggingService)
    , m_pythonRuntime(new PythonRuntimeManager(allowedRoots, loggingService, this))
    , m_memoryManager(std::make_unique<MemoryManager>())
{
}

ToolExecutionResult ToolExecutionService::execute(const ToolExecutionRequest &request)
{
    if (request.toolName.trimmed().isEmpty()) {
        return failureResult(request,
                             ToolErrorKind::Invalid,
                             QStringLiteral("Invalid tool request"),
                             QStringLiteral("Tool name is required."));
    }

    if (m_pythonRuntime) {
        QString runtimeError;
        if (m_pythonRuntime->supportsAction(request.toolName, &runtimeError)) {
            QJsonObject context;
            context.insert(QStringLiteral("braveSearchApiKey"), m_settings ? m_settings->braveSearchApiKey() : QString{});
            const QJsonObject response = m_pythonRuntime->executeAction(request.toolName, request.args, context, &runtimeError);
            if (!response.isEmpty()) {
                const bool ok = response.value(QStringLiteral("ok")).toBool();
                const QString summary = response.value(QStringLiteral("summary")).toString(request.toolName);
                const QString detail = response.value(QStringLiteral("detail")).toString();
                const QJsonObject payload = response.value(QStringLiteral("payload")).toObject();
                if (!ok
                    && request.toolName == QStringLiteral("browser_open")
                    && shouldFallbackFromBrowserAutomation(summary, detail)) {
                    ToolExecutionRequest fallbackRequest = request;
                    fallbackRequest.toolName = QStringLiteral("computer_open_url");
                    return executeComputerOpenUrl(fallbackRequest);
                }

                return ok
                    ? successResult(request, summary, detail, payload)
                    : failureResult(request,
                                    classifyErrorKind(detail),
                                    summary.isEmpty() ? QStringLiteral("Tool failed") : summary,
                                    detail,
                                    payload,
                                    runtimeError);
            }

            if (!runtimeError.trimmed().isEmpty()) {
                return failureResult(request,
                                     classifyErrorKind(runtimeError),
                                     QStringLiteral("Tool runtime failed"),
                                     runtimeError,
                                     {},
                                     runtimeError);
            }
        }
    }

    return executeBuiltIn(request);
}

AgentToolResult ToolExecutionService::executeCall(const AgentToolCall &call)
{
    const QJsonDocument argsDoc = QJsonDocument::fromJson(call.argumentsJson.toUtf8());
    ToolExecutionRequest request{
        .toolName = call.name,
        .callId = call.id,
        .args = argsDoc.isObject() ? argsDoc.object() : QJsonObject{}
    };

    if (call.name != QStringLiteral("skill_list") && !call.argumentsJson.trimmed().isEmpty() && !argsDoc.isObject()) {
        AgentToolResult invalid;
        invalid.callId = call.id;
        invalid.toolName = call.name;
        invalid.success = false;
        invalid.errorKind = ToolErrorKind::Invalid;
        invalid.summary = QStringLiteral("Tool arguments invalid");
        invalid.detail = QStringLiteral("Tool arguments were not valid JSON.");
        invalid.output = invalid.detail;
        return invalid;
    }

    const ToolExecutionResult result = execute(request);
    AgentToolResult toolResult;
    toolResult.callId = result.callId;
    toolResult.toolName = result.toolName;
    toolResult.output = outputTextForModel(result);
    toolResult.success = result.success;
    toolResult.errorKind = result.errorKind;
    toolResult.summary = result.summary;
    toolResult.detail = result.detail;
    toolResult.payload = result.payload;
    toolResult.rawProviderError = result.rawProviderError;
    return toolResult;
}

QString ToolExecutionService::outputTextForModel(const ToolExecutionResult &result)
{
    if (result.payload.contains(QStringLiteral("text"))) {
        return result.payload.value(QStringLiteral("text")).toString();
    }
    if (result.payload.contains(QStringLiteral("content"))) {
        return result.payload.value(QStringLiteral("content")).toString();
    }
    if (result.payload.contains(QStringLiteral("code"))) {
        return result.payload.value(QStringLiteral("code")).toString();
    }
    if (result.payload.contains(QStringLiteral("matches"))) {
        QStringList lines;
        for (const QJsonValue &value : result.payload.value(QStringLiteral("matches")).toArray()) {
            lines.push_back(value.toString());
        }
        return lines.join(QStringLiteral("\n"));
    }
    if (result.payload.contains(QStringLiteral("entries"))) {
        QStringList lines;
        for (const QJsonValue &value : result.payload.value(QStringLiteral("entries")).toArray()) {
            const QJsonObject entry = value.toObject();
            lines.push_back(QStringLiteral("%1\t%2")
                                .arg(entry.value(QStringLiteral("type")).toString(),
                                     entry.value(QStringLiteral("name")).toString()));
        }
        return lines.join(QStringLiteral("\n"));
    }
    if (!result.detail.trimmed().isEmpty()) {
        return result.detail;
    }
    return result.summary;
}

QJsonObject ToolExecutionService::taskResultObject(const AgentTask &task,
                                                   const ToolExecutionResult &result,
                                                   TaskState state)
{
    return QJsonObject{
        {QStringLiteral("taskId"), task.id},
        {QStringLiteral("type"), task.type},
        {QStringLiteral("success"), result.success},
        {QStringLiteral("state"), static_cast<int>(state)},
        {QStringLiteral("errorKind"), static_cast<int>(result.errorKind)},
        {QStringLiteral("title"), result.summary},
        {QStringLiteral("summary"), result.summary},
        {QStringLiteral("detail"), result.detail},
        {QStringLiteral("payload"), result.payload},
        {QStringLiteral("finishedAt"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
        {QStringLiteral("taskKey"), task.taskKey}
    };
}

bool ToolExecutionService::isReadablePath(const QString &path) const
{
    QFileInfo info(path);
    if (!info.exists()) {
        return false;
    }
    return info.isDir() ? QDir(info.absoluteFilePath()).exists() : (info.isFile() && info.isReadable());
}

bool ToolExecutionService::isWritablePath(const QString &path) const
{
    const QString resolved = normalizePath(path);
    for (const QString &root : m_allowedRoots) {
        if (!root.isEmpty() && resolved.startsWith(root, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QString ToolExecutionService::normalizePath(const QString &path) const
{
    const QFileInfo info(path);
    if (info.exists()) {
        return info.canonicalFilePath();
    }
    return QDir::cleanPath(info.absoluteFilePath());
}

ToolErrorKind ToolExecutionService::classifyErrorKind(const QString &message, int statusCode) const
{
    const QString lowered = message.toLower();
    if (lowered.contains(QStringLiteral("timed out"))) {
        return ToolErrorKind::Timeout;
    }
    if (statusCode == 401 || statusCode == 403
        || lowered.contains(QStringLiteral("unauthorized"))
        || lowered.contains(QStringLiteral("forbidden"))
        || lowered.contains(QStringLiteral("subscription-token"))) {
        return ToolErrorKind::Auth;
    }
    if (lowered.contains(QStringLiteral("invalid"))
        || lowered.contains(QStringLiteral("required"))
        || lowered.contains(QStringLiteral("not valid json"))
        || lowered.contains(QStringLiteral("missing"))) {
        return ToolErrorKind::Invalid;
    }
    if (lowered.contains(QStringLiteral("unsupported"))
        || lowered.contains(QStringLiteral("unknown tool"))
        || lowered.contains(QStringLiteral("unavailable"))) {
        return ToolErrorKind::Capability;
    }
    if (lowered.contains(QStringLiteral("network"))
        || lowered.contains(QStringLiteral("connection"))
        || lowered.contains(QStringLiteral("host"))
        || lowered.contains(QStringLiteral("http"))) {
        return ToolErrorKind::Transport;
    }
    return ToolErrorKind::Unknown;
}

ToolExecutionResult ToolExecutionService::executeBuiltIn(const ToolExecutionRequest &request)
{
    if (request.toolName == QStringLiteral("file_read")) return executeFileRead(request);
    if (request.toolName == QStringLiteral("file_search")) return executeFileSearch(request);
    if (request.toolName == QStringLiteral("file_write")) return executeFileWrite(request);
    if (request.toolName == QStringLiteral("file_patch")) return executeFilePatch(request);
    if (request.toolName == QStringLiteral("dir_list")) return executeDirList(request);
    if (request.toolName == QStringLiteral("memory_search")) return executeMemorySearch(request);
    if (request.toolName == QStringLiteral("memory_write")) return executeMemoryWrite(request);
    if (request.toolName == QStringLiteral("memory_delete")) return executeMemoryDelete(request);
    if (request.toolName == QStringLiteral("log_tail")) return executeLogTail(request);
    if (request.toolName == QStringLiteral("log_search")) return executeLogSearch(request);
    if (request.toolName == QStringLiteral("ai_log_read")) return executeAiLogRead(request);
    if (request.toolName == QStringLiteral("web_search")) return executeWebSearch(request);
    if (request.toolName == QStringLiteral("web_fetch")) return executeWebFetch(request);
    if (request.toolName == QStringLiteral("computer_list_apps")) return executeComputerListApps(request);
    if (request.toolName == QStringLiteral("computer_open_app")) return executeComputerOpenApp(request);
    if (request.toolName == QStringLiteral("computer_open_url")) return executeComputerOpenUrl(request);
    if (request.toolName == QStringLiteral("computer_write_file")) return executeComputerWriteFile(request);
    if (request.toolName == QStringLiteral("computer_set_timer")) return executeComputerSetTimer(request);
    if (request.toolName == QStringLiteral("skill_list")) return executeSkillList(request);
    if (request.toolName == QStringLiteral("skill_install")) return executeSkillInstall(request);
    if (request.toolName == QStringLiteral("skill_create")) return executeSkillCreate(request);

    return failureResult(request,
                         ToolErrorKind::Capability,
                         QStringLiteral("Unsupported task"),
                         QStringLiteral("Unsupported tool type: %1").arg(request.toolName));
}

ToolExecutionResult ToolExecutionService::executeFileRead(const ToolExecutionRequest &request)
{
    const QString path = request.args.value(QStringLiteral("path")).toString();
    if (!isReadablePath(path)) {
        return failureResult(request, ToolErrorKind::Capability, QStringLiteral("Read failed"), QStringLiteral("Read path is not readable."));
    }

    const QString text = readText(path, 18000);
    return text.isEmpty()
        ? failureResult(request, ToolErrorKind::Transport, QStringLiteral("Read failed"), QStringLiteral("Failed to read file."))
        : successResult(request,
                        QStringLiteral("Read file"),
                        QStringLiteral("Read file: %1").arg(QDir::cleanPath(path)),
                        QJsonObject{{QStringLiteral("path"), QDir::cleanPath(path)}, {QStringLiteral("text"), text}});
}

ToolExecutionResult ToolExecutionService::executeFileSearch(const ToolExecutionRequest &request)
{
    const QString root = request.args.value(QStringLiteral("root")).toString();
    const QString query = request.args.value(QStringLiteral("query")).toString();
    if (!isReadablePath(root)) {
        return failureResult(request, ToolErrorKind::Capability, QStringLiteral("Search failed"), QStringLiteral("Search root is not readable."));
    }

    QStringList matches;
    QDirIterator it(root, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext() && matches.size() < 25) {
        const QString filePath = it.next();
        const QString text = readText(filePath, 20000);
        if (text.contains(query, Qt::CaseInsensitive)) {
            matches.push_back(QDir::cleanPath(filePath));
        }
    }

    return successResult(request,
                         QStringLiteral("Search completed"),
                         QStringLiteral("Searched %1 for %2").arg(QDir::cleanPath(root), query),
                         QJsonObject{{QStringLiteral("matches"), QJsonArray::fromStringList(matches)}});
}

ToolExecutionResult ToolExecutionService::executeFileWrite(const ToolExecutionRequest &request)
{
    const QString path = request.args.value(QStringLiteral("path")).toString();
    const QString content = request.args.value(QStringLiteral("content")).toString();
    if (!isWritablePath(path)) {
        return failureResult(request, ToolErrorKind::Capability, QStringLiteral("Write failed"), QStringLiteral("Write path is outside allowed roots."));
    }

    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return failureResult(request, ToolErrorKind::Transport, QStringLiteral("Write failed"), QStringLiteral("Failed to open file for writing."));
    }
    file.write(content.toUtf8());

    return successResult(request,
                         QStringLiteral("File written"),
                         QStringLiteral("Wrote %1 bytes to %2").arg(content.toUtf8().size()).arg(QDir::cleanPath(path)),
                         QJsonObject{{QStringLiteral("path"), QDir::cleanPath(path)}});
}

ToolExecutionResult ToolExecutionService::executeFilePatch(const ToolExecutionRequest &request)
{
    const QString path = request.args.value(QStringLiteral("path")).toString();
    const QString find = request.args.value(QStringLiteral("find")).toString();
    const QString replace = request.args.value(QStringLiteral("replace")).toString();
    if (!isWritablePath(path)) {
        return failureResult(request, ToolErrorKind::Capability, QStringLiteral("Patch failed"), QStringLiteral("Patch path is outside allowed roots."));
    }

    QString text = readText(path, 500000);
    if (text.isEmpty()) {
        return failureResult(request, ToolErrorKind::Transport, QStringLiteral("Patch failed"), QStringLiteral("Failed to read file for patching."));
    }
    if (!text.contains(find)) {
        return failureResult(request, ToolErrorKind::Invalid, QStringLiteral("Patch failed"), QStringLiteral("Patch target text was not found."));
    }

    text.replace(find, replace);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return failureResult(request, ToolErrorKind::Transport, QStringLiteral("Patch failed"), QStringLiteral("Failed to write patched file."));
    }
    file.write(text.toUtf8());

    return successResult(request,
                         QStringLiteral("File patched"),
                         QStringLiteral("Patched %1").arg(QDir::cleanPath(path)),
                         QJsonObject{{QStringLiteral("path"), QDir::cleanPath(path)}});
}

ToolExecutionResult ToolExecutionService::executeDirList(const ToolExecutionRequest &request)
{
    const QString path = request.args.value(QStringLiteral("path")).toString();
    if (!isReadablePath(path)) {
        return failureResult(request, ToolErrorKind::Capability, QStringLiteral("Directory unreadable"), QStringLiteral("Directory is not readable."));
    }

    const QFileInfoList entries = QDir(path).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    QJsonArray rows;
    for (const QFileInfo &entry : entries.mid(0, 100)) {
        rows.push_back(QJsonObject{
            {QStringLiteral("type"), entry.isDir() ? QStringLiteral("dir") : QStringLiteral("file")},
            {QStringLiteral("name"), entry.fileName()}
        });
    }

    return successResult(request,
                         QStringLiteral("Directory listed"),
                         QStringLiteral("Listed %1").arg(QDir::cleanPath(path)),
                         QJsonObject{{QStringLiteral("entries"), rows}, {QStringLiteral("path"), QDir::cleanPath(path)}});
}

ToolExecutionResult ToolExecutionService::executeMemorySearch(const ToolExecutionRequest &request)
{
    const QString query = request.args.value(QStringLiteral("query")).toString();
    QJsonArray matches;
    for (const MemoryEntry &entry : m_memoryManager->search(query, 12)) {
        matches.push_back(QJsonObject{
            {QStringLiteral("kind"), entry.kind},
            {QStringLiteral("title"), entry.title},
            {QStringLiteral("content"), entry.content},
            {QStringLiteral("key"), entry.key},
            {QStringLiteral("value"), entry.value}
        });
    }

    return successResult(request,
                         QStringLiteral("Memory search complete"),
                         QStringLiteral("Found %1 memory entries.").arg(matches.size()),
                         QJsonObject{{QStringLiteral("matches"), matches}});
}

ToolExecutionResult ToolExecutionService::executeMemoryWrite(const ToolExecutionRequest &request)
{
    MemoryEntry entry;
    entry.kind = request.args.value(QStringLiteral("kind")).toString(QStringLiteral("fact"));
    entry.title = request.args.value(QStringLiteral("title")).toString(request.args.value(QStringLiteral("key")).toString(QStringLiteral("general_fact")));
    entry.content = request.args.value(QStringLiteral("content")).toString(request.args.value(QStringLiteral("value")).toString());
    entry.key = request.args.value(QStringLiteral("key")).toString(entry.title);
    entry.value = request.args.value(QStringLiteral("value")).toString(entry.content);
    entry.source = QStringLiteral("tool_execution");
    entry.createdAt = QDateTime::currentDateTimeUtc();

    const bool ok = m_memoryManager->write(entry);
    if (!ok) {
        return failureResult(request,
                             ToolErrorKind::Invalid,
                             QStringLiteral("Memory write rejected"),
                             QStringLiteral("The entry was empty, expired, or looked like a secret."));
    }

    return successResult(request,
                         QStringLiteral("Memory saved"),
                         QStringLiteral("Stored %1 memory entry.").arg(entry.kind),
                         QJsonObject{
                             {QStringLiteral("key"), entry.key},
                             {QStringLiteral("value"), entry.value},
                             {QStringLiteral("kind"), entry.kind}
                         });
}

ToolExecutionResult ToolExecutionService::executeMemoryDelete(const ToolExecutionRequest &request)
{
    const QString id = request.args.value(QStringLiteral("id")).toString();
    const bool ok = m_memoryManager->remove(id);
    return ok
        ? successResult(request, QStringLiteral("Memory deleted"), QStringLiteral("Deleted memory entry %1").arg(id))
        : failureResult(request, ToolErrorKind::Invalid, QStringLiteral("Memory entry not found"), QStringLiteral("Memory entry not found."));
}

ToolExecutionResult ToolExecutionService::executeLogTail(const ToolExecutionRequest &request)
{
    const QString path = request.args.value(QStringLiteral("path")).toString();
    const int lines = request.args.value(QStringLiteral("lines")).toInt(120);
    if (!isReadablePath(path)) {
        return failureResult(request, ToolErrorKind::Capability, QStringLiteral("Log tail failed"), QStringLiteral("Log path is not readable."));
    }

    const QString content = readText(path, 64000);
    if (content.isEmpty()) {
        return failureResult(request, ToolErrorKind::Transport, QStringLiteral("Log tail failed"), QStringLiteral("Failed to read the log tail."));
    }

    const QStringList lineList = content.split(QRegularExpression(QStringLiteral("\r?\n")), Qt::KeepEmptyParts);
    const qsizetype requestedLines = std::max<qsizetype>(1, lines);
    const qsizetype startIndex = std::max<qsizetype>(0, lineList.size() - requestedLines);
    const QString text = lineList.mid(startIndex).join(QStringLiteral("\n"));
    return successResult(request,
                         QStringLiteral("Log tail ready"),
                         QStringLiteral("Read log tail from %1").arg(QDir::cleanPath(path)),
                         QJsonObject{{QStringLiteral("path"), QDir::cleanPath(path)}, {QStringLiteral("text"), text}});
}

ToolExecutionResult ToolExecutionService::executeLogSearch(const ToolExecutionRequest &request)
{
    const QString path = request.args.value(QStringLiteral("path")).toString();
    const QString query = request.args.value(QStringLiteral("query")).toString();
    if (!isReadablePath(path)) {
        return failureResult(request, ToolErrorKind::Capability, QStringLiteral("Log search failed"), QStringLiteral("Log search path is not readable."));
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

    return successResult(request,
                         QStringLiteral("Log search complete"),
                         matches.isEmpty()
                             ? QStringLiteral("No matches in %1 for %2").arg(QDir::cleanPath(path), query)
                             : QStringLiteral("Search completed in %1").arg(QDir::cleanPath(path)),
                         QJsonObject{{QStringLiteral("matches"), QJsonArray::fromStringList(matches)}});
}

ToolExecutionResult ToolExecutionService::executeAiLogRead(const ToolExecutionRequest &request)
{
    QString path = request.args.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        path = latestAiLogPath();
    }
    if (path.isEmpty()) {
        return failureResult(request, ToolErrorKind::Capability, QStringLiteral("No AI logs found"), QStringLiteral("No AI logs found."));
    }
    if (!isReadablePath(path)) {
        return failureResult(request, ToolErrorKind::Capability, QStringLiteral("AI log unreadable"), QStringLiteral("AI log path is not readable."));
    }

    const QString text = readText(path, 18000);
    return text.isEmpty()
        ? failureResult(request, ToolErrorKind::Transport, QStringLiteral("AI log read failed"), QStringLiteral("Failed to read AI log."))
        : successResult(request,
                        QStringLiteral("AI log ready"),
                        QStringLiteral("Read AI log: %1").arg(QDir::cleanPath(path)),
                        QJsonObject{{QStringLiteral("path"), QDir::cleanPath(path)}, {QStringLiteral("text"), text}});
}

ToolExecutionResult ToolExecutionService::executeWebSearch(const ToolExecutionRequest &request)
{
    const QString query = request.args.value(QStringLiteral("query")).toString().trimmed();
    if (query.isEmpty()) {
        return failureResult(request, ToolErrorKind::Invalid, QStringLiteral("Web search failed"), QStringLiteral("A search query is required."));
    }

    QString provider = m_settings ? m_settings->webSearchProvider().trimmed().toLower() : QStringLiteral("duckduckgo");
    if (provider.isEmpty()) {
        provider = QStringLiteral("duckduckgo");
    }

    QString freshness = request.args.value(QStringLiteral("freshness")).toString().trimmed().toLower();
    if (freshness.isEmpty() && request.args.value(QStringLiteral("prefer_fresh")).toBool(false)) {
        freshness = QStringLiteral("pw");
    }
    if (!freshness.isEmpty()) {
        const QSet<QString> allowedFreshness{QStringLiteral("pd"), QStringLiteral("pw"), QStringLiteral("pm"), QStringLiteral("py")};
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
    QString lastError;

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
            QString apiKey = m_settings ? m_settings->braveSearchApiKey().trimmed() : QString{};
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
                } else {
                    lastError = fetch.error;
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
            } else {
                lastError = fetch.error;
            }
        }

        if (ok) {
            anyQuerySucceeded = true;
            latestRawOutput = output;
        }
    }

    if (!anyQuerySucceeded) {
        return failureResult(request,
                             classifyErrorKind(lastError),
                             QStringLiteral("Web search failed"),
                             QStringLiteral("I couldn't search the web right now."),
                             {},
                             lastError);
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

    QString output = compactSourcesContext(mergedSources);
    if (output.trimmed().isEmpty()) {
        output = latestRawOutput;
    }

    QString summary = QStringLiteral("Searched the web for \"%1\"").arg(query);
    if (providerUsed == QStringLiteral("brave")) {
        summary += QStringLiteral(" via Brave");
    } else if (provider == QStringLiteral("brave")) {
        summary += QStringLiteral(" via DuckDuckGo fallback");
    }

    return successResult(
        request,
        reliable ? QStringLiteral("Web search ready") : QStringLiteral("Web search low confidence"),
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
            {QStringLiteral("content"), output},
            {QStringLiteral("text"), output},
            {QStringLiteral("summary"), summary}
        });
}

ToolExecutionResult ToolExecutionService::executeWebFetch(const ToolExecutionRequest &request)
{
    const QString url = request.args.value(QStringLiteral("url")).toString();
    const NetworkFetchResult fetch = httpGet(QUrl(url), {}, 30000);
    if (!fetch.ok) {
        return failureResult(request, classifyErrorKind(fetch.error, fetch.statusCode), QStringLiteral("Web fetch failed"), QStringLiteral("Web fetch failed."), {}, fetch.error);
    }

    QString output = QString::fromUtf8(fetch.body);
    if (output.size() > 12000) {
        output = output.left(12000) + QStringLiteral("\n...[truncated]");
    }
    return successResult(request,
                         QStringLiteral("Web page fetched"),
                         QStringLiteral("Fetched %1").arg(url),
                         QJsonObject{{QStringLiteral("url"), url}, {QStringLiteral("content"), output}, {QStringLiteral("text"), output}});
}

ToolExecutionResult ToolExecutionService::executeComputerListApps(const ToolExecutionRequest &request)
{
    const QString query = request.args.value(QStringLiteral("query")).toString();
    const int limit = request.args.value(QStringLiteral("limit")).toInt(20);
    const auto result = ComputerControl::listApps(query, limit);
    return result.success
        ? successResult(request,
                        QStringLiteral("Apps listed"),
                        result.detail,
                        QJsonObject{{QStringLiteral("items"), QJsonArray::fromStringList(result.lines)}})
        : failureResult(request,
                        ToolErrorKind::Capability,
                        QStringLiteral("App listing failed"),
                        result.detail,
                        QJsonObject{{QStringLiteral("items"), QJsonArray::fromStringList(result.lines)}});
}

ToolExecutionResult ToolExecutionService::executeComputerOpenApp(const ToolExecutionRequest &request)
{
    QStringList arguments;
    for (const QJsonValue &argument : request.args.value(QStringLiteral("arguments")).toArray()) {
        arguments.push_back(argument.toString());
    }

    const auto result = ComputerControl::launchApp(request.args.value(QStringLiteral("target")).toString(), arguments);
    return result.success
        ? successResult(request,
                        QStringLiteral("App opened"),
                        result.detail,
                        QJsonObject{{QStringLiteral("matches"), QJsonArray::fromStringList(result.lines)}})
        : failureResult(request,
                        ToolErrorKind::Capability,
                        QStringLiteral("App open failed"),
                        result.detail,
                        QJsonObject{{QStringLiteral("matches"), QJsonArray::fromStringList(result.lines)}});
}

ToolExecutionResult ToolExecutionService::executeComputerOpenUrl(const ToolExecutionRequest &request)
{
    const QString url = request.args.value(QStringLiteral("url")).toString();
    const auto result = ComputerControl::openUrl(url);
    return result.success
        ? successResult(request,
                        QStringLiteral("Link opened"),
                        result.detail,
                        QJsonObject{{QStringLiteral("url"), url}})
        : failureResult(request,
                        ToolErrorKind::Capability,
                        QStringLiteral("Open link failed"),
                        result.detail,
                        QJsonObject{{QStringLiteral("url"), url}});
}

ToolExecutionResult ToolExecutionService::executeComputerWriteFile(const ToolExecutionRequest &request)
{
    const QString path = request.args.value(QStringLiteral("path")).toString();
    const QString content = request.args.value(QStringLiteral("content")).toString();
    const bool overwrite = request.args.value(QStringLiteral("overwrite")).toBool(false);
    const QString baseDir = request.args.value(QStringLiteral("base_dir")).toString();
    const auto result = ComputerControl::writeTextFile(path, content, overwrite, baseDir);

    return result.success
        ? successResult(request,
                        QStringLiteral("File written"),
                        result.detail,
                        QJsonObject{{QStringLiteral("path"), path}, {QStringLiteral("base_dir"), baseDir}})
        : failureResult(request,
                        ToolErrorKind::Capability,
                        QStringLiteral("Computer file write failed"),
                        result.detail,
                        QJsonObject{{QStringLiteral("path"), path}, {QStringLiteral("base_dir"), baseDir}});
}

ToolExecutionResult ToolExecutionService::executeComputerSetTimer(const ToolExecutionRequest &request)
{
    const int durationSeconds = request.args.value(QStringLiteral("duration_seconds")).toInt(0);
    const QString title = request.args.value(QStringLiteral("title")).toString();
    const QString message = request.args.value(QStringLiteral("message")).toString();
    const auto result = ComputerControl::setTimer(durationSeconds, title, message);
    return result.success
        ? successResult(request,
                        QStringLiteral("Timer set"),
                        result.detail,
                        QJsonObject{
                            {QStringLiteral("duration_seconds"), durationSeconds},
                            {QStringLiteral("title"), title},
                            {QStringLiteral("message"), message}
                        })
        : failureResult(request,
                        ToolErrorKind::Capability,
                        QStringLiteral("Timer setup failed"),
                        result.detail,
                        QJsonObject{
                            {QStringLiteral("duration_seconds"), durationSeconds},
                            {QStringLiteral("title"), title},
                            {QStringLiteral("message"), message}
                        });
}

ToolExecutionResult ToolExecutionService::executeSkillList(const ToolExecutionRequest &request)
{
    if (!m_skillStore) {
        return failureResult(request, ToolErrorKind::Capability, QStringLiteral("Skills unavailable"), QStringLiteral("Skill store is unavailable in this execution context."));
    }

    QJsonArray skills;
    for (const SkillManifest &skill : m_skillStore->listSkills()) {
        skills.push_back(QJsonObject{
            {QStringLiteral("id"), skill.id},
            {QStringLiteral("name"), skill.name},
            {QStringLiteral("description"), skill.description}
        });
    }
    return successResult(request, QStringLiteral("Skills listed"), QStringLiteral("Listed installed skills."), QJsonObject{{QStringLiteral("entries"), skills}});
}

ToolExecutionResult ToolExecutionService::executeSkillInstall(const ToolExecutionRequest &request)
{
    if (!m_skillStore) {
        return failureResult(request, ToolErrorKind::Capability, QStringLiteral("Skill install unavailable"), QStringLiteral("Skill store is unavailable in this execution context."));
    }

    QString error;
    const bool ok = m_skillStore->installSkill(request.args.value(QStringLiteral("url")).toString(), &error);
    return ok
        ? successResult(request, QStringLiteral("Skill installed"), QStringLiteral("Skill installed."))
        : failureResult(request, classifyErrorKind(error), QStringLiteral("Skill install failed"), error);
}

ToolExecutionResult ToolExecutionService::executeSkillCreate(const ToolExecutionRequest &request)
{
    if (!m_skillStore) {
        return failureResult(request, ToolErrorKind::Capability, QStringLiteral("Skill creation unavailable"), QStringLiteral("Skill store is unavailable in this execution context."));
    }

    QString error;
    const bool ok = m_skillStore->createSkill(request.args.value(QStringLiteral("id")).toString(),
                                              request.args.value(QStringLiteral("name")).toString(),
                                              request.args.value(QStringLiteral("description")).toString(),
                                              &error);
    return ok
        ? successResult(request, QStringLiteral("Skill created"), QStringLiteral("Skill created."))
        : failureResult(request, classifyErrorKind(error), QStringLiteral("Skill creation failed"), error);
}

ToolExecutionResult ToolExecutionService::successResult(const ToolExecutionRequest &request,
                                                        const QString &summary,
                                                        const QString &detail,
                                                        const QJsonObject &payload) const
{
    ToolExecutionResult result;
    result.toolName = request.toolName;
    result.callId = request.callId;
    result.success = true;
    result.errorKind = ToolErrorKind::None;
    result.summary = summary;
    result.detail = detail;
    result.payload = payload;
    return result;
}

ToolExecutionResult ToolExecutionService::failureResult(const ToolExecutionRequest &request,
                                                        ToolErrorKind errorKind,
                                                        const QString &summary,
                                                        const QString &detail,
                                                        const QJsonObject &payload,
                                                        const QString &rawProviderError) const
{
    ToolExecutionResult result;
    result.toolName = request.toolName;
    result.callId = request.callId;
    result.success = false;
    result.errorKind = errorKind;
    result.summary = summary;
    result.detail = detail;
    result.payload = payload;
    result.rawProviderError = rawProviderError;
    return result;
}
