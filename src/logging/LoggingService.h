#pragma once

#include <memory>

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QVariantList>

#include "companion/contracts/BehaviorTraceEvent.h"
#include "companion/contracts/FocusModeState.h"
#include "core/AssistantTypes.h"

class BehavioralEventLedger;

namespace spdlog {
class logger;
}

class LoggingService : public QObject
{
    Q_OBJECT

public:
    explicit LoggingService(QObject *parent = nullptr);
    ~LoggingService() override;

    bool initialize();
    void info(const QString &message) const;
    void warn(const QString &message) const;
    void error(const QString &message) const;
    void fatal(const QString &message, const QString &errorCode = QString()) const;
    void crash(const QString &message, const QString &errorCode = QString()) const;
    void infoFor(const QString &channel, const QString &message) const;
    void warnFor(const QString &channel, const QString &message) const;
    void errorFor(const QString &channel, const QString &message) const;
    void fatalFor(const QString &channel, const QString &message, const QString &errorCode = QString()) const;
    void crashFor(const QString &channel, const QString &message, const QString &errorCode = QString()) const;
    void flushAll() const;
    void breadcrumb(const QString &module,
                    const QString &event,
                    const QString &detail = QString(),
                    const QString &traceId = QString(),
                    const QString &sessionId = QString()) const;
    void setRuntimeContext(const QString &module,
                           const QString &route,
                           const QString &tool,
                           const QString &traceId = QString(),
                           const QString &sessionId = QString(),
                           const QString &threadId = QString()) const;
    void logVisionSnapshot(const VisionSnapshot &snapshot, const QString &source = QStringLiteral("vision_ingest")) const;
    void logVisionStatus(const QString &message,
                         const QString &rateLimitKey = QStringLiteral("vision_status"),
                         int intervalMs = 2000) const;
    void logVisionDrop(const QString &reason,
                       const QString &detail = QString(),
                       const QString &rateLimitKey = QStringLiteral("vision_drop"),
                       int intervalMs = 1500) const;
    bool logBehaviorEvent(const BehaviorTraceEvent &event) const;
    bool logFocusModeTransition(const FocusModeState &state,
                                const QString &source,
                                const QString &reasonCode) const;
    QVariantList recentBehaviorEvents(int limit = 50) const;
    QString behaviorLedgerDatabasePath() const;
    QString behaviorLedgerNdjsonPath() const;
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
    void logWithSeverity(const QString &channel,
                         const QString &message,
                         const QString &severity,
                         const QString &errorCode,
                         bool flushImmediately) const;
    bool shouldLogRateLimited(const QString &key, int intervalMs) const;
    std::shared_ptr<spdlog::logger> loggerForChannel(const QString &channel) const;

    std::shared_ptr<spdlog::logger> m_logger;
    std::shared_ptr<spdlog::logger> m_aiLogger;
    std::shared_ptr<spdlog::logger> m_toolsMcpLogger;
    std::shared_ptr<spdlog::logger> m_visionLogger;
    std::shared_ptr<spdlog::logger> m_wakeLogger;
    std::shared_ptr<spdlog::logger> m_ttsLogger;
    std::shared_ptr<spdlog::logger> m_sttLogger;
    std::shared_ptr<spdlog::logger> m_crashLogger;
    std::shared_ptr<spdlog::logger> m_orbLogger;
    std::shared_ptr<spdlog::logger> m_promptAuditLogger;
    std::shared_ptr<spdlog::logger> m_routeAuditLogger;
    std::shared_ptr<spdlog::logger> m_safetyAuditLogger;
    std::shared_ptr<spdlog::logger> m_memoryAuditLogger;
    std::shared_ptr<spdlog::logger> m_toolAuditLogger;
    std::shared_ptr<spdlog::logger> m_followUpAuditLogger;
    std::unique_ptr<BehavioralEventLedger> m_behavioralLedger;
    QString m_logFilePath;
    mutable QHash<QString, qint64> m_rateLimitedLogTimes;
    mutable QMutex m_rateLimitMutex;
};
