#pragma once

#include <memory>
#include <optional>

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

#include "core/AssistantTypes.h"
#include "memory/MemoryManager.h"

class LoggingService;
class AppSettings;

class ToolWorker : public QObject
{
    Q_OBJECT

public:
    explicit ToolWorker(const QStringList &allowedRoots,
                        LoggingService *loggingService = nullptr,
                        AppSettings *settings = nullptr,
                        QObject *parent = nullptr);

public slots:
    void processTask(const AgentTask &task);
    void cancelTask(int taskId);

signals:
    void taskStarted(int taskId, const QString &type);
    void taskFinished(int taskId, QJsonObject result);

private:
    struct CachedResult {
        QJsonObject result;
        qint64 cachedAtMs = 0;
    };

    bool isReadablePath(const QString &path) const;
    bool isWritablePath(const QString &path) const;
    QString normalizePath(const QString &path) const;
    QString cacheKeyFor(const AgentTask &task) const;
    bool isCooldownActive(const AgentTask &task) const;
    std::optional<QJsonObject> cachedResultFor(const AgentTask &task) const;
    QJsonObject buildResult(const AgentTask &task,
                            bool success,
                            TaskState state,
                            const QString &title,
                            const QString &summary,
                            const QString &detail,
                            const QJsonObject &payload = {}) const;
    QJsonObject processDirList(const AgentTask &task);
    QJsonObject processFileRead(const AgentTask &task);
    QJsonObject processFileWrite(const AgentTask &task);
    QJsonObject processLogTail(const AgentTask &task);
    QJsonObject processLogSearch(const AgentTask &task);
    QJsonObject processAiLogRead(const AgentTask &task);
    QJsonObject processWebSearch(const AgentTask &task);
    QJsonObject processMemoryWrite(const AgentTask &task);
    QJsonObject processComputerListApps(const AgentTask &task);
    QJsonObject processComputerOpenApp(const AgentTask &task);
    QJsonObject processComputerOpenUrl(const AgentTask &task);
    QJsonObject processComputerWriteFile(const AgentTask &task);
    QJsonObject processComputerSetTimer(const AgentTask &task);

    QStringList m_allowedRoots;
    LoggingService *m_loggingService = nullptr;
    AppSettings *m_settings = nullptr;
    std::unique_ptr<MemoryManager> m_memoryManager;
    QHash<QString, QElapsedTimer> m_lastExecution;
    QHash<QString, CachedResult> m_cache;
    QSet<int> m_canceledTaskIds;
};
