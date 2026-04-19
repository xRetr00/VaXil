#include "logging/LoggingService.h"

#include "telemetry/BehavioralEventLedger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QStringConverter>
#include <QTextStream>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace {
QString normalizeChannel(QString channel)
{
    channel = channel.trimmed().toLower();
    if (channel == QStringLiteral("tools") || channel == QStringLiteral("mcp")) {
        return QStringLiteral("tools_mcp");
    }
    if (channel == QStringLiteral("prompt") || channel == QStringLiteral("prompt_audit")) {
        return QStringLiteral("ai_prompt");
    }
    if (channel == QStringLiteral("route") || channel == QStringLiteral("routing")) {
        return QStringLiteral("route_audit");
    }
    if (channel == QStringLiteral("safety") || channel == QStringLiteral("confirm")) {
        return QStringLiteral("safety_audit");
    }
    if (channel == QStringLiteral("memory")) {
        return QStringLiteral("memory_audit");
    }
    if (channel == QStringLiteral("tool_audit") || channel == QStringLiteral("tools_audit")) {
        return QStringLiteral("tool_audit");
    }
    if (channel == QStringLiteral("followup") || channel == QStringLiteral("follow_up")) {
        return QStringLiteral("follow_up_audit");
    }
    return channel;
}

QString detectChannelFromMessage(const QString &message)
{
    const QString lowered = message.toLower();

    if (lowered.contains(QStringLiteral("mcp"))
        || lowered.contains(QStringLiteral("[toolworker]"))
        || lowered.contains(QStringLiteral("[taskdispatcher]"))
        || lowered.contains(QStringLiteral("agent tool"))) {
        return QStringLiteral("tools_mcp");
    }

    if (lowered.contains(QStringLiteral("whisper"))
        || lowered.contains(QStringLiteral(" transcription"))
        || lowered.contains(QStringLiteral("stt"))) {
        return QStringLiteral("stt");
    }

    if (lowered.contains(QStringLiteral("tts"))
        || lowered.contains(QStringLiteral("piper"))) {
        return QStringLiteral("tts");
    }

    if (lowered.contains(QStringLiteral("wake"))
        || lowered.contains(QStringLiteral("sherpa"))) {
        return QStringLiteral("wake_engine");
    }

    if (lowered.contains(QStringLiteral("vision"))) {
        return QStringLiteral("vision");
    }

    if (lowered.contains(QStringLiteral("[orb]"))
        || lowered.contains(QStringLiteral("orb renderer"))
        || lowered.contains(QStringLiteral("orb.frag"))
        || lowered.contains(QStringLiteral("scenegraph"))
        || lowered.contains(QStringLiteral("shadereffect"))) {
        return QStringLiteral("orb_render");
    }

    if (lowered.contains(QStringLiteral("ai backend"))
        || lowered.contains(QStringLiteral("agent request"))
        || lowered.contains(QStringLiteral("conversation request"))
        || lowered.contains(QStringLiteral("command extraction"))
        || lowered.contains(QStringLiteral("local ai"))) {
        return QStringLiteral("ai");
    }

    return {};
}
}

LoggingService::LoggingService(QObject *parent)
    : QObject(parent)
{
}

LoggingService::~LoggingService() = default;

bool LoggingService::initialize()
{
    const auto root = QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
    QDir().mkpath(root);
    m_logFilePath = root + QStringLiteral("/vaxil.log");

    try {
        const auto makeFileLogger = [&root](const QString &name, const QString &fileName) {
            const QString absolutePath = root + QStringLiteral("/") + fileName;
            auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                absolutePath.toStdString(),
                1024 * 1024 * 5,
                3);
            auto logger = std::make_shared<spdlog::logger>(name.toStdString(), sink);
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            logger->set_level(spdlog::level::info);
            logger->flush_on(spdlog::level::info);
            return logger;
        };

        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            m_logFilePath.toStdString(),
            1024 * 1024 * 5,
            3);
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};
        m_logger = std::make_shared<spdlog::logger>("vaxil", sinks.begin(), sinks.end());
        spdlog::register_logger(m_logger);
        m_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        m_logger->set_level(spdlog::level::info);
        m_logger->flush_on(spdlog::level::info);

        m_aiLogger = makeFileLogger(QStringLiteral("vaxil_ai"), QStringLiteral("ai.log"));
        m_toolsMcpLogger = makeFileLogger(QStringLiteral("vaxil_tools_mcp"), QStringLiteral("tools_mcp.log"));
        m_visionLogger = makeFileLogger(QStringLiteral("vaxil_vision"), QStringLiteral("vision.log"));
        m_wakeLogger = makeFileLogger(QStringLiteral("vaxil_wake"), QStringLiteral("wake_engine.log"));
        m_ttsLogger = makeFileLogger(QStringLiteral("vaxil_tts"), QStringLiteral("tts.log"));
        m_sttLogger = makeFileLogger(QStringLiteral("vaxil_stt"), QStringLiteral("stt.log"));
        m_orbLogger = makeFileLogger(QStringLiteral("vaxil_orb"), QStringLiteral("orb_render.log"));
        m_promptAuditLogger = makeFileLogger(QStringLiteral("vaxil_ai_prompt"), QStringLiteral("ai_prompt.log"));
        m_routeAuditLogger = makeFileLogger(QStringLiteral("vaxil_route_audit"), QStringLiteral("route_audit.log"));
        m_safetyAuditLogger = makeFileLogger(QStringLiteral("vaxil_safety_audit"), QStringLiteral("safety_audit.log"));
        m_memoryAuditLogger = makeFileLogger(QStringLiteral("vaxil_memory_audit"), QStringLiteral("memory_audit.log"));
        m_toolAuditLogger = makeFileLogger(QStringLiteral("vaxil_tool_audit"), QStringLiteral("tool_audit.log"));
        m_followUpAuditLogger = makeFileLogger(QStringLiteral("vaxil_follow_up_audit"), QStringLiteral("follow_up_audit.log"));
        m_behavioralLedger = std::make_unique<BehavioralEventLedger>(root + QStringLiteral("/behavior"));
        if (m_behavioralLedger) {
            const bool ledgerInitialized = m_behavioralLedger->initialize();
            if (!ledgerInitialized) {
                return false;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

void LoggingService::info(const QString &message) const
{
    infoFor(detectChannelFromMessage(message), message);
}

void LoggingService::warn(const QString &message) const
{
    warnFor(detectChannelFromMessage(message), message);
}

void LoggingService::error(const QString &message) const
{
    errorFor(detectChannelFromMessage(message), message);
}

void LoggingService::infoFor(const QString &channel, const QString &message) const
{
    if (m_logger) {
        m_logger->info(message.toStdString());
    }

    if (const auto logger = loggerForChannel(channel); logger) {
        logger->info(message.toStdString());
    }
}

void LoggingService::warnFor(const QString &channel, const QString &message) const
{
    if (m_logger) {
        m_logger->warn(message.toStdString());
    }

    if (const auto logger = loggerForChannel(channel); logger) {
        logger->warn(message.toStdString());
    }
}

void LoggingService::errorFor(const QString &channel, const QString &message) const
{
    if (m_logger) {
        m_logger->error(message.toStdString());
    }

    if (const auto logger = loggerForChannel(channel); logger) {
        logger->error(message.toStdString());
    }
}

std::shared_ptr<spdlog::logger> LoggingService::loggerForChannel(const QString &channel) const
{
    const QString normalized = normalizeChannel(channel);
    if (normalized == QStringLiteral("ai")) {
        return m_aiLogger;
    }
    if (normalized == QStringLiteral("tools_mcp")) {
        return m_toolsMcpLogger;
    }
    if (normalized == QStringLiteral("vision")) {
        return m_visionLogger;
    }
    if (normalized == QStringLiteral("wake_engine")) {
        return m_wakeLogger;
    }
    if (normalized == QStringLiteral("tts")) {
        return m_ttsLogger;
    }
    if (normalized == QStringLiteral("stt")) {
        return m_sttLogger;
    }
    if (normalized == QStringLiteral("orb_render")) {
        return m_orbLogger;
    }
    if (normalized == QStringLiteral("ai_prompt")) {
        return m_promptAuditLogger;
    }
    if (normalized == QStringLiteral("route_audit")) {
        return m_routeAuditLogger;
    }
    if (normalized == QStringLiteral("safety_audit")) {
        return m_safetyAuditLogger;
    }
    if (normalized == QStringLiteral("memory_audit")) {
        return m_memoryAuditLogger;
    }
    if (normalized == QStringLiteral("tool_audit")) {
        return m_toolAuditLogger;
    }
    if (normalized == QStringLiteral("follow_up_audit")) {
        return m_followUpAuditLogger;
    }

    return nullptr;
}

bool LoggingService::shouldLogRateLimited(const QString &key, int intervalMs) const
{
    if (key.trimmed().isEmpty()) {
        return true;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker locker(&m_rateLimitMutex);
    const qint64 previousMs = m_rateLimitedLogTimes.value(key, 0);
    if (previousMs > 0 && (nowMs - previousMs) < intervalMs) {
        return false;
    }
    m_rateLimitedLogTimes.insert(key, nowMs);
    return true;
}

void LoggingService::logVisionSnapshot(const VisionSnapshot &snapshot, const QString &source) const
{
    const QString rateLimitKey = QStringLiteral("vision_snapshot_%1_%2")
        .arg(snapshot.nodeId, snapshot.summary.left(96));
    if (!shouldLogRateLimited(rateLimitKey, 1200)) {
        return;
    }

    QStringList objects;
    for (const auto &object : snapshot.objects) {
        objects.push_back(QStringLiteral("%1(%2)")
                              .arg(object.className)
                              .arg(object.confidence, 0, 'f', 2));
    }

    QStringList gestures;
    for (const auto &gesture : snapshot.gestures) {
        gestures.push_back(QStringLiteral("%1(%2)")
                               .arg(gesture.name)
                               .arg(gesture.confidence, 0, 'f', 2));
    }

    infoFor(QStringLiteral("vision"), QStringLiteral("Vision snapshot received. source=\"%1\" node=\"%2\" trace=\"%3\" summary=\"%4\" objects=[%5] gestures=[%6]")
             .arg(source,
                  snapshot.nodeId,
                  snapshot.traceId,
                  snapshot.summary,
                  objects.join(QStringLiteral(", ")),
                  gestures.join(QStringLiteral(", "))));
}

void LoggingService::logVisionStatus(const QString &message, const QString &rateLimitKey, int intervalMs) const
{
    if (!shouldLogRateLimited(rateLimitKey, intervalMs)) {
        return;
    }
    infoFor(QStringLiteral("vision"), message);
}

void LoggingService::logVisionDrop(const QString &reason,
                                   const QString &detail,
                                   const QString &rateLimitKey,
                                   int intervalMs) const
{
    if (!shouldLogRateLimited(rateLimitKey, intervalMs)) {
        return;
    }

    infoFor(QStringLiteral("vision"), QStringLiteral("Vision snapshot dropped. snapshot_dropped_reason=\"%1\" detail=\"%2\"")
             .arg(reason.trimmed().isEmpty() ? QStringLiteral("unknown") : reason.trimmed(),
                  detail.trimmed()));
}

bool LoggingService::logBehaviorEvent(const BehaviorTraceEvent &event) const
{
    return m_behavioralLedger ? m_behavioralLedger->recordEvent(event) : false;
}

bool LoggingService::logFocusModeTransition(const FocusModeState &state,
                                            const QString &source,
                                            const QString &reasonCode) const
{
    BehaviorTraceEvent event = BehaviorTraceEvent::create(
        QStringLiteral("focus_mode"),
        QStringLiteral("state_transition"),
        reasonCode.trimmed().isEmpty() ? QStringLiteral("focus_mode.changed") : reasonCode.trimmed(),
        state.toVariantMap(),
        QStringLiteral("user"));
    event.capabilityId = QStringLiteral("focus_mode");
    event.threadId = QStringLiteral("companion::focus_mode");
    event.payload.insert(QStringLiteral("source"), source.trimmed().isEmpty() ? QStringLiteral("unknown") : source.trimmed());
    return logBehaviorEvent(event);
}

QVariantList LoggingService::recentBehaviorEvents(int limit) const
{
    QVariantList rows;
    if (!m_behavioralLedger) {
        return rows;
    }

    for (const BehaviorTraceEvent &event : m_behavioralLedger->recentEvents(limit)) {
        rows.push_back(event.toVariantMap());
    }
    return rows;
}

QString LoggingService::behaviorLedgerDatabasePath() const
{
    return m_behavioralLedger ? m_behavioralLedger->databasePath() : QString();
}

QString LoggingService::behaviorLedgerNdjsonPath() const
{
    return m_behavioralLedger ? m_behavioralLedger->ndjsonPath() : QString();
}

bool LoggingService::logAiExchange(const QString &prompt, const QString &response, const QString &source, const QString &status) const
{
    const QString root = QCoreApplication::applicationDirPath() + QStringLiteral("/logs/AI");
    if (!QDir().mkpath(root)) {
        return false;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString randomToken = QString::number(QRandomGenerator::global()->generate(), 16);
    const QString path = root + QStringLiteral("/") + timestamp + QStringLiteral("_") + randomToken + QStringLiteral(".log");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\n";
    out << "source=" << source << "\n";
    if (!status.trimmed().isEmpty()) {
        out << "status=" << status.trimmed() << "\n";
    }
    out << "\n[PROMPT]\n";
    out << prompt.trimmed() << "\n";
    out << "\n[RESPONSE]\n";
    out << response.trimmed() << "\n";
    out.flush();
    file.close();

    if (m_logger) {
        m_logger->info(QStringLiteral("AI exchange log written: %1").arg(path).toStdString());
    }
    if (m_aiLogger) {
        m_aiLogger->info(QStringLiteral("AI exchange log written: %1").arg(path).toStdString());
    }
    return true;
}

bool LoggingService::logAgentExchange(const QString &prompt,
                                      const QString &response,
                                      const QString &source,
                                      const AgentCapabilitySet &capabilities,
                                      const SamplingProfile &sampling,
                                      const QList<AgentTraceEntry> &trace,
                                      const QString &status) const
{
    const QString root = QCoreApplication::applicationDirPath() + QStringLiteral("/logs/AI");
    if (!QDir().mkpath(root)) {
        return false;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString randomToken = QString::number(QRandomGenerator::global()->generate(), 16);
    const QString path = root + QStringLiteral("/") + timestamp + QStringLiteral("_") + randomToken + QStringLiteral(".log");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\n";
    out << "source=" << source << "\n";
    out << "agent_provider_mode=" << capabilities.providerMode << "\n";
    out << "agent_enabled=" << (capabilities.agentEnabled ? "true" : "false") << "\n";
    out << "responses_api=" << (capabilities.responsesApi ? "true" : "false") << "\n";
    out << "tool_calling=" << (capabilities.toolCalling ? "true" : "false") << "\n";
    out << "selected_model_tool_capable=" << (capabilities.selectedModelToolCapable ? "true" : "false") << "\n";
    out << "conversation_temperature=" << sampling.conversationTemperature << "\n";
    if (sampling.conversationTopP.has_value()) {
        out << "conversation_top_p=" << *sampling.conversationTopP << "\n";
    }
    out << "tool_use_temperature=" << sampling.toolUseTemperature << "\n";
    if (sampling.providerTopK.has_value()) {
        out << "provider_top_k=" << *sampling.providerTopK << "\n";
    }
    out << "max_output_tokens=" << sampling.maxOutputTokens << "\n";
    if (!status.trimmed().isEmpty()) {
        out << "status=" << status.trimmed() << "\n";
    }
    out << "\n[TRACE]\n";
    for (const auto &entry : trace) {
        out << entry.timestamp << " | " << entry.kind << " | " << entry.title << " | "
            << (entry.success ? "ok" : "error") << " | " << entry.detail << "\n";
    }
    out << "\n[PROMPT]\n";
    out << prompt.trimmed() << "\n";
    out << "\n[RESPONSE]\n";
    out << response.trimmed() << "\n";
    out.flush();
    file.close();

    if (m_logger) {
        m_logger->info(QStringLiteral("Agent exchange log written: %1").arg(path).toStdString());
    }
    if (m_aiLogger) {
        m_aiLogger->info(QStringLiteral("Agent exchange log written: %1").arg(path).toStdString());
    }
    return true;
}

QString LoggingService::logFilePath() const
{
    return m_logFilePath;
}
