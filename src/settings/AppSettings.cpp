#include "settings/AppSettings.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <nlohmann/json.hpp>

namespace {
constexpr double kMinVoiceSpeed = 0.85;
constexpr double kMaxVoiceSpeed = 0.92;
constexpr double kDefaultVoiceSpeed = 0.89;
constexpr double kMinVoicePitch = 0.90;
constexpr double kMaxVoicePitch = 0.97;
constexpr double kDefaultVoicePitch = 0.93;

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

double clampPreciseTriggerThreshold(double value)
{
    return std::clamp(value, 0.30, 0.85);
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

int clampPreciseTriggerCooldownMs(int value)
{
    return std::clamp(value, 600, 900);
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

    m_chatBackendKind = QString::fromStdString(parsed.value("chatBackendKind", m_chatBackendKind.toStdString()));
    m_chatBackendEndpoint = QString::fromStdString(parsed.value("chatBackendEndpoint", std::string{}));
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
    m_mcpEnabled = parsed.value("mcpEnabled", false);
    m_mcpCatalogUrl = QString::fromStdString(parsed.value("mcpCatalogUrl", std::string{}));
    m_mcpServerUrl = QString::fromStdString(parsed.value("mcpServerUrl", std::string{}));
    m_tracePanelEnabled = parsed.value("tracePanelEnabled", true);
    m_whisperExecutable = QString::fromStdString(parsed.value("whisperExecutable", std::string{}));
    m_whisperModelPath = QString::fromStdString(parsed.value("whisperModelPath", std::string{}));
    m_intentModelPath = QString::fromStdString(parsed.value("intentModelPath", std::string{}));
    m_selectedIntentModelId = QString::fromStdString(parsed.value("selectedIntentModelId", m_selectedIntentModelId.toStdString()));
    m_piperExecutable = QString::fromStdString(parsed.value("piperExecutable", std::string{}));
    m_piperVoiceModel = QString::fromStdString(parsed.value("piperVoiceModel", std::string{}));
    m_selectedVoicePresetId = QString::fromStdString(parsed.value("selectedVoicePresetId", m_selectedVoicePresetId.toStdString()));
    m_preciseEngineExecutable = QString::fromStdString(parsed.value("preciseEngineExecutable", std::string{}));
    m_preciseModelPath = QString::fromStdString(parsed.value("preciseModelPath", std::string{}));
    m_aecEnabled = parsed.value("aecEnabled", true);
    m_rnnoiseEnabled = parsed.value("rnnoiseEnabled", false);
    m_vadSensitivity = clampVadSensitivity(parsed.value("vadSensitivity", 0.55));
    m_preciseTriggerThreshold = clampPreciseTriggerThreshold(parsed.value("preciseTriggerThreshold", 0.30));
    m_preciseTriggerCooldownMs = clampPreciseTriggerCooldownMs(parsed.value("preciseTriggerCooldownMs", 750));
    m_ffmpegExecutable = QString::fromStdString(parsed.value("ffmpegExecutable", std::string{}));
    m_ttsEngineKind = QString::fromStdString(parsed.value("ttsEngineKind", m_ttsEngineKind.toStdString()));
    m_voiceSpeed = clampVoiceSpeed(parsed.value("voiceSpeed", kDefaultVoiceSpeed));
    m_voicePitch = clampVoicePitch(parsed.value("voicePitch", kDefaultVoicePitch));
    m_micSensitivity = parsed.value("micSensitivity", 0.02);
    m_selectedAudioInputDeviceId = QString::fromStdString(parsed.value("selectedAudioInputDeviceId", std::string{}));
    m_selectedAudioOutputDeviceId = QString::fromStdString(parsed.value("selectedAudioOutputDeviceId", std::string{}));
    m_clickThroughEnabled = parsed.value("clickThroughEnabled", true);
    m_initialSetupCompleted = parsed.value("initialSetupCompleted", false);
    m_wakeWordPhrase = QString::fromStdString(parsed.value("wakeWordPhrase", m_wakeWordPhrase.toStdString()));
    m_wakeEngineKind = QString::fromStdString(parsed.value("wakeEngineKind", m_wakeEngineKind.toStdString()));
    emit settingsChanged();
    return true;
}

bool AppSettings::save() const
{
    nlohmann::json json = {
        {"chatBackendKind", m_chatBackendKind.toStdString()},
        {"chatBackendEndpoint", m_chatBackendEndpoint.toStdString()},
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
        {"mcpEnabled", m_mcpEnabled},
        {"mcpCatalogUrl", m_mcpCatalogUrl.toStdString()},
        {"mcpServerUrl", m_mcpServerUrl.toStdString()},
        {"tracePanelEnabled", m_tracePanelEnabled},
        {"whisperExecutable", m_whisperExecutable.toStdString()},
        {"whisperModelPath", m_whisperModelPath.toStdString()},
        {"intentModelPath", m_intentModelPath.toStdString()},
        {"selectedIntentModelId", m_selectedIntentModelId.toStdString()},
        {"piperExecutable", m_piperExecutable.toStdString()},
        {"piperVoiceModel", m_piperVoiceModel.toStdString()},
        {"selectedVoicePresetId", m_selectedVoicePresetId.toStdString()},
        {"preciseEngineExecutable", m_preciseEngineExecutable.toStdString()},
        {"preciseModelPath", m_preciseModelPath.toStdString()},
        {"aecEnabled", m_aecEnabled},
        {"rnnoiseEnabled", m_rnnoiseEnabled},
        {"vadSensitivity", m_vadSensitivity},
        {"preciseTriggerThreshold", m_preciseTriggerThreshold},
        {"preciseTriggerCooldownMs", m_preciseTriggerCooldownMs},
        {"ffmpegExecutable", m_ffmpegExecutable.toStdString()},
        {"ttsEngineKind", m_ttsEngineKind.toStdString()},
        {"voiceSpeed", m_voiceSpeed},
        {"voicePitch", m_voicePitch},
        {"micSensitivity", m_micSensitivity},
        {"selectedAudioInputDeviceId", m_selectedAudioInputDeviceId.toStdString()},
        {"selectedAudioOutputDeviceId", m_selectedAudioOutputDeviceId.toStdString()},
        {"clickThroughEnabled", m_clickThroughEnabled},
        {"initialSetupCompleted", m_initialSetupCompleted},
        {"wakeWordPhrase", m_wakeWordPhrase.toStdString()},
        {"wakeEngineKind", m_wakeEngineKind.toStdString()}
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
    m_chatBackendKind = kind.trimmed().isEmpty() ? QStringLiteral("openai_compatible_local") : kind.trimmed();
    emit settingsChanged();
}
QString AppSettings::chatBackendEndpoint() const { return m_chatBackendEndpoint; }
void AppSettings::setChatBackendEndpoint(const QString &endpoint)
{
    m_chatBackendEndpoint = endpoint;
    m_lmStudioEndpoint = endpoint;
    emit settingsChanged();
}
QString AppSettings::chatBackendModel() const { return m_chatBackendModel; }
void AppSettings::setChatBackendModel(const QString &modelId)
{
    m_chatBackendModel = modelId;
    m_selectedModel = modelId;
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
bool AppSettings::tracePanelEnabled() const { return m_tracePanelEnabled; }
void AppSettings::setTracePanelEnabled(bool enabled) { m_tracePanelEnabled = enabled; emit settingsChanged(); }
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
QString AppSettings::preciseEngineExecutable() const { return m_preciseEngineExecutable; }
void AppSettings::setPreciseEngineExecutable(const QString &path) { m_preciseEngineExecutable = path; emit settingsChanged(); }
QString AppSettings::preciseModelPath() const { return m_preciseModelPath; }
void AppSettings::setPreciseModelPath(const QString &path) { m_preciseModelPath = path; emit settingsChanged(); }
bool AppSettings::aecEnabled() const { return m_aecEnabled; }
void AppSettings::setAecEnabled(bool enabled) { m_aecEnabled = enabled; emit settingsChanged(); }
bool AppSettings::rnnoiseEnabled() const { return m_rnnoiseEnabled; }
void AppSettings::setRnnoiseEnabled(bool enabled) { m_rnnoiseEnabled = enabled; emit settingsChanged(); }
double AppSettings::vadSensitivity() const { return m_vadSensitivity; }
void AppSettings::setVadSensitivity(double sensitivity) { m_vadSensitivity = clampVadSensitivity(sensitivity); emit settingsChanged(); }
double AppSettings::preciseTriggerThreshold() const { return m_preciseTriggerThreshold; }
void AppSettings::setPreciseTriggerThreshold(double threshold) { m_preciseTriggerThreshold = clampPreciseTriggerThreshold(threshold); emit settingsChanged(); }
int AppSettings::preciseTriggerCooldownMs() const { return m_preciseTriggerCooldownMs; }
void AppSettings::setPreciseTriggerCooldownMs(int cooldownMs) { m_preciseTriggerCooldownMs = clampPreciseTriggerCooldownMs(cooldownMs); emit settingsChanged(); }
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
bool AppSettings::initialSetupCompleted() const { return m_initialSetupCompleted; }
void AppSettings::setInitialSetupCompleted(bool completed) { m_initialSetupCompleted = completed; emit settingsChanged(); }
QString AppSettings::wakeWordPhrase() const { return m_wakeWordPhrase; }
void AppSettings::setWakeWordPhrase(const QString &wakeWordPhrase)
{
    m_wakeWordPhrase = wakeWordPhrase.trimmed().isEmpty() ? QStringLiteral("Jarvis") : wakeWordPhrase.trimmed();
    emit settingsChanged();
}
QString AppSettings::wakeEngineKind() const { return m_wakeEngineKind; }
void AppSettings::setWakeEngineKind(const QString &kind)
{
    m_wakeEngineKind = kind.trimmed().isEmpty() ? QStringLiteral("sherpa-onnx") : kind.trimmed();
    emit settingsChanged();
}
QString AppSettings::storagePath() const { return settingsFilePath(); }
