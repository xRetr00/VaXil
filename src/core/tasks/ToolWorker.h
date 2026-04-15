#pragma once

#include <memory>
#include <optional>

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

#include "core/AssistantTypes.h"

class LoggingService;
class AppSettings;
class ToolExecutionService;

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
    void connectorEventReady(const ConnectorEvent &event);
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
    QStringList m_allowedRoots;
    LoggingService *m_loggingService = nullptr;
    AppSettings *m_settings = nullptr;
    ToolExecutionService *m_toolExecutionService = nullptr;
    QHash<QString, QElapsedTimer> m_lastExecution;
    QHash<QString, CachedResult> m_cache;
    QSet<int> m_canceledTaskIds;
};
