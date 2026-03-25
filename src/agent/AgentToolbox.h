#pragma once

#include <QObject>

#include "core/AssistantTypes.h"

class AppSettings;
class LoggingService;
class MemoryStore;
class SkillStore;

class AgentToolbox : public QObject
{
    Q_OBJECT

public:
    AgentToolbox(AppSettings *settings, MemoryStore *memoryStore, SkillStore *skillStore, LoggingService *loggingService, QObject *parent = nullptr);

    QList<AgentToolSpec> builtInTools() const;
    AgentToolResult execute(const AgentToolCall &call);

private:
    QStringList allowedRoots() const;
    bool isAllowedPath(const QString &path, bool forWrite) const;
    AgentToolResult executeFileRead(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult executeFileSearch(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult executeFileWrite(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult executeFilePatch(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult executeDirList(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult executeMemorySearch(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult executeMemoryWrite(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult executeMemoryDelete(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult executeLogTail(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult executeLogSearch(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult executeAiLogRead(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult executeWebSearch(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult executeWebFetch(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult executeSkillList(const AgentToolCall &call);
    AgentToolResult executeSkillInstall(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult executeSkillCreate(const AgentToolCall &call, const nlohmann::json &args);
    AgentToolResult failedResult(const AgentToolCall &call, const QString &message) const;
    AgentToolResult successResult(const AgentToolCall &call, const QString &message) const;

    AppSettings *m_settings = nullptr;
    MemoryStore *m_memoryStore = nullptr;
    SkillStore *m_skillStore = nullptr;
    LoggingService *m_loggingService = nullptr;
};
