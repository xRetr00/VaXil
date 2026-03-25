#pragma once

#include <optional>

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

#include "core/AssistantTypes.h"

class LoggingService;

class ToolWorker : public QObject
{
    Q_OBJECT

public:
    explicit ToolWorker(const QStringList &allowedRoots, LoggingService *loggingService = nullptr, QObject *parent = nullptr);

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

    bool isAllowedPath(const QString &path) const;
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
    QJsonObject processMemoryWrite(const AgentTask &task) const;

    QStringList m_allowedRoots;
    LoggingService *m_loggingService = nullptr;
    QHash<QString, QElapsedTimer> m_lastExecution;
    QHash<QString, CachedResult> m_cache;
    QSet<int> m_canceledTaskIds;
};
