#include "agent/AgentToolbox.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QStandardPaths>

#include "core/tools/ToolExecutionService.h"
#include "platform/PlatformRuntime.h"
#include "python/PythonRuntimeManager.h"
#include "settings/AppSettings.h"
#include "skills/SkillStore.h"

namespace {
QString canonicalPath(const QString &path)
{
    QFileInfo info(path);
    if (info.exists()) {
        return info.canonicalFilePath();
    }
    return QDir::cleanPath(info.absoluteFilePath());
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

nlohmann::json qJsonValueToStdJson(const QJsonValue &value)
{
    const QJsonDocument doc = value.isObject()
        ? QJsonDocument(value.toObject())
        : QJsonDocument(value.toArray());
    return nlohmann::json::parse(doc.toJson(QJsonDocument::Compact).constData(), nullptr, false);
}

AgentToolSpec toolSpecFromRuntimeJson(const QJsonObject &object)
{
    AgentToolSpec spec;
    spec.name = object.value(QStringLiteral("name")).toString();
    spec.description = object.value(QStringLiteral("description")).toString();
    if (object.contains(QStringLiteral("args_schema"))) {
        spec.parameters = qJsonValueToStdJson(object.value(QStringLiteral("args_schema")));
        if (spec.parameters.is_discarded()) {
            spec.parameters = nlohmann::json::object();
        }
    }
    return spec;
}
}

AgentToolbox::AgentToolbox(AppSettings *settings,
                           MemoryStore *memoryStore,
                           SkillStore *skillStore,
                           LoggingService *loggingService,
                           QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_memoryStore(memoryStore)
    , m_skillStore(skillStore)
    , m_loggingService(loggingService)
    , m_pythonRuntime(new PythonRuntimeManager(allowedRoots(), loggingService, this))
    , m_toolExecutionService(new ToolExecutionService(allowedRoots(), settings, skillStore, loggingService, this))
{
}

QList<AgentToolSpec> AgentToolbox::builtInTools() const
{
    QList<AgentToolSpec> tools;
    QString runtimeError;
    const QJsonArray runtimeCatalog = m_pythonRuntime ? m_pythonRuntime->listCatalog(&runtimeError) : QJsonArray{};
    for (const QJsonValue &value : runtimeCatalog) {
        const AgentToolSpec spec = toolSpecFromRuntimeJson(value.toObject());
        if (!spec.name.isEmpty()) {
            tools.push_back(spec);
        }
    }

    tools.push_back({QStringLiteral("memory_search"), QStringLiteral("Search structured memory entries."),
                     schemaObject({{"query", {{"type", "string"}}}}, {"query"})});
    tools.push_back({QStringLiteral("memory_write"), QStringLiteral("Write or update a structured memory entry."),
                     schemaObject({{"kind", {{"type", "string"}}}, {"title", {{"type", "string"}}}, {"content", {{"type", "string"}}}}, {"kind", "title", "content"})});
    tools.push_back({QStringLiteral("memory_delete"), QStringLiteral("Delete a memory entry by id or title."),
                     schemaObject({{"id", {{"type", "string"}}}}, {"id"})});
    tools.push_back({QStringLiteral("skill_list"), QStringLiteral("List installed declarative skills."),
                     schemaObject({})});
    tools.push_back({QStringLiteral("skill_install"), QStringLiteral("Install a skill from a GitHub repo URL or zip URL."),
                     schemaObject({{"url", {{"type", "string"}}}}, {"url"})});
    tools.push_back({QStringLiteral("skill_create"), QStringLiteral("Create a new local skill scaffold."),
                     schemaObject({{"id", {{"type", "string"}}}, {"name", {{"type", "string"}}}, {"description", {{"type", "string"}}}}, {"id", "name", "description"})});

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

    QSet<QString> seen;
    QList<AgentToolSpec> deduped;
    for (const AgentToolSpec &tool : tools) {
        if (!tool.name.isEmpty() && !seen.contains(tool.name)) {
            seen.insert(tool.name);
            deduped.push_back(tool);
        }
    }

    return deduped;
}

AgentToolResult AgentToolbox::execute(const AgentToolCall &call)
{
    return m_toolExecutionService
        ? m_toolExecutionService->executeCall(call)
        : AgentToolResult{
            .callId = call.id,
            .toolName = call.name,
            .output = QStringLiteral("Tool execution service unavailable."),
            .success = false,
            .errorKind = ToolErrorKind::Capability,
            .summary = QStringLiteral("Tool unavailable"),
            .detail = QStringLiteral("Tool execution service unavailable.")
        };
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
