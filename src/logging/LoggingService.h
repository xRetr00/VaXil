#pragma once

#include <memory>

#include <QObject>
#include <QHash>
#include <QMutex>

#include "core/AssistantTypes.h"

namespace spdlog {
class logger;
}

class LoggingService : public QObject
{
    Q_OBJECT

public:
    explicit LoggingService(QObject *parent = nullptr);

    bool initialize();
    void info(const QString &message) const;
    void warn(const QString &message) const;
    void error(const QString &message) const;
    void infoFor(const QString &channel, const QString &message) const;
    void warnFor(const QString &channel, const QString &message) const;
    void errorFor(const QString &channel, const QString &message) const;
    void logVisionSnapshot(const VisionSnapshot &snapshot, const QString &source = QStringLiteral("vision_ingest")) const;
    void logVisionStatus(const QString &message,
                         const QString &rateLimitKey = QStringLiteral("vision_status"),
                         int intervalMs = 2000) const;
    void logVisionDrop(const QString &reason,
                       const QString &detail = QString(),
                       const QString &rateLimitKey = QStringLiteral("vision_drop"),
                       int intervalMs = 1500) const;
    bool logAiExchange(const QString &prompt, const QString &response, const QString &source, const QString &status = QString()) const;
    bool logAgentExchange(const QString &prompt,
                          const QString &response,
                          const QString &source,
                          const AgentCapabilitySet &capabilities,
                          const SamplingProfile &sampling,
                          const QList<AgentTraceEntry> &trace,
                          const QString &status = QString()) const;
    QString logFilePath() const;

private:
    bool shouldLogRateLimited(const QString &key, int intervalMs) const;
    std::shared_ptr<spdlog::logger> loggerForChannel(const QString &channel) const;

    std::shared_ptr<spdlog::logger> m_logger;
    std::shared_ptr<spdlog::logger> m_aiLogger;
    std::shared_ptr<spdlog::logger> m_toolsMcpLogger;
    std::shared_ptr<spdlog::logger> m_visionLogger;
    std::shared_ptr<spdlog::logger> m_wakeLogger;
    std::shared_ptr<spdlog::logger> m_ttsLogger;
    std::shared_ptr<spdlog::logger> m_sttLogger;
    QString m_logFilePath;
    mutable QHash<QString, qint64> m_rateLimitedLogTimes;
    mutable QMutex m_rateLimitMutex;
};
