#pragma once

#include <memory>

#include <QObject>
#include <QJsonObject>
#include <QStringList>

#include "core/AssistantTypes.h"
#include "memory/MemoryManager.h"

class AppSettings;
class LoggingService;
class PythonRuntimeManager;
class SkillStore;

class ToolExecutionService : public QObject
{
    Q_OBJECT

public:
    explicit ToolExecutionService(const QStringList &allowedRoots,
                                  AppSettings *settings = nullptr,
                                  SkillStore *skillStore = nullptr,
                                  LoggingService *loggingService = nullptr,
                                  QObject *parent = nullptr);

    ToolExecutionResult execute(const ToolExecutionRequest &request);
    AgentToolResult executeCall(const AgentToolCall &call);

    static QString outputTextForModel(const ToolExecutionResult &result);
    static QJsonObject taskResultObject(const AgentTask &task,
                                        const ToolExecutionResult &result,
                                        TaskState state = TaskState::Finished);

private:
    bool isReadablePath(const QString &path) const;
    bool isWritablePath(const QString &path) const;
    QString normalizePath(const QString &path) const;
    ToolErrorKind classifyErrorKind(const QString &message, int statusCode = 0) const;

    ToolExecutionResult executeBuiltIn(const ToolExecutionRequest &request);
    ToolExecutionResult executeFileRead(const ToolExecutionRequest &request);
    ToolExecutionResult executeFileSearch(const ToolExecutionRequest &request);
    ToolExecutionResult executeFileWrite(const ToolExecutionRequest &request);
    ToolExecutionResult executeFilePatch(const ToolExecutionRequest &request);
    ToolExecutionResult executeDirList(const ToolExecutionRequest &request);
    ToolExecutionResult executeMemorySearch(const ToolExecutionRequest &request);
    ToolExecutionResult executeMemoryWrite(const ToolExecutionRequest &request);
    ToolExecutionResult executeMemoryDelete(const ToolExecutionRequest &request);
    ToolExecutionResult executeLogTail(const ToolExecutionRequest &request);
    ToolExecutionResult executeLogSearch(const ToolExecutionRequest &request);
    ToolExecutionResult executeAiLogRead(const ToolExecutionRequest &request);
    ToolExecutionResult executeWebSearch(const ToolExecutionRequest &request);
    ToolExecutionResult executeWebFetch(const ToolExecutionRequest &request);
    ToolExecutionResult executeComputerListApps(const ToolExecutionRequest &request);
    ToolExecutionResult executeComputerOpenApp(const ToolExecutionRequest &request);
    ToolExecutionResult executeComputerOpenUrl(const ToolExecutionRequest &request);
    ToolExecutionResult executeComputerWriteFile(const ToolExecutionRequest &request);
    ToolExecutionResult executeComputerSetTimer(const ToolExecutionRequest &request);
    ToolExecutionResult executeSkillList(const ToolExecutionRequest &request);
    ToolExecutionResult executeSkillInstall(const ToolExecutionRequest &request);
    ToolExecutionResult executeSkillCreate(const ToolExecutionRequest &request);

    ToolExecutionResult successResult(const ToolExecutionRequest &request,
                                      const QString &summary,
                                      const QString &detail,
                                      const QJsonObject &payload = {}) const;
    ToolExecutionResult failureResult(const ToolExecutionRequest &request,
                                      ToolErrorKind errorKind,
                                      const QString &summary,
                                      const QString &detail,
                                      const QJsonObject &payload = {},
                                      const QString &rawProviderError = {}) const;

    QStringList m_allowedRoots;
    AppSettings *m_settings = nullptr;
    SkillStore *m_skillStore = nullptr;
    LoggingService *m_loggingService = nullptr;
    PythonRuntimeManager *m_pythonRuntime = nullptr;
    std::unique_ptr<MemoryManager> m_memoryManager;
};
