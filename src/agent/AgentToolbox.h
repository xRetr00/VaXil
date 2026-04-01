#pragma once

#include <QObject>

#include "core/AssistantTypes.h"

class AppSettings;
class LoggingService;
class MemoryStore;
class PythonRuntimeManager;
class SkillStore;
class ToolExecutionService;

class AgentToolbox : public QObject
{
    Q_OBJECT

public:
    AgentToolbox(AppSettings *settings, MemoryStore *memoryStore, SkillStore *skillStore, LoggingService *loggingService, QObject *parent = nullptr);

    QList<AgentToolSpec> builtInTools() const;
    AgentToolResult execute(const AgentToolCall &call);

private:
    QStringList allowedRoots() const;

    AppSettings *m_settings = nullptr;
    MemoryStore *m_memoryStore = nullptr;
    SkillStore *m_skillStore = nullptr;
    LoggingService *m_loggingService = nullptr;
    PythonRuntimeManager *m_pythonRuntime = nullptr;
    ToolExecutionService *m_toolExecutionService = nullptr;
};
