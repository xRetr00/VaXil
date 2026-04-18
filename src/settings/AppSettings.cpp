#include "settings/AppSettings.h"

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QVariantMap>

#include "core/PermissionOverrideSettings.h"
#include <nlohmann/json.hpp>

namespace {
constexpr double kMinVoiceSpeed = 0.85;
constexpr double kMaxVoiceSpeed = 0.92;
constexpr double kDefaultVoiceSpeed = 0.89;
constexpr double kMinVoicePitch = 0.90;
constexpr double kMaxVoicePitch = 0.97;
constexpr double kDefaultVoicePitch = 0.93;
constexpr double kLegacyPreciseThresholdDefault = 0.30;
constexpr int kLegacyPreciseCooldownDefault = 750;
constexpr double kSherpaWakeThresholdDefault = 0.80;
constexpr int kSherpaWakeCooldownDefault = 450;

QString settingsFilePath()
{
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);
    return root + QStringLiteral("/settings.json");
}

QString reasoningModeToString(ReasoningMode mode)
{
    switch (mode) {
    case ReasoningMode::Fast:
        return QStringLiteral("fast");
    case ReasoningMode::Balanced:
        return QStringLiteral("balanced");
    case ReasoningMode::Deep:
        return QStringLiteral("deep");
    }

    return QStringLiteral("balanced");
}

ReasoningMode reasoningModeFromString(const QString &value)
{
    if (value == QStringLiteral("fast")) {
        return ReasoningMode::Fast;
    }

    if (value == QStringLiteral("deep")) {
        return ReasoningMode::Deep;
    }

    return ReasoningMode::Balanced;
}

double clampVoiceSpeed(double value)
{
    return std::clamp(value, kMinVoiceSpeed, kMaxVoiceSpeed);
}

double clampVoicePitch(double value)
{
    return std::clamp(value, kMinVoicePitch, kMaxVoicePitch);
}

double clampWakeTriggerThreshold(double value)
{
    return std::clamp(value, 0.50, 1.0);
}

double clampTemperature(double value)
{
    return std::clamp(value, 0.0, 2.0);
}

int clampMaxOutputTokens(int value)
{
    return std::clamp(value, 64, 8192);
}

double clampVadSensitivity(double value)
{
    return std::clamp(value, 0.05, 0.95);
}

int clampWakeTriggerCooldownMs(int value)
{
    return std::clamp(value, 250, 1600);
}

int clampVisionTimeoutMs(int value)
{
    return std::clamp(value, 500, 60000);
}

int clampVisionStaleThresholdMs(int value)
{
    return std::clamp(value, 100, 10000);
}

int clampGestureCooldownMs(int value)
{
    return std::clamp(value, 100, 5000);
}

int clampGestureStabilityMs(int value)
{
    return std::clamp(value, 100, 1500);
}

int clampFocusModeDurationMinutes(int value)
{
    return std::clamp(value, 0, 24 * 60);
}

double clampLearningMaxAudioStorageGb(double value)
{
    return std::clamp(value, 0.25, 512.0);
}

int clampLearningRetentionDays(int value)
{
    return std::clamp(value, 1, 3650);
}

double clampVisionConfidence(double value, double fallback)
{
    if (std::isnan(value)) {
        return fallback;
    }
    return std::clamp(value, 0.05, 1.0);
}

QString normalizeUiMode(const QString &mode)
{
    const QString normalized = mode.trimmed().toLower();
    if (normalized == QStringLiteral("overlay")) {
        return QStringLiteral("overlay");
    }
    if (normalized == QStringLiteral("full")) {
        return QStringLiteral("full");
    }
    return QStringLiteral("full");
}

QVariantList permissionOverridesFromJson(const nlohmann::json &parsed)
{
    QVariantList rows;
    if (!parsed.contains("permissionOverrides") || !parsed.at("permissionOverrides").is_array()) {
        return rows;
    }

    for (const auto &entry : parsed.at("permissionOverrides")) {
        if (!entry.is_object()) {
            continue;
        }
        QVariantMap row;
        row.insert(QStringLiteral("capabilityId"),
                   QString::fromStdString(entry.value("capabilityId", std::string{})));
        row.insert(QStringLiteral("decision"),
                   QString::fromStdString(entry.value("decision", std::string{})));
        row.insert(QStringLiteral("scope"),
                   QString::fromStdString(entry.value("scope", std::string{})));
        row.insert(QStringLiteral("reasonCode"),
                   QString::fromStdString(entry.value("reasonCode", std::string{})));
        rows.push_back(row);
    }

    return PermissionOverrideSettings::sanitize(rows);
}

nlohmann::json permissionOverridesToJson(const QVariantList &overrides)
{
    nlohmann::json rows = nlohmann::json::array();
    for (const QVariant &value : PermissionOverrideSettings::sanitize(overrides)) {
        const QVariantMap row = value.toMap();
        rows.push_back({
            {"capabilityId", row.value(QStringLiteral("capabilityId")).toString().toStdString()},
            {"decision", row.value(QStringLiteral("decision")).toString().toStdString()},
            {"scope", row.value(QStringLiteral("scope")).toString().toStdString()},
            {"reasonCode", row.value(QStringLiteral("reasonCode")).toString().toStdString()}
        });
    }
    return rows;
}
}

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
    , m_chatBackendEndpoint(QStringLiteral("http://localhost:1234"))
    , m_lmStudioEndpoint(QStringLiteral("http://localhost:1234"))
{
}

bool AppSettings::load()
{
    QFile file(settingsFilePath());
    if (!file.exists()) {
        return save();
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const auto parsed = nlohmann::json::parse(file.readAll().constData(), nullptr, false);
    if (parsed.is_discarded()) {
        return false;
    }

    m_chatBackendKind = QString::fromStdString(parsed.value("chatBackendKind", m_chatBackendKind.toStdString())).trimmed().toLower();
    if (m_chatBackendKind.isEmpty()) {
        m_chatBackendKind = QStringLiteral("openai_compatible_local");
    }
    m_chatBackendEndpoint = QString::fromStdString(parsed.value("chatBackendEndpoint", std::string{}));
    m_chatBackendApiKey = QString::fromStdString(parsed.value("chatBackendApiKey", std::string{}));
    if (m_chatBackendEndpoint.isEmpty()) {
        m_chatBackendEndpoint = QString::fromStdString(parsed.value("lmStudioEndpoint", m_chatBackendEndpoint.toStdString()));
    }
    m_lmStudioEndpoint = m_chatBackendEndpoint;
    m_chatBackendModel = QString::fromStdString(parsed.value("chatBackendModel", std::string{}));
    if (m_chatBackendModel.isEmpty()) {
        m_chatBackendModel = QString::fromStdString(parsed.value("selectedModel", std::string{}));
    }
    m_selectedModel = m_chatBackendModel;
    m_defaultReasoningMode = reasoningModeFromString(QString::fromStdString(parsed.value("defaultReasoningMode", std::string("balanced"))));
    m_autoRoutingEnabled = parsed.value("autoRoutingEnabled", true);
    m_streamingEnabled = parsed.value("streamingEnabled", true);
    m_requestTimeoutMs = parsed.value("requestTimeoutMs", 12000);
    m_agentEnabled = parsed.value("agentEnabled", true);
    m_agentProviderMode = QString::fromStdString(parsed.value("agentProviderMode", m_agentProviderMode.toStdString()));
    m_conversationTemperature = clampTemperature(parsed.value("conversationTemperature", 0.7));
    if (parsed.contains("conversationTopP") && !parsed.at("conversationTopP").is_null()) {
        m_conversationTopP = parsed.at("conversationTopP").get<double>();
    } else {
        m_conversationTopP = 0.9;
    }
    m_toolUseTemperature = clampTemperature(parsed.value("toolUseTemperature", 0.2));
    if (parsed.contains("providerTopK") && !parsed.at("providerTopK").is_null()) {
        m_providerTopK = parsed.at("providerTopK").get<int>();
    } else {
        m_providerTopK.reset();
    }
    m_maxOutputTokens = clampMaxOutputTokens(parsed.value("maxOutputTokens", 1024));
    m_memoryAutoWrite = parsed.value("memoryAutoWrite", true);
    m_webSearchProvider = QString::fromStdString(parsed.value("webSearchProvider", m_webSearchProvider.toStdString()));
    m_braveSearchApiKey = QString::fromStdString(parsed.value("braveSearchApiKey", std::string{}));
    m_mcpEnabled = parsed.value("mcpEnabled", false);
    m_mcpCatalogUrl = QString::fromStdString(parsed.value("mcpCatalogUrl", std::string{}));
    m_mcpServerUrl = QString::fromStdString(parsed.value("mcpServerUrl", std::string{}));
    m_visionEnabled = parsed.value("visionEnabled", false);
    m_visionEndpoint = QString::fromStdString(parsed.value("visionEndpoint", m_visionEndpoint.toStdString()));
    if (m_visionEndpoint.trimmed().isEmpty()) {
        m_visionEndpoint = QStringLiteral("ws://0.0.0.0:8765/vision");
    }
    m_visionTimeoutMs = clampVisionTimeoutMs(parsed.value("visionTimeoutMs", 5000));
    m_visionStaleThresholdMs = clampVisionStaleThresholdMs(parsed.value("visionStaleThresholdMs", 2000));
    m_visionContextAlwaysOn = parsed.value("visionContextAlwaysOn", false);
    m_visionObjectsMinConfidence = clampVisionConfidence(parsed.value("visionObjectsMinConfidence", 0.60), 0.60);
    m_visionGesturesMinConfidence = clampVisionConfidence(parsed.value("visionGesturesMinConfidence", 0.70), 0.70);
    m_gestureEnabled = parsed.value("gestureEnabled", false);
    m_gestureStabilityMs = clampGestureStabilityMs(parsed.value("gestureStabilityMs", 180));
    m_gestureCooldownMs = clampGestureCooldownMs(parsed.value("gestureCooldownMs", 500));
    m_tracePanelEnabled = parsed.value("tracePanelEnabled", true);
    m_focusModeEnabled = parsed.value("focusModeEnabled", false);
    m_focusModeAllowCriticalAlerts = parsed.value("focusModeAllowCriticalAlerts", true);
    m_focusModeDurationMinutes = clampFocusModeDurationMinutes(parsed.value("focusModeDurationMinutes", 0));
    m_focusModeUntilEpochMs = parsed.value("focusModeUntilEpochMs", static_cast<qint64>(0));
    m_privateModeEnabled = parsed.value("privateModeEnabled", false);
    m_permissionOverrides = permissionOverridesFromJson(parsed);
    m_whisperExecutable = QString::fromStdString(parsed.value("whisperExecutable", std::string{}));
    m_whisperModelPath = QString::fromStdString(parsed.value("whisperModelPath", std::string{}));
    m_intentModelPath = QString::fromStdString(parsed.value("intentModelPath", std::string{}));
    m_selectedIntentModelId = QString::fromStdString(parsed.value("selectedIntentModelId", m_selectedIntentModelId.toStdString()));
    m_piperExecutable = QString::fromStdString(parsed.value("piperExecutable", std::string{}));
    m_piperVoiceModel = QString::fromStdString(parsed.value("piperVoiceModel", std::string{}));
    m_selectedVoicePresetId = QString::fromStdString(parsed.value("selectedVoicePresetId", m_selectedVoicePresetId.toStdString()));
    m_aecEnabled = parsed.value("aecEnabled", true);
    m_rnnoiseEnabled = parsed.value("rnnoiseEnabled", false);
    m_vadSensitivity = clampVadSensitivity(parsed.value("vadSensitivity", 0.55));
    const bool hasWakeTriggerThreshold = parsed.contains("wakeTriggerThreshold");
    const bool hasWakeTriggerCooldown = parsed.contains("wakeTriggerCooldownMs");
    const bool hasLegacyWakeSettings = (!hasWakeTriggerThreshold && parsed.contains("preciseTriggerThreshold"))
        || (!hasWakeTriggerCooldown && parsed.contains("preciseTriggerCooldownMs"))
        || parsed.contains("preciseEngineExecutable")
        || parsed.contains("preciseModelPath");
    double wakeTriggerThreshold = parsed.value("wakeTriggerThreshold", kSherpaWakeThresholdDefault);
    if (!hasWakeTriggerThreshold && parsed.contains("preciseTriggerThreshold")) {
        const double legacyThreshold = parsed.value("preciseTriggerThreshold", kLegacyPreciseThresholdDefault);
        wakeTriggerThreshold = std::abs(legacyThreshold - kLegacyPreciseThresholdDefault) < 0.0001
            ? kSherpaWakeThresholdDefault
            : legacyThreshold;
    }
    int wakeTriggerCooldownMs = parsed.value("wakeTriggerCooldownMs", kSherpaWakeCooldownDefault);
    if (!hasWakeTriggerCooldown && parsed.contains("preciseTriggerCooldownMs")) {
        const int legacyCooldownMs = parsed.value("preciseTriggerCooldownMs", kLegacyPreciseCooldownDefault);
        wakeTriggerCooldownMs = legacyCooldownMs == kLegacyPreciseCooldownDefault
            ? kSherpaWakeCooldownDefault
            : legacyCooldownMs;
    }
    const double wakeWordSensitivity = parsed.contains("wake_word_sensitivity")
        ? parsed.at("wake_word_sensitivity").get<double>()
        : (wakeTriggerThreshold < 0.50 ? kSherpaWakeThresholdDefault : wakeTriggerThreshold);
    m_wakeWordSensitivity = clampWakeTriggerThreshold(wakeWordSensitivity);
    m_wakeTriggerThreshold = m_wakeWordSensitivity;
    m_wakeTriggerCooldownMs = clampWakeTriggerCooldownMs(wakeTriggerCooldownMs);
    m_ffmpegExecutable = QString::fromStdString(parsed.value("ffmpegExecutable", std::string{}));
    m_ttsEngineKind = QString::fromStdString(parsed.value("ttsEngineKind", m_ttsEngineKind.toStdString()));
    m_voiceSpeed = clampVoiceSpeed(parsed.value("voiceSpeed", kDefaultVoiceSpeed));
    m_voicePitch = clampVoicePitch(parsed.value("voicePitch", kDefaultVoicePitch));
    m_micSensitivity = parsed.value("micSensitivity", 0.02);
    m_selectedAudioInputDeviceId = QString::fromStdString(parsed.value("selectedAudioInputDeviceId", std::string{}));
    m_selectedAudioOutputDeviceId = QString::fromStdString(parsed.value("selectedAudioOutputDeviceId", std::string{}));
    m_clickThroughEnabled = parsed.value("clickThroughEnabled", true);
    m_uiMode = normalizeUiMode(QString::fromStdString(parsed.value("uiMode", m_uiMode.toStdString())));
    m_initialSetupCompleted = parsed.value("initialSetupCompleted", false);
    m_wakeWordPhrase = QString::fromStdString(parsed.value(
        "wake_word",
        parsed.value("wakeWordPhrase", m_wakeWordPhrase.toStdString())));
    if (m_wakeWordPhrase.trimmed().isEmpty()) {
        m_wakeWordPhrase = QStringLiteral("Hey Vaxil");
    }
    m_wakeWordEnabled = parsed.value("wake_word_enabled", true);
    m_wakeEngineKind = QString::fromStdString(parsed.value("wakeEngineKind", m_wakeEngineKind.toStdString()));
    m_learningDataCollectionEnabled = parsed.value(
        "enable_learning_data_collection",
        parsed.value("learningDataCollectionEnabled", false));
    m_learningAudioCollectionEnabled = parsed.value(
        "enable_audio_collection",
        parsed.value("learningAudioCollectionEnabled", false));
    m_learningTranscriptCollectionEnabled = parsed.value(
        "enable_transcript_collection",
        parsed.value("learningTranscriptCollectionEnabled", false));
    m_learningToolLoggingEnabled = parsed.value(
        "enable_tool_logging",
        parsed.value("learningToolLoggingEnabled", false));
    m_learningBehaviorLoggingEnabled = parsed.value(
        "enable_behavior_logging",
        parsed.value("learningBehaviorLoggingEnabled", false));
    m_learningMemoryLoggingEnabled = parsed.value(
        "enable_memory_logging",
        parsed.value("learningMemoryLoggingEnabled", false));
    m_learningMaxAudioStorageGb = clampLearningMaxAudioStorageGb(parsed.value(
        "max_audio_storage_gb",
        parsed.value("learningMaxAudioStorageGb", 4.0)));
    m_learningMaxDaysToKeepAudio = clampLearningRetentionDays(parsed.value(
        "max_days_to_keep_audio",
        parsed.value("learningMaxDaysToKeepAudio", 30)));
    m_learningMaxDaysToKeepStructuredLogs = clampLearningRetentionDays(parsed.value(
        "max_days_to_keep_structured_logs",
        parsed.value("learningMaxDaysToKeepStructuredLogs", 90)));
    m_learningAllowPreparedDatasetExport = parsed.value(
        "allow_export_prepared_datasets",
        parsed.value("learningAllowPreparedDatasetExport", false));
    emit settingsChanged();
    if (hasLegacyWakeSettings) {
        (void)save();
    }
    return true;
}

bool AppSettings::save() const
{
    nlohmann::json json = {
        {"chatBackendKind", m_chatBackendKind.toStdString()},
        {"chatBackendEndpoint", m_chatBackendEndpoint.toStdString()},
        {"chatBackendApiKey", m_chatBackendApiKey.toStdString()},
        {"chatBackendModel", m_chatBackendModel.toStdString()},
        {"lmStudioEndpoint", m_lmStudioEndpoint.toStdString()},
        {"selectedModel", m_selectedModel.toStdString()},
        {"defaultReasoningMode", reasoningModeToString(m_defaultReasoningMode).toStdString()},
        {"autoRoutingEnabled", m_autoRoutingEnabled},
        {"streamingEnabled", m_streamingEnabled},
        {"requestTimeoutMs", m_requestTimeoutMs},
        {"agentEnabled", m_agentEnabled},
        {"agentProviderMode", m_agentProviderMode.toStdString()},
        {"conversationTemperature", m_conversationTemperature},
        {"conversationTopP", m_conversationTopP.has_value() ? nlohmann::json(*m_conversationTopP) : nlohmann::json(nullptr)},
        {"toolUseTemperature", m_toolUseTemperature},
        {"providerTopK", m_providerTopK.has_value() ? nlohmann::json(*m_providerTopK) : nlohmann::json(nullptr)},
        {"maxOutputTokens", m_maxOutputTokens},
        {"memoryAutoWrite", m_memoryAutoWrite},
        {"webSearchProvider", m_webSearchProvider.toStdString()},
        {"braveSearchApiKey", m_braveSearchApiKey.toStdString()},
        {"mcpEnabled", m_mcpEnabled},
        {"mcpCatalogUrl", m_mcpCatalogUrl.toStdString()},
        {"mcpServerUrl", m_mcpServerUrl.toStdString()},
        {"visionEnabled", m_visionEnabled},
        {"visionEndpoint", m_visionEndpoint.toStdString()},
        {"visionTimeoutMs", m_visionTimeoutMs},
        {"visionStaleThresholdMs", m_visionStaleThresholdMs},
        {"visionContextAlwaysOn", m_visionContextAlwaysOn},
        {"visionObjectsMinConfidence", m_visionObjectsMinConfidence},
        {"visionGesturesMinConfidence", m_visionGesturesMinConfidence},
        {"gestureEnabled", m_gestureEnabled},
        {"gestureStabilityMs", m_gestureStabilityMs},
        {"gestureCooldownMs", m_gestureCooldownMs},
        {"tracePanelEnabled", m_tracePanelEnabled},
        {"focusModeEnabled", m_focusModeEnabled},
        {"focusModeAllowCriticalAlerts", m_focusModeAllowCriticalAlerts},
        {"focusModeDurationMinutes", m_focusModeDurationMinutes},
        {"focusModeUntilEpochMs", m_focusModeUntilEpochMs},
        {"privateModeEnabled", m_privateModeEnabled},
        {"permissionOverrides", permissionOverridesToJson(m_permissionOverrides)},
        {"whisperExecutable", m_whisperExecutable.toStdString()},
        {"whisperModelPath", m_whisperModelPath.toStdString()},
        {"intentModelPath", m_intentModelPath.toStdString()},
        {"selectedIntentModelId", m_selectedIntentModelId.toStdString()},
        {"piperExecutable", m_piperExecutable.toStdString()},
        {"piperVoiceModel", m_piperVoiceModel.toStdString()},
        {"selectedVoicePresetId", m_selectedVoicePresetId.toStdString()},
        {"aecEnabled", m_aecEnabled},
        {"rnnoiseEnabled", m_rnnoiseEnabled},
        {"vadSensitivity", m_vadSensitivity},
        {"wakeTriggerThreshold", m_wakeTriggerThreshold},
        {"wakeTriggerCooldownMs", m_wakeTriggerCooldownMs},
        {"ffmpegExecutable", m_ffmpegExecutable.toStdString()},
        {"ttsEngineKind", m_ttsEngineKind.toStdString()},
        {"voiceSpeed", m_voiceSpeed},
        {"voicePitch", m_voicePitch},
        {"micSensitivity", m_micSensitivity},
        {"selectedAudioInputDeviceId", m_selectedAudioInputDeviceId.toStdString()},
        {"selectedAudioOutputDeviceId", m_selectedAudioOutputDeviceId.toStdString()},
        {"clickThroughEnabled", m_clickThroughEnabled},
        {"uiMode", m_uiMode.toStdString()},
        {"initialSetupCompleted", m_initialSetupCompleted},
        {"wake_word", m_wakeWordPhrase.toStdString()},
        {"wake_word_enabled", m_wakeWordEnabled},
        {"wake_word_sensitivity", m_wakeWordSensitivity},
        {"wakeWordPhrase", m_wakeWordPhrase.toStdString()},
        {"wakeEngineKind", m_wakeEngineKind.toStdString()},
        {"enable_learning_data_collection", m_learningDataCollectionEnabled},
        {"enable_audio_collection", m_learningAudioCollectionEnabled},
        {"enable_transcript_collection", m_learningTranscriptCollectionEnabled},
        {"enable_tool_logging", m_learningToolLoggingEnabled},
        {"enable_behavior_logging", m_learningBehaviorLoggingEnabled},
        {"enable_memory_logging", m_learningMemoryLoggingEnabled},
        {"max_audio_storage_gb", m_learningMaxAudioStorageGb},
        {"max_days_to_keep_audio", m_learningMaxDaysToKeepAudio},
        {"max_days_to_keep_structured_logs", m_learningMaxDaysToKeepStructuredLogs},
        {"allow_export_prepared_datasets", m_learningAllowPreparedDatasetExport}
    };

    QFile file(settingsFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    file.write(QByteArray::fromStdString(json.dump(2)));
    return true;
}

QString AppSettings::chatBackendKind() const { return m_chatBackendKind; }
void AppSettings::setChatBackendKind(const QString &kind)
{
    const QString normalized = kind.trimmed().toLower();
    m_chatBackendKind = normalized.isEmpty() ? QStringLiteral("openai_compatible_local") : normalized;
    emit settingsChanged();
}
QString AppSettings::chatBackendEndpoint() const { return m_chatBackendEndpoint; }
void AppSettings::setChatBackendEndpoint(const QString &endpoint)
{
    m_chatBackendEndpoint = endpoint;
    m_lmStudioEndpoint = endpoint;
    emit settingsChanged();
}
QString AppSettings::chatBackendApiKey() const { return m_chatBackendApiKey; }
void AppSettings::setChatBackendApiKey(const QString &apiKey)
{
    m_chatBackendApiKey = apiKey.trimmed();
    emit settingsChanged();
}
QString AppSettings::chatBackendModel() const { return m_chatBackendModel; }
void AppSettings::setChatBackendModel(const QString &modelId)
{
    const QString normalizedModel = modelId.trimmed();
    m_chatBackendModel = normalizedModel;
    m_selectedModel = normalizedModel;
    emit settingsChanged();
}
QString AppSettings::lmStudioEndpoint() const { return m_lmStudioEndpoint; }
void AppSettings::setLmStudioEndpoint(const QString &endpoint)
{
    m_lmStudioEndpoint = endpoint;
    m_chatBackendEndpoint = endpoint;
    emit settingsChanged();
}
QString AppSettings::selectedModel() const { return m_selectedModel; }
void AppSettings::setSelectedModel(const QString &modelId)
{
    m_selectedModel = modelId;
    m_chatBackendModel = modelId;
    emit settingsChanged();
}
ReasoningMode AppSettings::defaultReasoningMode() const { return m_defaultReasoningMode; }
void AppSettings::setDefaultReasoningMode(ReasoningMode mode) { m_defaultReasoningMode = mode; emit settingsChanged(); }
bool AppSettings::autoRoutingEnabled() const { return m_autoRoutingEnabled; }
void AppSettings::setAutoRoutingEnabled(bool enabled) { m_autoRoutingEnabled = enabled; emit settingsChanged(); }
bool AppSettings::streamingEnabled() const { return m_streamingEnabled; }
void AppSettings::setStreamingEnabled(bool enabled) { m_streamingEnabled = enabled; emit settingsChanged(); }
int AppSettings::requestTimeoutMs() const { return m_requestTimeoutMs; }
void AppSettings::setRequestTimeoutMs(int timeoutMs) { m_requestTimeoutMs = timeoutMs; emit settingsChanged(); }
bool AppSettings::agentEnabled() const { return m_agentEnabled; }
void AppSettings::setAgentEnabled(bool enabled) { m_agentEnabled = enabled; emit settingsChanged(); }
QString AppSettings::agentProviderMode() const { return m_agentProviderMode; }
void AppSettings::setAgentProviderMode(const QString &mode)
{
    m_agentProviderMode = mode.trimmed().isEmpty() ? QStringLiteral("auto") : mode.trimmed();
    emit settingsChanged();
}
double AppSettings::conversationTemperature() const { return m_conversationTemperature; }
void AppSettings::setConversationTemperature(double temperature)
{
    m_conversationTemperature = clampTemperature(temperature);
    emit settingsChanged();
}
std::optional<double> AppSettings::conversationTopP() const { return m_conversationTopP; }
void AppSettings::setConversationTopP(const std::optional<double> &topP)
{
    m_conversationTopP = topP;
    emit settingsChanged();
}
double AppSettings::toolUseTemperature() const { return m_toolUseTemperature; }
void AppSettings::setToolUseTemperature(double temperature)
{
    m_toolUseTemperature = clampTemperature(temperature);
    emit settingsChanged();
}
std::optional<int> AppSettings::providerTopK() const { return m_providerTopK; }
void AppSettings::setProviderTopK(const std::optional<int> &topK)
{
    m_providerTopK = topK;
    emit settingsChanged();
}
int AppSettings::maxOutputTokens() const { return m_maxOutputTokens; }
void AppSettings::setMaxOutputTokens(int maxTokens)
{
    m_maxOutputTokens = clampMaxOutputTokens(maxTokens);
    emit settingsChanged();
}
bool AppSettings::memoryAutoWrite() const { return m_memoryAutoWrite; }
void AppSettings::setMemoryAutoWrite(bool enabled) { m_memoryAutoWrite = enabled; emit settingsChanged(); }
QString AppSettings::webSearchProvider() const { return m_webSearchProvider; }
void AppSettings::setWebSearchProvider(const QString &provider)
{
    m_webSearchProvider = provider.trimmed().isEmpty() ? QStringLiteral("brave") : provider.trimmed();
    emit settingsChanged();
}
QString AppSettings::braveSearchApiKey() const { return m_braveSearchApiKey; }
void AppSettings::setBraveSearchApiKey(const QString &apiKey)
{
    m_braveSearchApiKey = apiKey.trimmed();
    emit settingsChanged();
}
bool AppSettings::mcpEnabled() const { return m_mcpEnabled; }
void AppSettings::setMcpEnabled(bool enabled) { m_mcpEnabled = enabled; emit settingsChanged(); }
QString AppSettings::mcpCatalogUrl() const { return m_mcpCatalogUrl; }
void AppSettings::setMcpCatalogUrl(const QString &url)
{
    m_mcpCatalogUrl = url.trimmed();
    emit settingsChanged();
}
QString AppSettings::mcpServerUrl() const { return m_mcpServerUrl; }
void AppSettings::setMcpServerUrl(const QString &url)
{
    m_mcpServerUrl = url.trimmed();
    emit settingsChanged();
}
bool AppSettings::visionEnabled() const { return m_visionEnabled; }
void AppSettings::setVisionEnabled(bool enabled)
{
    m_visionEnabled = enabled;
    emit settingsChanged();
}
QString AppSettings::visionEndpoint() const { return m_visionEndpoint; }
void AppSettings::setVisionEndpoint(const QString &endpoint)
{
    m_visionEndpoint = endpoint.trimmed().isEmpty()
        ? QStringLiteral("ws://0.0.0.0:8765/vision")
        : endpoint.trimmed();
    emit settingsChanged();
}
int AppSettings::visionTimeoutMs() const { return m_visionTimeoutMs; }
void AppSettings::setVisionTimeoutMs(int timeoutMs)
{
    m_visionTimeoutMs = clampVisionTimeoutMs(timeoutMs);
    emit settingsChanged();
}
int AppSettings::visionStaleThresholdMs() const { return m_visionStaleThresholdMs; }
void AppSettings::setVisionStaleThresholdMs(int thresholdMs)
{
    m_visionStaleThresholdMs = clampVisionStaleThresholdMs(thresholdMs);
    emit settingsChanged();
}
bool AppSettings::visionContextAlwaysOn() const { return m_visionContextAlwaysOn; }
void AppSettings::setVisionContextAlwaysOn(bool enabled)
{
    m_visionContextAlwaysOn = enabled;
    emit settingsChanged();
}
double AppSettings::visionObjectsMinConfidence() const { return m_visionObjectsMinConfidence; }
void AppSettings::setVisionObjectsMinConfidence(double confidence)
{
    m_visionObjectsMinConfidence = clampVisionConfidence(confidence, 0.60);
    emit settingsChanged();
}
double AppSettings::visionGesturesMinConfidence() const { return m_visionGesturesMinConfidence; }
void AppSettings::setVisionGesturesMinConfidence(double confidence)
{
    m_visionGesturesMinConfidence = clampVisionConfidence(confidence, 0.70);
    emit settingsChanged();
}
bool AppSettings::gestureEnabled() const { return m_gestureEnabled; }
void AppSettings::setGestureEnabled(bool enabled)
{
    m_gestureEnabled = enabled;
    emit settingsChanged();
}
int AppSettings::gestureStabilityMs() const { return m_gestureStabilityMs; }
void AppSettings::setGestureStabilityMs(int stabilityMs)
{
    m_gestureStabilityMs = clampGestureStabilityMs(stabilityMs);
    emit settingsChanged();
}
int AppSettings::gestureCooldownMs() const { return m_gestureCooldownMs; }
void AppSettings::setGestureCooldownMs(int cooldownMs)
{
    m_gestureCooldownMs = clampGestureCooldownMs(cooldownMs);
    emit settingsChanged();
}
bool AppSettings::tracePanelEnabled() const { return m_tracePanelEnabled; }
void AppSettings::setTracePanelEnabled(bool enabled) { m_tracePanelEnabled = enabled; emit settingsChanged(); }
bool AppSettings::focusModeEnabled() const { return m_focusModeEnabled; }
void AppSettings::setFocusModeEnabled(bool enabled) { m_focusModeEnabled = enabled; emit settingsChanged(); }
bool AppSettings::focusModeAllowCriticalAlerts() const { return m_focusModeAllowCriticalAlerts; }
void AppSettings::setFocusModeAllowCriticalAlerts(bool enabled) { m_focusModeAllowCriticalAlerts = enabled; emit settingsChanged(); }
int AppSettings::focusModeDurationMinutes() const { return m_focusModeDurationMinutes; }
void AppSettings::setFocusModeDurationMinutes(int minutes) { m_focusModeDurationMinutes = clampFocusModeDurationMinutes(minutes); emit settingsChanged(); }
qint64 AppSettings::focusModeUntilEpochMs() const { return m_focusModeUntilEpochMs; }
void AppSettings::setFocusModeUntilEpochMs(qint64 epochMs) { m_focusModeUntilEpochMs = std::max<qint64>(0, epochMs); emit settingsChanged(); }
bool AppSettings::privateModeEnabled() const { return m_privateModeEnabled; }
void AppSettings::setPrivateModeEnabled(bool enabled) { m_privateModeEnabled = enabled; emit settingsChanged(); }
QVariantList AppSettings::permissionOverrides() const { return m_permissionOverrides; }
void AppSettings::setPermissionOverrides(const QVariantList &overrides)
{
    m_permissionOverrides = PermissionOverrideSettings::sanitize(overrides);
    emit settingsChanged();
}
QString AppSettings::whisperExecutable() const { return m_whisperExecutable; }
void AppSettings::setWhisperExecutable(const QString &path) { m_whisperExecutable = path; emit settingsChanged(); }
QString AppSettings::whisperModelPath() const { return m_whisperModelPath; }
void AppSettings::setWhisperModelPath(const QString &path) { m_whisperModelPath = path; emit settingsChanged(); }
QString AppSettings::intentModelPath() const { return m_intentModelPath; }
void AppSettings::setIntentModelPath(const QString &path) { m_intentModelPath = path; emit settingsChanged(); }
QString AppSettings::selectedIntentModelId() const { return m_selectedIntentModelId; }
void AppSettings::setSelectedIntentModelId(const QString &modelId)
{
    m_selectedIntentModelId = modelId.trimmed().isEmpty() ? QStringLiteral("intent-minilm-int8") : modelId.trimmed();
    emit settingsChanged();
}
QString AppSettings::piperExecutable() const { return m_piperExecutable; }
void AppSettings::setPiperExecutable(const QString &path) { m_piperExecutable = path; emit settingsChanged(); }
QString AppSettings::piperVoiceModel() const { return m_piperVoiceModel; }
void AppSettings::setPiperVoiceModel(const QString &path) { m_piperVoiceModel = path; emit settingsChanged(); }
QString AppSettings::selectedVoicePresetId() const { return m_selectedVoicePresetId; }
void AppSettings::setSelectedVoicePresetId(const QString &voicePresetId) { m_selectedVoicePresetId = voicePresetId; emit settingsChanged(); }
bool AppSettings::aecEnabled() const { return m_aecEnabled; }
void AppSettings::setAecEnabled(bool enabled) { m_aecEnabled = enabled; emit settingsChanged(); }
bool AppSettings::rnnoiseEnabled() const { return m_rnnoiseEnabled; }
void AppSettings::setRnnoiseEnabled(bool enabled) { m_rnnoiseEnabled = enabled; emit settingsChanged(); }
double AppSettings::vadSensitivity() const { return m_vadSensitivity; }
void AppSettings::setVadSensitivity(double sensitivity) { m_vadSensitivity = clampVadSensitivity(sensitivity); emit settingsChanged(); }
double AppSettings::wakeTriggerThreshold() const { return m_wakeTriggerThreshold; }
void AppSettings::setWakeTriggerThreshold(double threshold)
{
    m_wakeTriggerThreshold = clampWakeTriggerThreshold(threshold);
    m_wakeWordSensitivity = m_wakeTriggerThreshold;
    emit settingsChanged();
}
int AppSettings::wakeTriggerCooldownMs() const { return m_wakeTriggerCooldownMs; }
void AppSettings::setWakeTriggerCooldownMs(int cooldownMs) { m_wakeTriggerCooldownMs = clampWakeTriggerCooldownMs(cooldownMs); emit settingsChanged(); }
QString AppSettings::ffmpegExecutable() const { return m_ffmpegExecutable; }
void AppSettings::setFfmpegExecutable(const QString &path) { m_ffmpegExecutable = path; emit settingsChanged(); }
QString AppSettings::ttsEngineKind() const { return m_ttsEngineKind; }
void AppSettings::setTtsEngineKind(const QString &kind)
{
    m_ttsEngineKind = kind.trimmed().isEmpty() ? QStringLiteral("piper") : kind.trimmed();
    emit settingsChanged();
}
double AppSettings::voiceSpeed() const { return m_voiceSpeed; }
void AppSettings::setVoiceSpeed(double speed) { m_voiceSpeed = clampVoiceSpeed(speed); emit settingsChanged(); }
double AppSettings::voicePitch() const { return m_voicePitch; }
void AppSettings::setVoicePitch(double pitch) { m_voicePitch = clampVoicePitch(pitch); emit settingsChanged(); }
double AppSettings::micSensitivity() const { return m_micSensitivity; }
void AppSettings::setMicSensitivity(double sensitivity) { m_micSensitivity = sensitivity; emit settingsChanged(); }
QString AppSettings::selectedAudioInputDeviceId() const { return m_selectedAudioInputDeviceId; }
void AppSettings::setSelectedAudioInputDeviceId(const QString &deviceId) { m_selectedAudioInputDeviceId = deviceId; emit settingsChanged(); }
QString AppSettings::selectedAudioOutputDeviceId() const { return m_selectedAudioOutputDeviceId; }
void AppSettings::setSelectedAudioOutputDeviceId(const QString &deviceId) { m_selectedAudioOutputDeviceId = deviceId; emit settingsChanged(); }
bool AppSettings::clickThroughEnabled() const { return m_clickThroughEnabled; }
void AppSettings::setClickThroughEnabled(bool enabled) { m_clickThroughEnabled = enabled; emit settingsChanged(); }
QString AppSettings::uiMode() const { return m_uiMode; }
void AppSettings::setUiMode(const QString &mode)
{
    m_uiMode = normalizeUiMode(mode);
    emit settingsChanged();
}
bool AppSettings::initialSetupCompleted() const { return m_initialSetupCompleted; }
void AppSettings::setInitialSetupCompleted(bool completed) { m_initialSetupCompleted = completed; emit settingsChanged(); }
QString AppSettings::wakeWordPhrase() const { return m_wakeWordPhrase; }
void AppSettings::setWakeWordPhrase(const QString &wakeWordPhrase)
{
    m_wakeWordPhrase = wakeWordPhrase.trimmed().isEmpty() ? QStringLiteral("Hey Vaxil") : wakeWordPhrase.trimmed();
    emit settingsChanged();
}
bool AppSettings::wakeWordEnabled() const { return m_wakeWordEnabled; }
void AppSettings::setWakeWordEnabled(bool enabled)
{
    m_wakeWordEnabled = enabled;
    emit settingsChanged();
}
double AppSettings::wakeWordSensitivity() const { return m_wakeWordSensitivity; }
void AppSettings::setWakeWordSensitivity(double sensitivity)
{
    m_wakeWordSensitivity = clampWakeTriggerThreshold(sensitivity);
    m_wakeTriggerThreshold = m_wakeWordSensitivity;
    emit settingsChanged();
}
QString AppSettings::wakeEngineKind() const { return m_wakeEngineKind; }
void AppSettings::setWakeEngineKind(const QString &kind)
{
    m_wakeEngineKind = kind.trimmed().isEmpty() ? QStringLiteral("sherpa-onnx") : kind.trimmed();
    emit settingsChanged();
}
bool AppSettings::learningDataCollectionEnabled() const { return m_learningDataCollectionEnabled; }
void AppSettings::setLearningDataCollectionEnabled(bool enabled)
{
    m_learningDataCollectionEnabled = enabled;
    emit settingsChanged();
}
bool AppSettings::learningAudioCollectionEnabled() const { return m_learningAudioCollectionEnabled; }
void AppSettings::setLearningAudioCollectionEnabled(bool enabled)
{
    m_learningAudioCollectionEnabled = enabled;
    emit settingsChanged();
}
bool AppSettings::learningTranscriptCollectionEnabled() const { return m_learningTranscriptCollectionEnabled; }
void AppSettings::setLearningTranscriptCollectionEnabled(bool enabled)
{
    m_learningTranscriptCollectionEnabled = enabled;
    emit settingsChanged();
}
bool AppSettings::learningToolLoggingEnabled() const { return m_learningToolLoggingEnabled; }
void AppSettings::setLearningToolLoggingEnabled(bool enabled)
{
    m_learningToolLoggingEnabled = enabled;
    emit settingsChanged();
}
bool AppSettings::learningBehaviorLoggingEnabled() const { return m_learningBehaviorLoggingEnabled; }
void AppSettings::setLearningBehaviorLoggingEnabled(bool enabled)
{
    m_learningBehaviorLoggingEnabled = enabled;
    emit settingsChanged();
}
bool AppSettings::learningMemoryLoggingEnabled() const { return m_learningMemoryLoggingEnabled; }
void AppSettings::setLearningMemoryLoggingEnabled(bool enabled)
{
    m_learningMemoryLoggingEnabled = enabled;
    emit settingsChanged();
}
double AppSettings::learningMaxAudioStorageGb() const { return m_learningMaxAudioStorageGb; }
void AppSettings::setLearningMaxAudioStorageGb(double value)
{
    m_learningMaxAudioStorageGb = clampLearningMaxAudioStorageGb(value);
    emit settingsChanged();
}
int AppSettings::learningMaxDaysToKeepAudio() const { return m_learningMaxDaysToKeepAudio; }
void AppSettings::setLearningMaxDaysToKeepAudio(int days)
{
    m_learningMaxDaysToKeepAudio = clampLearningRetentionDays(days);
    emit settingsChanged();
}
int AppSettings::learningMaxDaysToKeepStructuredLogs() const { return m_learningMaxDaysToKeepStructuredLogs; }
void AppSettings::setLearningMaxDaysToKeepStructuredLogs(int days)
{
    m_learningMaxDaysToKeepStructuredLogs = clampLearningRetentionDays(days);
    emit settingsChanged();
}
bool AppSettings::learningAllowPreparedDatasetExport() const { return m_learningAllowPreparedDatasetExport; }
void AppSettings::setLearningAllowPreparedDatasetExport(bool enabled)
{
    m_learningAllowPreparedDatasetExport = enabled;
    emit settingsChanged();
}
QString AppSettings::storagePath() const { return settingsFilePath(); }
