#include "logging/LoggingService.h"

#include <QCoreApplication>
#include <QDir>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

LoggingService::LoggingService(QObject *parent)
    : QObject(parent)
{
}

bool LoggingService::initialize()
{
    const auto root = QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
    QDir().mkpath(root);
    m_logFilePath = root + QStringLiteral("/jarvis.log");

    try {
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            m_logFilePath.toStdString(),
            1024 * 1024 * 5,
            3);
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};
        m_logger = std::make_shared<spdlog::logger>("jarvis", sinks.begin(), sinks.end());
        spdlog::register_logger(m_logger);
        m_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        return true;
    } catch (...) {
        return false;
    }
}

void LoggingService::info(const QString &message) const
{
    if (m_logger) {
        m_logger->info(message.toStdString());
    }
}

void LoggingService::warn(const QString &message) const
{
    if (m_logger) {
        m_logger->warn(message.toStdString());
    }
}

void LoggingService::error(const QString &message) const
{
    if (m_logger) {
        m_logger->error(message.toStdString());
    }
}

QString LoggingService::logFilePath() const
{
    return m_logFilePath;
}
