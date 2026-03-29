#include "agent/AgentToolbox.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include "core/ComputerControl.h"
#include "logging/LoggingService.h"
#include "memory/MemoryStore.h"
#include "platform/PlatformRuntime.h"
#include "settings/AppSettings.h"
#include "skills/SkillStore.h"

namespace {
struct NetworkFetchResult
{
    bool ok = false;
    QString error;
    QByteArray body;
};

QString canonicalPath(const QString &path)
{
    QFileInfo info(path);
    if (info.exists()) {
        return info.canonicalFilePath();
    }
    return QDir::cleanPath(info.absoluteFilePath());
}

QString readTextFile(const QString &path, int maxChars = 12000)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QString text = QString::fromUtf8(file.readAll());
    if (text.size() > maxChars) {
        text = text.left(maxChars) + QStringLiteral("\n...[truncated]");
    }
    return text;
}

nlohmann::json schemaObject(const std::initializer_list<std::pair<const char *, nlohmann::json>> &properties,
                            const std::vector<std::string> &required = {})
{
    nlohmann::json schema = {
        {"type", "object"},
        {"properties", nlohmann::json::object()}
    };
    for (const auto &property : properties) {
        schema["properties"][property.first] = property.second;
    }
    if (!required.empty()) {
        schema["required"] = required;
    }
    return schema;
}

QString extractString(const nlohmann::json &args, const char *key)
{
    return args.contains(key) && args.at(key).is_string()
        ? QString::fromStdString(args.at(key).get<std::string>())
        : QString{};
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

    result.body = reply->readAll();
    result.ok = reply->error() == QNetworkReply::NoError;
    if (!result.ok) {
        result.error = reply->errorString();
    }
    reply->deleteLater();
    return result;
}
}

AgentToolbox::AgentToolbox(AppSettings *settings, MemoryStore *memoryStore, SkillStore *skillStore, LoggingService *loggingService, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_memoryStore(memoryStore)
    , m_skillStore(skillStore)
    , m_loggingService(loggingService)
{
}

QList<AgentToolSpec> AgentToolbox::builtInTools() const
{
    QList<AgentToolSpec> tools = {
        {QStringLiteral("file_read"), QStringLiteral("Read a UTF-8 text file from a readable path."),
         schemaObject({{"path", {{"type", "string"}}}}, {"path"})},
        {QStringLiteral("file_search"), QStringLiteral("Search for text under a readable directory."),
         schemaObject({{"root", {{"type", "string"}}}, {"query", {{"type", "string"}}}}, {"root", "query"})},
        {QStringLiteral("file_write"), QStringLiteral("Write a UTF-8 text file to a writable sandbox path."),
         schemaObject({{"path", {{"type", "string"}}}, {"content", {{"type", "string"}}}}, {"path", "content"})},
        {QStringLiteral("file_patch"), QStringLiteral("Patch a writable sandbox file by replacing one text fragment with another."),
         schemaObject({{"path", {{"type", "string"}}}, {"find", {{"type", "string"}}}, {"replace", {{"type", "string"}}}}, {"path", "find", "replace"})},
        {QStringLiteral("dir_list"), QStringLiteral("List files under a readable directory."),
         schemaObject({{"path", {{"type", "string"}}}}, {"path"})},
        {QStringLiteral("memory_search"), QStringLiteral("Search structured memory entries."),
         schemaObject({{"query", {{"type", "string"}}}}, {"query"})},
        {QStringLiteral("memory_write"), QStringLiteral("Write or update a structured memory entry."),
         schemaObject({{"kind", {{"type", "string"}}}, {"title", {{"type", "string"}}}, {"content", {{"type", "string"}}}}, {"kind", "title", "content"})},
        {QStringLiteral("memory_delete"), QStringLiteral("Delete a memory entry by id or title."),
         schemaObject({{"id", {{"type", "string"}}}}, {"id"})},
        {QStringLiteral("log_tail"), QStringLiteral("Read the tail of a readable log file."),
         schemaObject({{"path", {{"type", "string"}}}, {"lines", {{"type", "integer"}}}}, {"path"})},
        {QStringLiteral("log_search"), QStringLiteral("Search a readable log directory or log file for a pattern."),
         schemaObject({{"path", {{"type", "string"}}}, {"query", {{"type", "string"}}}}, {"path", "query"})},
        {QStringLiteral("ai_log_read"), QStringLiteral("Read the latest AI exchange log or a specific readable AI log file."),
         schemaObject({{"path", {{"type", "string"}}}}, {})},
        {QStringLiteral("web_search"), QStringLiteral("Search the web using the configured provider."),
         schemaObject({{"query", {{"type", "string"}}}}, {"query"})},
        {QStringLiteral("web_fetch"), QStringLiteral("Fetch the contents of a public URL."),
         schemaObject({{"url", {{"type", "string"}}}}, {"url"})},
        {QStringLiteral("computer_open_url"), QStringLiteral("Open a URL in the system default browser or handler. Use this for browser sites like YouTube."),
         schemaObject({{"url", {{"type", "string"}}}}, {"url"})},
        {QStringLiteral("computer_write_file"), QStringLiteral("Create a UTF-8 text file on the computer. Relative paths default to the Desktop unless base_dir is provided."),
         schemaObject({{"path", {{"type", "string"}}},
                       {"content", {{"type", "string"}}},
                       {"base_dir", {{"type", "string"}}},
                       {"overwrite", {{"type", "boolean"}}}},
                      {"path", "content"})},
        {QStringLiteral("skill_list"), QStringLiteral("List installed declarative skills."),
         schemaObject({})},
        {QStringLiteral("skill_install"), QStringLiteral("Install a skill from a GitHub repo URL or zip URL."),
         schemaObject({{"url", {{"type", "string"}}}}, {"url"})},
        {QStringLiteral("skill_create"), QStringLiteral("Create a new local skill scaffold."),
         schemaObject({{"id", {{"type", "string"}}}, {"name", {{"type", "string"}}}, {"description", {{"type", "string"}}}}, {"id", "name", "description"})}
    };

    const PlatformCapabilities capabilities = PlatformRuntime::currentCapabilities();
    if (capabilities.supportsAppListing) {
        tools.push_back({QStringLiteral("computer_list_apps"), QStringLiteral("List installed apps from the desktop environment, optionally filtered by name."),
                         schemaObject({{"query", {{"type", "string"}}}, {"limit", {{"type", "integer"}}}}, {})});
    }
    if (capabilities.supportsAppLaunch) {
        tools.push_back({QStringLiteral("computer_open_app"), QStringLiteral("Open an installed app, a shortcut, or an executable path."),
                         schemaObject({{"target", {{"type", "string"}}}, {"arguments", {{"type", "array"}, {"items", {{"type", "string"}}}}}}, {"target"})});
    }
    if (capabilities.supportsTimerNotification) {
        tools.push_back({QStringLiteral("computer_set_timer"), QStringLiteral("Set a local timer that will show a reminder when it finishes."),
                         schemaObject({{"duration_seconds", {{"type", "integer"}}},
                                       {"title", {{"type", "string"}}},
                                       {"message", {{"type", "string"}}}},
                                      {"duration_seconds"})});
    }

    return tools;
}

AgentToolResult AgentToolbox::execute(const AgentToolCall &call)
{
    const auto args = nlohmann::json::parse(call.argumentsJson.toStdString(), nullptr, false);
    if (call.name != QStringLiteral("skill_list") && args.is_discarded()) {
        return failedResult(call, QStringLiteral("Tool arguments were not valid JSON."));
    }

    if (call.name == QStringLiteral("file_read")) return executeFileRead(call, args);
    if (call.name == QStringLiteral("file_search")) return executeFileSearch(call, args);
    if (call.name == QStringLiteral("file_write")) return executeFileWrite(call, args);
    if (call.name == QStringLiteral("file_patch")) return executeFilePatch(call, args);
    if (call.name == QStringLiteral("dir_list")) return executeDirList(call, args);
    if (call.name == QStringLiteral("memory_search")) return executeMemorySearch(call, args);
    if (call.name == QStringLiteral("memory_write")) return executeMemoryWrite(call, args);
    if (call.name == QStringLiteral("memory_delete")) return executeMemoryDelete(call, args);
    if (call.name == QStringLiteral("log_tail")) return executeLogTail(call, args);
    if (call.name == QStringLiteral("log_search")) return executeLogSearch(call, args);
    if (call.name == QStringLiteral("ai_log_read")) return executeAiLogRead(call, args);
    if (call.name == QStringLiteral("web_search")) return executeWebSearch(call, args);
    if (call.name == QStringLiteral("web_fetch")) return executeWebFetch(call, args);
    if (call.name == QStringLiteral("computer_list_apps")) return executeComputerListApps(call, args);
    if (call.name == QStringLiteral("computer_open_app")) return executeComputerOpenApp(call, args);
    if (call.name == QStringLiteral("computer_open_url")) return executeComputerOpenUrl(call, args);
    if (call.name == QStringLiteral("computer_write_file")) return executeComputerWriteFile(call, args);
    if (call.name == QStringLiteral("computer_set_timer")) return executeComputerSetTimer(call, args);
    if (call.name == QStringLiteral("skill_list")) return executeSkillList(call);
    if (call.name == QStringLiteral("skill_install")) return executeSkillInstall(call, args);
    if (call.name == QStringLiteral("skill_create")) return executeSkillCreate(call, args);

    return failedResult(call, QStringLiteral("Unknown tool."));
}

QStringList AgentToolbox::allowedRoots() const
{
    return {
        canonicalPath(QDir::currentPath()),
        canonicalPath(QDir::currentPath() + QStringLiteral("/config")),
        canonicalPath(QDir::currentPath() + QStringLiteral("/bin/logs")),
        canonicalPath(QDir::currentPath() + QStringLiteral("/skills")),
        canonicalPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)),
        canonicalPath(m_skillStore->skillsRoot())
    };
}

bool AgentToolbox::isReadablePath(const QString &path) const
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return false;
    }

    if (info.isDir()) {
        return QDir(info.absoluteFilePath()).exists();
    }

    return info.isFile() && info.isReadable();
}

bool AgentToolbox::isWritablePath(const QString &path) const
{
    const QString resolved = canonicalPath(path);
    for (const QString &root : allowedRoots()) {
        if (!root.isEmpty() && resolved.startsWith(root, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

AgentToolResult AgentToolbox::executeFileRead(const AgentToolCall &call, const nlohmann::json &args)
{
    const QString path = extractString(args, "path");
    if (!isReadablePath(path)) {
        return failedResult(call, QStringLiteral("Read path is not readable."));
    }
    const QString text = readTextFile(path);
    return text.isEmpty() ? failedResult(call, QStringLiteral("Failed to read file.")) : successResult(call, text);
}

AgentToolResult AgentToolbox::executeFileSearch(const AgentToolCall &call, const nlohmann::json &args)
{
    const QString root = extractString(args, "root");
    const QString query = extractString(args, "query");
    if (!isReadablePath(root)) {
        return failedResult(call, QStringLiteral("Search root is not readable."));
    }

    QStringList matches;
    QDirIterator it(root, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext() && matches.size() < 25) {
        const QString filePath = it.next();
        const QString text = readTextFile(filePath, 20000);
        if (text.contains(query, Qt::CaseInsensitive)) {
            matches.push_back(QDir::cleanPath(filePath));
        }
    }
    return successResult(call, matches.join(QStringLiteral("\n")));
}

AgentToolResult AgentToolbox::executeFileWrite(const AgentToolCall &call, const nlohmann::json &args)
{
    const QString path = extractString(args, "path");
    const QString content = extractString(args, "content");
    if (!isWritablePath(path)) {
        return failedResult(call, QStringLiteral("Write path is outside allowed roots."));
    }

    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return failedResult(call, QStringLiteral("Failed to open file for writing."));
    }
    file.write(content.toUtf8());
    return successResult(call, QStringLiteral("Wrote %1 bytes to %2").arg(content.toUtf8().size()).arg(QDir::cleanPath(path)));
}

AgentToolResult AgentToolbox::executeFilePatch(const AgentToolCall &call, const nlohmann::json &args)
{
    const QString path = extractString(args, "path");
    const QString find = extractString(args, "find");
    const QString replace = extractString(args, "replace");
    if (!isWritablePath(path)) {
        return failedResult(call, QStringLiteral("Patch path is outside allowed roots."));
    }

    QString text = readTextFile(path, 500000);
    if (text.isEmpty()) {
        return failedResult(call, QStringLiteral("Failed to read file for patching."));
    }
    if (!text.contains(find)) {
        return failedResult(call, QStringLiteral("Patch target text was not found."));
    }
    text.replace(find, replace);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return failedResult(call, QStringLiteral("Failed to write patched file."));
    }
    file.write(text.toUtf8());
    return successResult(call, QStringLiteral("Patched %1").arg(QDir::cleanPath(path)));
}

AgentToolResult AgentToolbox::executeDirList(const AgentToolCall &call, const nlohmann::json &args)
{
    const QString path = extractString(args, "path");
    if (!isReadablePath(path)) {
        return failedResult(call, QStringLiteral("Directory is not readable."));
    }

    const QFileInfoList entries = QDir(path).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    QStringList lines;
    for (const QFileInfo &entry : entries.mid(0, 100)) {
        lines.push_back(QStringLiteral("%1\t%2").arg(entry.isDir() ? QStringLiteral("dir") : QStringLiteral("file"), entry.fileName()));
    }
    return successResult(call, lines.join(QStringLiteral("\n")));
}

AgentToolResult AgentToolbox::executeMemorySearch(const AgentToolCall &call, const nlohmann::json &args)
{
    const QString query = extractString(args, "query");
    QStringList lines;
    for (const auto &entry : m_memoryStore->searchEntries(query, 12)) {
        lines.push_back(QStringLiteral("%1 | %2 | %3").arg(entry.kind, entry.title, entry.content));
    }
    return successResult(call, lines.join(QStringLiteral("\n")));
}

AgentToolResult AgentToolbox::executeMemoryWrite(const AgentToolCall &call, const nlohmann::json &args)
{
    MemoryEntry entry;
    entry.kind = extractString(args, "kind");
    entry.title = extractString(args, "title");
    entry.content = extractString(args, "content");
    entry.key = extractString(args, "key");
    entry.value = extractString(args, "value");
    entry.secret = args.value("secret", false);
    entry.source = QStringLiteral("agent_tool");
    const bool ok = m_memoryStore->upsertEntry(entry);
    return ok ? successResult(call, QStringLiteral("Memory saved.")) : failedResult(call, QStringLiteral("Memory write failed."));
}

AgentToolResult AgentToolbox::executeMemoryDelete(const AgentToolCall &call, const nlohmann::json &args)
{
    const bool ok = m_memoryStore->deleteEntry(extractString(args, "id"));
    return ok ? successResult(call, QStringLiteral("Memory deleted.")) : failedResult(call, QStringLiteral("Memory entry not found."));
}

AgentToolResult AgentToolbox::executeLogTail(const AgentToolCall &call, const nlohmann::json &args)
{
    const QString path = extractString(args, "path");
    const int lines = args.contains("lines") && args.at("lines").is_number_integer() ? args.at("lines").get<int>() : 120;
    if (!isReadablePath(path)) {
        return failedResult(call, QStringLiteral("Log path is not readable."));
    }

    const QString content = readTextFile(path, 64000);
    if (content.isEmpty()) {
        return failedResult(call, QStringLiteral("Failed to read the log tail."));
    }
    const QStringList lineList = content.split(QRegularExpression(QStringLiteral("\r?\n")), Qt::KeepEmptyParts);
    const qsizetype lineCount = lineList.size();
    const qsizetype requestedLines = std::max<qsizetype>(1, lines);
    const qsizetype startIndex = std::max<qsizetype>(0, lineCount - requestedLines);
    return successResult(call, lineList.mid(startIndex).join(QStringLiteral("\n")));
}

AgentToolResult AgentToolbox::executeLogSearch(const AgentToolCall &call, const nlohmann::json &args)
{
    const QString path = extractString(args, "path");
    const QString query = extractString(args, "query");
    if (!isReadablePath(path)) {
        return failedResult(call, QStringLiteral("Log search path is not readable."));
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
        return failedResult(call, QStringLiteral("Failed to search the log."));
    }
    return successResult(call, matches.join(QStringLiteral("\n")));
}

AgentToolResult AgentToolbox::executeAiLogRead(const AgentToolCall &call, const nlohmann::json &args)
{
    QString path = extractString(args, "path");
    if (path.isEmpty()) {
        const QDir logDir(QDir::currentPath() + QStringLiteral("/bin/logs/AI"));
        const QFileInfoList files = logDir.entryInfoList({QStringLiteral("*.log")}, QDir::Files | QDir::Readable, QDir::Time);
        if (files.isEmpty()) {
            return failedResult(call, QStringLiteral("No AI logs found."));
        }
        path = files.first().absoluteFilePath();
    }
    if (!isReadablePath(path)) {
        return failedResult(call, QStringLiteral("AI log path is not readable."));
    }
    return successResult(call, readTextFile(path, 18000));
}

AgentToolResult AgentToolbox::executeWebSearch(const AgentToolCall &call, const nlohmann::json &args)
{
    const QString query = extractString(args, "query");
    const QString provider = m_settings->webSearchProvider().toLower();
    if (provider == QStringLiteral("brave")) {
        QString apiKey = m_settings->braveSearchApiKey().trimmed();
        if (apiKey.isEmpty()) {
            apiKey = qEnvironmentVariable("BRAVE_SEARCH_API_KEY");
        }
        if (!apiKey.isEmpty()) {
            const NetworkFetchResult fetch = httpGet(
                QUrl(QStringLiteral("https://api.search.brave.com/res/v1/web/search?q=%1")
                         .arg(QString::fromUtf8(QUrl::toPercentEncoding(query)))),
                {
                    {QByteArray("Accept"), QByteArray("application/json")},
                    {QByteArray("X-Subscription-Token"), apiKey.toUtf8()}
                },
                20000);
            if (fetch.ok) {
                return successResult(call, QString::fromUtf8(fetch.body));
            }
        }
    }

    const NetworkFetchResult fetch = httpGet(
        QUrl(QStringLiteral("https://api.duckduckgo.com/?q=%1&format=json&no_html=1&skip_disambig=1")
                 .arg(QString::fromUtf8(QUrl::toPercentEncoding(query)))),
        {},
        20000);
    if (!fetch.ok) {
        return failedResult(call, QStringLiteral("Web search failed."));
    }
    return successResult(call, QString::fromUtf8(fetch.body));
}

AgentToolResult AgentToolbox::executeWebFetch(const AgentToolCall &call, const nlohmann::json &args)
{
    const QString url = extractString(args, "url");
    const NetworkFetchResult fetch = httpGet(QUrl(url), {}, 30000);
    if (!fetch.ok) {
        return failedResult(call, QStringLiteral("Web fetch failed."));
    }
    QString output = QString::fromUtf8(fetch.body);
    if (output.size() > 12000) {
        output = output.left(12000) + QStringLiteral("\n...[truncated]");
    }
    return successResult(call, output);
}

AgentToolResult AgentToolbox::executeComputerListApps(const AgentToolCall &call, const nlohmann::json &args)
{
    const QString query = extractString(args, "query");
    const int limit = args.contains("limit") && args.at("limit").is_number_integer() ? args.at("limit").get<int>() : 20;
    const auto result = ComputerControl::listApps(query, limit);
    const QString output = result.lines.isEmpty()
        ? result.detail
        : result.lines.join(QStringLiteral("\n"));
    return result.success ? successResult(call, output) : failedResult(call, output);
}

AgentToolResult AgentToolbox::executeComputerOpenApp(const AgentToolCall &call, const nlohmann::json &args)
{
    QStringList arguments;
    if (args.contains("arguments") && args.at("arguments").is_array()) {
        for (const auto &argument : args.at("arguments")) {
            if (argument.is_string()) {
                arguments.push_back(QString::fromStdString(argument.get<std::string>()));
            }
        }
    }

    const auto result = ComputerControl::launchApp(extractString(args, "target"), arguments);
    QString output = result.detail;
    if (!result.lines.isEmpty()) {
        output += QStringLiteral("\n") + result.lines.join(QStringLiteral("\n"));
    }
    return result.success ? successResult(call, output.trimmed()) : failedResult(call, output.trimmed());
}

AgentToolResult AgentToolbox::executeComputerOpenUrl(const AgentToolCall &call, const nlohmann::json &args)
{
    const auto result = ComputerControl::openUrl(extractString(args, "url"));
    return result.success ? successResult(call, result.detail) : failedResult(call, result.detail);
}

AgentToolResult AgentToolbox::executeComputerWriteFile(const AgentToolCall &call, const nlohmann::json &args)
{
    const bool overwrite = args.value("overwrite", false);
    const auto result = ComputerControl::writeTextFile(extractString(args, "path"),
                                                       extractString(args, "content"),
                                                       overwrite,
                                                       extractString(args, "base_dir"));
    return result.success ? successResult(call, result.detail) : failedResult(call, result.detail);
}

AgentToolResult AgentToolbox::executeComputerSetTimer(const AgentToolCall &call, const nlohmann::json &args)
{
    if (!args.contains("duration_seconds") || !args.at("duration_seconds").is_number_integer()) {
        return failedResult(call, QStringLiteral("duration_seconds must be an integer."));
    }

    const auto result = ComputerControl::setTimer(args.at("duration_seconds").get<int>(),
                                                  extractString(args, "title"),
                                                  extractString(args, "message"));
    return result.success ? successResult(call, result.detail) : failedResult(call, result.detail);
}

AgentToolResult AgentToolbox::executeSkillList(const AgentToolCall &call)
{
    QStringList lines;
    for (const auto &skill : m_skillStore->listSkills()) {
        lines.push_back(QStringLiteral("%1 | %2 | %3").arg(skill.id, skill.name, skill.description));
    }
    return successResult(call, lines.join(QStringLiteral("\n")));
}

AgentToolResult AgentToolbox::executeSkillInstall(const AgentToolCall &call, const nlohmann::json &args)
{
    QString error;
    const bool ok = m_skillStore->installSkill(extractString(args, "url"), &error);
    return ok ? successResult(call, QStringLiteral("Skill installed.")) : failedResult(call, error);
}

AgentToolResult AgentToolbox::executeSkillCreate(const AgentToolCall &call, const nlohmann::json &args)
{
    QString error;
    const bool ok = m_skillStore->createSkill(extractString(args, "id"), extractString(args, "name"), extractString(args, "description"), &error);
    return ok ? successResult(call, QStringLiteral("Skill created.")) : failedResult(call, error);
}

AgentToolResult AgentToolbox::failedResult(const AgentToolCall &call, const QString &message) const
{
    if (m_loggingService) {
        m_loggingService->warn(QStringLiteral("Agent tool failed. tool=\"%1\" message=\"%2\"").arg(call.name, message.left(240)));
    }
    return {
        .callId = call.id,
        .toolName = call.name,
        .output = message,
        .success = false
    };
}

AgentToolResult AgentToolbox::successResult(const AgentToolCall &call, const QString &message) const
{
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Agent tool completed. tool=\"%1\"").arg(call.name));
    }
    return {
        .callId = call.id,
        .toolName = call.name,
        .output = message,
        .success = true
    };
}
