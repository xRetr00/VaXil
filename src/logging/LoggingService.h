#pragma once

#include <memory>

#include <QObject>

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
    std::shared_ptr<spdlog::logger> m_logger;
    QString m_logFilePath;
};
