#include "gui/BackendFacade.h"

#include <QDir>
#include <QDirIterator>
#include <QAudioDevice>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMediaDevices>
#include <QProcess>
#include <QRegularExpression>
#include <QVariantMap>
#include <QStandardPaths>
#include <QUrl>

#include "core/AssistantController.h"
#include "overlay/OverlayController.h"
#include "settings/AppSettings.h"
#include "settings/IdentityProfileService.h"

namespace {
struct PiperVoicePreset {
    QString id;
    QString label;
    QString modelUrl;
    QString configUrl;
};

QString firstValidPath(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty()) {
            QFileInfo info(candidate);
            if (info.exists() && info.isFile()) {
                return info.absoluteFilePath();
            }
        }
    }
    return {};
}

QString resolveExecutable(const QStringList &programNames, const QStringList &pathCandidates)
{
    for (const QString &program : programNames) {
        const QString fromPath = QStandardPaths::findExecutable(program);
        if (!fromPath.isEmpty()) {
            return fromPath;
        }
    }

    return firstValidPath(pathCandidates);
}

QString findFileRecursive(const QString &rootPath, const QString &fileName)
{
    if (rootPath.isEmpty()) {
        return {};
    }

    QDir root(rootPath);
    if (!root.exists()) {
        return {};
    }

    QDirIterator it(rootPath, {fileName}, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        return it.next();
    }

    return {};
}

QString findFirstMatchingFileRecursive(const QString &rootPath, const QStringList &patterns)
{
    if (rootPath.isEmpty()) {
        return {};
    }

    QDir root(rootPath);
    if (!root.exists()) {
        return {};
    }

    QDirIterator it(rootPath, patterns, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        return it.next();
    }

    return {};
}

const QList<PiperVoicePreset> &voicePresets()
{
    static const QList<PiperVoicePreset> presets{
        {
            QStringLiteral("en_GB-alba-medium"),
            QStringLiteral("Alba Medium  |  UK  |  Calm recommended"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_GB/alba/medium/en_GB-alba-medium.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_GB/alba/medium/en_GB-alba-medium.onnx.json?download=true")
        },
        {
            QStringLiteral("en_GB-alan-medium"),
            QStringLiteral("Alan Medium  |  UK  |  Neutral male"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_GB/alan/medium/en_GB-alan-medium.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_GB/alan/medium/en_GB-alan-medium.onnx.json?download=true")
        },
        {
            QStringLiteral("en_GB-northern_english_male-medium"),
            QStringLiteral("Northern English Male  |  UK  |  Deep tone"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_GB/northern_english_male/medium/en_GB-northern_english_male-medium.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_GB/northern_english_male/medium/en_GB-northern_english_male-medium.onnx.json?download=true")
        },
        {
            QStringLiteral("en_GB-semaine-medium"),
            QStringLiteral("Semaine Medium  |  UK  |  Controlled female"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_GB/semaine/medium/en_GB-semaine-medium.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_GB/semaine/medium/en_GB-semaine-medium.onnx.json?download=true")
        },
        {
            QStringLiteral("en_US-ryan-medium"),
            QStringLiteral("Ryan Medium  |  US  |  Neutral male"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_US/ryan/medium/en_US-ryan-medium.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_US/ryan/medium/en_US-ryan-medium.onnx.json?download=true")
        },
        {
            QStringLiteral("en_US-lessac-medium"),
            QStringLiteral("Lessac Medium  |  US  |  Clear neutral"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_US/lessac/medium/en_US-lessac-medium.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_US/lessac/medium/en_US-lessac-medium.onnx.json?download=true")
        }
    };

    return presets;
}

const PiperVoicePreset *findVoicePreset(const QString &voiceId)
{
    for (const PiperVoicePreset &preset : voicePresets()) {
        if (preset.id == voiceId) {
            return &preset;
        }
    }

    return nullptr;
}

QString detectVoicePresetIdFromPath(const QString &path)
{
    const QString fileName = QFileInfo(path).fileName();
    for (const PiperVoicePreset &preset : voicePresets()) {
        if (fileName.compare(preset.id + QStringLiteral(".onnx"), Qt::CaseInsensitive) == 0) {
            return preset.id;
        }
    }

    return {};
}

QString piperVoicesRoot(const QString &appDataRoot)
{
    return appDataRoot + QStringLiteral("/tools/piper-voices");
}

QString quotePowerShell(const QString &value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("'"), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(escaped);
}

bool downloadFileWithPowerShell(const QString &url, const QString &destinationPath, int timeoutMs, QString *error)
{
    QProcess process;
    const QString script = QStringLiteral(
        "$ProgressPreference='SilentlyContinue'; "
        "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; "
        "New-Item -ItemType Directory -Force -Path (Split-Path -Parent %1) | Out-Null; "
        "Invoke-WebRequest -UseBasicParsing -Uri %2 -OutFile %1")
        .arg(quotePowerShell(destinationPath), quotePowerShell(url));

    process.start(
        QStringLiteral("powershell"),
        {
            QStringLiteral("-NoProfile"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-Command"),
            script
        });

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        if (error) {
            *error = QStringLiteral("Download timed out.");
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
            if (error->isEmpty()) {
                *error = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
            }
            if (error->isEmpty()) {
                *error = QStringLiteral("Download failed.");
            }
        }
        return false;
    }

    return QFileInfo::exists(destinationPath);
}

QString detectPiperVoiceModel(const QString &appDataRoot)
{
    const QStringList roots = {
        piperVoicesRoot(appDataRoot),
        appDataRoot + QStringLiteral("/tools/piper"),
        QDir::currentPath() + QStringLiteral("/models"),
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/models")
    };

    const QStringList preferredFiles = {
        QStringLiteral("en_GB-alba-medium.onnx"),
        QStringLiteral("en_GB-northern_english_male-medium.onnx"),
        QStringLiteral("en_GB-southern_english_female-medium.onnx"),
        QStringLiteral("en_US-ryan-medium.onnx")
    };

    const QStringList patterns = {
        QStringLiteral("en_GB-*-medium.onnx"),
        QStringLiteral("en_GB-*.onnx"),
        QStringLiteral("en_US-*-medium.onnx"),
        QStringLiteral("en_US-*.onnx"),
        QStringLiteral("*-medium.onnx"),
        QStringLiteral("*.onnx")
    };

    for (const QString &rootPath : roots) {
        QDir root(rootPath);
        if (!root.exists()) {
            continue;
        }

        for (const QString &preferred : preferredFiles) {
            const QString directMatch = findFileRecursive(rootPath, preferred);
            if (!directMatch.isEmpty()) {
                return directMatch;
            }
        }

        for (const QString &pattern : patterns) {
            const QStringList files = root.entryList({pattern}, QDir::Files | QDir::Readable, QDir::Name);
            if (!files.isEmpty()) {
                return root.absoluteFilePath(files.first());
            }

            const QFileInfoList nested = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &subDirInfo : nested) {
                QDir subDir(subDirInfo.absoluteFilePath());
                const QStringList nestedFiles = subDir.entryList({pattern}, QDir::Files | QDir::Readable, QDir::Name);
                if (!nestedFiles.isEmpty()) {
                    return subDir.absoluteFilePath(nestedFiles.first());
                }
            }
        }
    }

    return {};
}

QString detectWhisperModel(const QString &appDataRoot)
{
    const QStringList roots = {
        appDataRoot + QStringLiteral("/tools/whisper/models"),
        appDataRoot + QStringLiteral("/tools/whisper"),
        QDir::currentPath() + QStringLiteral("/models"),
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/models")
    };

    const QStringList preferredFiles = {
        QStringLiteral("ggml-base.en.bin"),
        QStringLiteral("ggml-small.en.bin"),
        QStringLiteral("ggml-tiny.en.bin"),
        QStringLiteral("ggml-base.bin")
    };

    for (const QString &rootPath : roots) {
        for (const QString &preferred : preferredFiles) {
            const QString match = findFileRecursive(rootPath, preferred);
            if (!match.isEmpty()) {
                return match;
            }
        }

        const QString fallback = findFirstMatchingFileRecursive(rootPath, {QStringLiteral("ggml-*.bin")});
        if (!fallback.isEmpty()) {
            return fallback;
        }
    }

    return {};
}

QString resolveExecutableFromDirectory(const QString &directoryPath, const QStringList &candidateNames)
{
    QDir directory(directoryPath);
    if (!directory.exists()) {
        return {};
    }

    for (const QString &name : candidateNames) {
        const QString direct = directory.absoluteFilePath(name);
        if (QFileInfo::exists(direct)) {
            return QFileInfo(direct).absoluteFilePath();
        }
    }

    QDirIterator it(directoryPath, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        QFileInfo info(filePath);
        if (!info.isFile()) {
            continue;
        }

        for (const QString &name : candidateNames) {
            if (info.fileName().compare(name, Qt::CaseInsensitive) == 0) {
                return info.absoluteFilePath();
            }
        }
    }

    return {};
}

QString resolveExecutableSelection(const QString &selection, const QStringList &candidateNames)
{
    const QString trimmed = selection.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QFileInfo info(trimmed);
    if (info.exists() && info.isFile()) {
        for (const QString &name : candidateNames) {
            if (info.fileName().compare(name, Qt::CaseInsensitive) == 0) {
                return info.absoluteFilePath();
            }
        }
        return {};
    }

    if (info.exists() && info.isDir()) {
        return resolveExecutableFromDirectory(info.absoluteFilePath(), candidateNames);
    }

    return {};
}

QString resolveVoiceModelSelection(const QString &selection)
{
    const QString trimmed = selection.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QFileInfo info(trimmed);
    if (info.exists() && info.isFile() && info.suffix().compare(QStringLiteral("onnx"), Qt::CaseInsensitive) == 0) {
        return info.absoluteFilePath();
    }

    if (info.exists() && info.isDir()) {
        const QString found = findFileRecursive(info.absoluteFilePath(), QStringLiteral("*.onnx"));
        return found;
    }

    return {};
}

QString resolveWhisperModelSelection(const QString &selection)
{
    const QString trimmed = selection.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QFileInfo info(trimmed);
    if (info.exists() && info.isFile()
        && info.fileName().startsWith(QStringLiteral("ggml-"), Qt::CaseInsensitive)
        && info.suffix().compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0) {
        return info.absoluteFilePath();
    }

    if (info.exists() && info.isDir()) {
        return findFirstMatchingFileRecursive(info.absoluteFilePath(), {QStringLiteral("ggml-*.bin")});
    }

    return {};
}

QString extractVersionToken(const QString &text)
{
    const QRegularExpression versionPattern(QStringLiteral("(v?\\d+\\.\\d+(?:\\.\\d+)*)"));
    const QRegularExpressionMatch match = versionPattern.match(text);
    if (!match.hasMatch()) {
        return {};
    }

    return match.captured(1);
}

QString probeToolVersion(const QString &executablePath, const QStringList &args)
{
    if (executablePath.isEmpty() || !QFileInfo::exists(executablePath)) {
        return {};
    }

    QProcess process;
    process.start(executablePath, args);
    if (!process.waitForFinished(3000)) {
        process.kill();
        return {};
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput())
        + QString::fromUtf8(process.readAllStandardError());
    return extractVersionToken(output);
}

QString fetchLatestReleaseTag(const QString &repo)
{
    QProcess process;
    const QString command = QStringLiteral("(Invoke-RestMethod -Uri 'https://api.github.com/repos/%1/releases/latest').tag_name").arg(repo);
    process.start(QStringLiteral("powershell"),
        {
            QStringLiteral("-NoProfile"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-Command"),
            command
        });

    if (!process.waitForFinished(3500)) {
        process.kill();
        return {};
    }

    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

bool looksLatestEnough(const QString &installedVersion, const QString &latestTag)
{
    if (installedVersion.isEmpty() || latestTag.isEmpty()) {
        return false;
    }

    return latestTag.contains(installedVersion, Qt::CaseInsensitive);
}
}

BackendFacade::BackendFacade(
    AppSettings *settings,
    IdentityProfileService *identityProfileService,
    AssistantController *assistantController,
    OverlayController *overlayController,
    QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_identityProfileService(identityProfileService)
    , m_assistantController(assistantController)
    , m_overlayController(overlayController)
{
    connect(m_assistantController, &AssistantController::stateChanged, this, &BackendFacade::stateNameChanged);
    connect(m_assistantController, &AssistantController::transcriptChanged, this, &BackendFacade::transcriptChanged);
    connect(m_assistantController, &AssistantController::responseTextChanged, this, &BackendFacade::responseTextChanged);
    connect(m_assistantController, &AssistantController::statusTextChanged, this, &BackendFacade::statusTextChanged);
    connect(m_assistantController, &AssistantController::audioLevelChanged, this, &BackendFacade::audioLevelChanged);
    connect(m_assistantController, &AssistantController::modelsChanged, this, [this]() {
        emit modelsChanged();
        emit selectedModelChanged();
    });
    connect(m_overlayController, &OverlayController::visibilityChanged, this, &BackendFacade::overlayVisibleChanged);
    connect(m_settings, &AppSettings::settingsChanged, this, &BackendFacade::settingsChanged);

    auto *mediaDevices = new QMediaDevices(this);
    connect(mediaDevices, &QMediaDevices::audioInputsChanged, this, &BackendFacade::audioDevicesChanged);
    connect(mediaDevices, &QMediaDevices::audioOutputsChanged, this, &BackendFacade::audioDevicesChanged);
}

QString BackendFacade::stateName() const { return m_assistantController->stateName(); }
QString BackendFacade::transcript() const { return m_assistantController->transcript(); }
QString BackendFacade::responseText() const { return m_assistantController->responseText(); }
QString BackendFacade::statusText() const { return m_assistantController->statusText(); }
double BackendFacade::audioLevel() const { return m_assistantController->audioLevel(); }
QStringList BackendFacade::models() const { return m_assistantController->availableModelIds(); }
QString BackendFacade::selectedModel() const { return m_assistantController->selectedModel(); }
QStringList BackendFacade::voicePresetNames() const
{
    QStringList values;
    for (const PiperVoicePreset &preset : voicePresets()) {
        values.push_back(preset.label);
    }
    return values;
}
QStringList BackendFacade::voicePresetIds() const
{
    QStringList values;
    for (const PiperVoicePreset &preset : voicePresets()) {
        values.push_back(preset.id);
    }
    return values;
}
QString BackendFacade::selectedVoicePresetId() const { return m_settings->selectedVoicePresetId(); }
bool BackendFacade::overlayVisible() const { return m_overlayController->isVisible(); }
QString BackendFacade::lmStudioEndpoint() const { return m_settings->lmStudioEndpoint(); }
int BackendFacade::defaultReasoningMode() const { return static_cast<int>(m_settings->defaultReasoningMode()); }
bool BackendFacade::autoRoutingEnabled() const { return m_settings->autoRoutingEnabled(); }
bool BackendFacade::streamingEnabled() const { return m_settings->streamingEnabled(); }
int BackendFacade::requestTimeoutMs() const { return m_settings->requestTimeoutMs(); }
QString BackendFacade::whisperExecutable() const { return m_settings->whisperExecutable(); }
QString BackendFacade::whisperModelPath() const { return m_settings->whisperModelPath(); }
QString BackendFacade::piperExecutable() const { return m_settings->piperExecutable(); }
QString BackendFacade::piperVoiceModel() const { return m_settings->piperVoiceModel(); }
QString BackendFacade::ffmpegExecutable() const { return m_settings->ffmpegExecutable(); }
double BackendFacade::voiceSpeed() const { return m_settings->voiceSpeed(); }
double BackendFacade::voicePitch() const { return m_settings->voicePitch(); }
double BackendFacade::micSensitivity() const { return m_settings->micSensitivity(); }
QStringList BackendFacade::audioInputDeviceNames() const
{
    QStringList names;
    for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
        names.push_back(device.description());
    }
    return names;
}

QStringList BackendFacade::audioInputDeviceIds() const
{
    QStringList ids;
    for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
        ids.push_back(QString::fromUtf8(device.id()));
    }
    return ids;
}

QStringList BackendFacade::audioOutputDeviceNames() const
{
    QStringList names;
    for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
        names.push_back(device.description());
    }
    return names;
}

QStringList BackendFacade::audioOutputDeviceIds() const
{
    QStringList ids;
    for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
        ids.push_back(QString::fromUtf8(device.id()));
    }
    return ids;
}

QString BackendFacade::selectedAudioInputDeviceId() const { return m_settings->selectedAudioInputDeviceId(); }
QString BackendFacade::selectedAudioOutputDeviceId() const { return m_settings->selectedAudioOutputDeviceId(); }
bool BackendFacade::clickThroughEnabled() const { return m_settings->clickThroughEnabled(); }
QString BackendFacade::assistantName() const { return m_identityProfileService->identity().assistantName; }
QString BackendFacade::userName() const
{
    const UserProfile profile = m_identityProfileService->userProfile();
    return profile.displayName.isEmpty() ? profile.userName : profile.displayName;
}
QString BackendFacade::spokenUserName() const
{
    const UserProfile profile = m_identityProfileService->userProfile();
    const QString displayName = profile.displayName.isEmpty() ? profile.userName : profile.displayName;
    return profile.spokenName.isEmpty() ? displayName : profile.spokenName;
}
bool BackendFacade::initialSetupCompleted() const { return m_settings->initialSetupCompleted(); }
QString BackendFacade::toolInstallStatus() const { return m_toolInstallStatus; }
QString BackendFacade::wakeWordPhrase() const { return m_settings->wakeWordPhrase(); }
void BackendFacade::toggleOverlay() { m_overlayController->toggleOverlay(); }
void BackendFacade::refreshModels() { m_assistantController->refreshModels(); }
void BackendFacade::submitText(const QString &text) { m_assistantController->submitText(text); }
void BackendFacade::startListening() { m_assistantController->startListening(); }
void BackendFacade::cancelRequest() { m_assistantController->cancelActiveRequest(); }
void BackendFacade::setSelectedModel(const QString &modelId) { m_assistantController->setSelectedModel(modelId); }
void BackendFacade::setSelectedVoicePresetId(const QString &voiceId)
{
    if (findVoicePreset(voiceId) == nullptr) {
        return;
    }

    m_settings->setSelectedVoicePresetId(voiceId);
    m_settings->save();
    emit settingsChanged();
}
void BackendFacade::refreshAudioDevices()
{
    emit audioDevicesChanged();
    emit settingsChanged();
}

void BackendFacade::saveSettings(
    const QString &endpoint,
    const QString &modelId,
    int defaultMode,
    bool autoRouting,
    bool streaming,
    int timeoutMs,
    const QString &whisperPath,
    const QString &whisperModelPath,
    const QString &piperPath,
    const QString &voicePath,
    const QString &ffmpegPath,
    double voiceSpeed,
    double voicePitch,
    double micSensitivity,
    const QString &audioInputDeviceId,
    const QString &audioOutputDeviceId,
    bool clickThrough)
{
    const QString detectedVoicePresetId = detectVoicePresetIdFromPath(voicePath);
    if (!detectedVoicePresetId.isEmpty()) {
        m_settings->setSelectedVoicePresetId(detectedVoicePresetId);
    }

    m_assistantController->saveSettings(
        endpoint, modelId, defaultMode, autoRouting, streaming, timeoutMs,
        whisperPath,
        whisperModelPath,
        piperPath,
        voicePath,
        ffmpegPath,
        voiceSpeed,
        voicePitch,
        micSensitivity,
        audioInputDeviceId,
        audioOutputDeviceId,
        clickThrough);
    m_overlayController->setClickThrough(clickThrough);
    emit settingsChanged();
}

bool BackendFacade::downloadVoiceModel(const QString &voiceId)
{
    const PiperVoicePreset *preset = findVoicePreset(voiceId);
    if (preset == nullptr) {
        setToolInstallStatus(QStringLiteral("Selected Piper voice is not recognized."));
        return false;
    }

    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString voiceRoot = piperVoicesRoot(appDataRoot);
    QDir().mkpath(voiceRoot);

    const QString modelPath = voiceRoot + QStringLiteral("/") + preset->id + QStringLiteral(".onnx");
    const QString configPath = modelPath + QStringLiteral(".json");

    setToolInstallStatus(QStringLiteral("Downloading Piper voice %1...").arg(preset->id));

    QString errorText;
    if (!QFileInfo::exists(modelPath) && !downloadFileWithPowerShell(preset->modelUrl, modelPath, 5 * 60 * 1000, &errorText)) {
        setToolInstallStatus(QStringLiteral("Voice download failed: %1").arg(errorText));
        return false;
    }

    if (!QFileInfo::exists(configPath) && !downloadFileWithPowerShell(preset->configUrl, configPath, 60 * 1000, &errorText)) {
        setToolInstallStatus(QStringLiteral("Voice config download failed: %1").arg(errorText));
        return false;
    }

    m_settings->setSelectedVoicePresetId(preset->id);
    m_settings->setPiperVoiceModel(modelPath);
    m_settings->save();
    setToolInstallStatus(QStringLiteral("Voice ready: %1").arg(preset->label));
    emit settingsChanged();
    return true;
}

bool BackendFacade::completeInitialSetup(
    const QString &displayName,
    const QString &spokenName,
    const QString &endpoint,
    const QString &modelId,
    const QString &whisperPath,
    const QString &whisperModelPath,
    const QString &piperPath,
    const QString &voicePath,
    const QString &ffmpegPath,
    const QString &audioInputDeviceId,
    const QString &audioOutputDeviceId,
    bool clickThrough)
{
    const QString normalizedEndpoint = endpoint.trimmed();
    if (normalizedEndpoint.isEmpty()) {
        setToolInstallStatus(QStringLiteral("LM Studio endpoint is required."));
        return false;
    }

    const QStringList modelIds = m_assistantController->availableModelIds();
    if (modelIds.isEmpty() || !modelIds.contains(modelId)) {
        setToolInstallStatus(QStringLiteral("Selected AI model is invalid. Refresh models and choose a valid one."));
        return false;
    }

    const QString resolvedWhisper = resolveExecutableSelection(
        whisperPath,
        {
            QStringLiteral("whisper-cli.exe"),
            QStringLiteral("main.exe"),
            QStringLiteral("whisper.exe")
        });
    if (resolvedWhisper.isEmpty()) {
        setToolInstallStatus(QStringLiteral("Whisper executable is invalid. Use whisper-cli.exe or main.exe from the whisper Release folder."));
        return false;
    }

    const QString resolvedWhisperModel = resolveWhisperModelSelection(whisperModelPath);
    if (resolvedWhisperModel.isEmpty()) {
        setToolInstallStatus(QStringLiteral("Whisper model is invalid. Select a valid ggml-*.bin model file."));
        return false;
    }

    const QString resolvedPiper = resolveExecutableSelection(
        piperPath,
        {
            QStringLiteral("piper.exe")
        });
    if (resolvedPiper.isEmpty()) {
        setToolInstallStatus(QStringLiteral("Piper executable is invalid. Select piper.exe."));
        return false;
    }

    const QString resolvedFfmpeg = resolveExecutableSelection(
        ffmpegPath,
        {
            QStringLiteral("ffmpeg.exe")
        });
    if (resolvedFfmpeg.isEmpty()) {
        setToolInstallStatus(QStringLiteral("FFmpeg executable is invalid. Select ffmpeg.exe."));
        return false;
    }

    const QString resolvedVoiceModel = resolveVoiceModelSelection(voicePath);
    if (resolvedVoiceModel.isEmpty()) {
        setToolInstallStatus(QStringLiteral("Piper voice model is invalid. Select a valid .onnx model file."));
        return false;
    }

    if (!audioInputDeviceId.isEmpty() && !audioInputDeviceIds().contains(audioInputDeviceId)) {
        setToolInstallStatus(QStringLiteral("Selected microphone is not available anymore."));
        return false;
    }

    if (!audioOutputDeviceId.isEmpty() && !audioOutputDeviceIds().contains(audioOutputDeviceId)) {
        setToolInstallStatus(QStringLiteral("Selected speaker is not available anymore."));
        return false;
    }

    m_identityProfileService->setUserNames(displayName.trimmed(), spokenName.trimmed());

    const QString detectedVoicePresetId = detectVoicePresetIdFromPath(resolvedVoiceModel);
    if (!detectedVoicePresetId.isEmpty()) {
        m_settings->setSelectedVoicePresetId(detectedVoicePresetId);
    }

    m_assistantController->saveSettings(
        normalizedEndpoint,
        modelId,
        static_cast<int>(ReasoningMode::Balanced),
        true,
        true,
        12000,
        resolvedWhisper,
        resolvedWhisperModel,
        resolvedPiper,
        resolvedVoiceModel,
        resolvedFfmpeg,
        0.89,
        0.93,
        0.02,
        audioInputDeviceId,
        audioOutputDeviceId,
        clickThrough);

    m_overlayController->setClickThrough(clickThrough);
    m_settings->setInitialSetupCompleted(true);
    m_settings->save();
    setToolInstallStatus(QStringLiteral("Setup validation passed. Configuration saved."));
    emit profileChanged();
    emit settingsChanged();
    emit initialSetupFinished();
    return true;
}

bool BackendFacade::runSetupScenario(
    const QString &displayName,
    const QString &spokenName,
    const QString &endpoint,
    const QString &modelId,
    const QString &whisperPath,
    const QString &whisperModelPath,
    const QString &piperPath,
    const QString &voicePath,
    const QString &ffmpegPath,
    const QString &audioInputDeviceId,
    const QString &audioOutputDeviceId,
    bool clickThrough,
    const QString &scenarioId)
{
    const QString normalizedEndpoint = endpoint.trimmed();
    if (normalizedEndpoint.isEmpty()) {
        setToolInstallStatus(QStringLiteral("LM Studio endpoint is required before running a setup scenario."));
        return false;
    }

    const QStringList modelIds = m_assistantController->availableModelIds();
    if (modelIds.isEmpty() || !modelIds.contains(modelId)) {
        setToolInstallStatus(QStringLiteral("Selected AI model is invalid. Refresh models and choose a valid one."));
        return false;
    }

    const QString resolvedWhisper = resolveExecutableSelection(
        whisperPath,
        {
            QStringLiteral("whisper-cli.exe"),
            QStringLiteral("main.exe"),
            QStringLiteral("whisper.exe")
        });
    if (resolvedWhisper.isEmpty()) {
        setToolInstallStatus(QStringLiteral("Whisper executable is invalid. Use whisper-cli.exe or main.exe from the whisper Release folder."));
        return false;
    }

    const QString resolvedWhisperModel = resolveWhisperModelSelection(whisperModelPath);
    if (resolvedWhisperModel.isEmpty()) {
        setToolInstallStatus(QStringLiteral("Whisper model is invalid. Select a valid ggml-*.bin model file."));
        return false;
    }

    const QString resolvedPiper = resolveExecutableSelection(
        piperPath,
        {
            QStringLiteral("piper.exe")
        });
    if (resolvedPiper.isEmpty()) {
        setToolInstallStatus(QStringLiteral("Piper executable is invalid. Select piper.exe."));
        return false;
    }

    const QString resolvedFfmpeg = resolveExecutableSelection(
        ffmpegPath,
        {
            QStringLiteral("ffmpeg.exe")
        });
    if (resolvedFfmpeg.isEmpty()) {
        setToolInstallStatus(QStringLiteral("FFmpeg executable is invalid. Select ffmpeg.exe."));
        return false;
    }

    const QString resolvedVoiceModel = resolveVoiceModelSelection(voicePath);
    if (resolvedVoiceModel.isEmpty()) {
        setToolInstallStatus(QStringLiteral("Piper voice model is invalid. Select a valid .onnx model file."));
        return false;
    }

    if (!audioInputDeviceId.isEmpty() && !audioInputDeviceIds().contains(audioInputDeviceId)) {
        setToolInstallStatus(QStringLiteral("Selected microphone is not available anymore."));
        return false;
    }

    if (!audioOutputDeviceId.isEmpty() && !audioOutputDeviceIds().contains(audioOutputDeviceId)) {
        setToolInstallStatus(QStringLiteral("Selected speaker is not available anymore."));
        return false;
    }

    m_identityProfileService->setUserNames(displayName.trimmed(), spokenName.trimmed());

    const QString detectedVoicePresetId = detectVoicePresetIdFromPath(resolvedVoiceModel);
    if (!detectedVoicePresetId.isEmpty()) {
        m_settings->setSelectedVoicePresetId(detectedVoicePresetId);
    }

    m_assistantController->saveSettings(
        normalizedEndpoint,
        modelId,
        static_cast<int>(ReasoningMode::Balanced),
        true,
        true,
        12000,
        resolvedWhisper,
        resolvedWhisperModel,
        resolvedPiper,
        resolvedVoiceModel,
        resolvedFfmpeg,
        0.89,
        0.93,
        0.02,
        audioInputDeviceId,
        audioOutputDeviceId,
        clickThrough);

    QString testPrompt;
    QString status;
    if (scenarioId == QStringLiteral("wakeword_time")) {
        testPrompt = QStringLiteral("%1, what's the time now?").arg(m_settings->wakeWordPhrase());
        status = QStringLiteral("Wake phrase time test started. Listen for the local clock response.");
    } else {
        testPrompt = m_settings->wakeWordPhrase();
        status = QStringLiteral("Wake phrase presence test started. Listen for the ready response.");
    }

    m_assistantController->submitText(testPrompt);
    setToolInstallStatus(status);
    emit settingsChanged();
    return true;
}

QVariantMap BackendFacade::evaluateSetupRequirements(
    const QString &endpoint,
    const QString &modelId,
    const QString &whisperPath,
    const QString &whisperModelPath,
    const QString &piperPath,
    const QString &voicePath,
    const QString &ffmpegPath)
{
    QVariantMap result;

    const bool endpointOk = !endpoint.trimmed().isEmpty();
    const bool modelOk = !modelId.trimmed().isEmpty() && m_assistantController->availableModelIds().contains(modelId);

    const QString resolvedWhisper = resolveExecutableSelection(
        whisperPath,
        {
            QStringLiteral("whisper-cli.exe"),
            QStringLiteral("main.exe"),
            QStringLiteral("whisper.exe")
        });
    const QString resolvedWhisperModel = resolveWhisperModelSelection(whisperModelPath);
    const QString resolvedPiper = resolveExecutableSelection(
        piperPath,
        {
            QStringLiteral("piper.exe")
        });
    const QString resolvedFfmpeg = resolveExecutableSelection(
        ffmpegPath,
        {
            QStringLiteral("ffmpeg.exe")
        });
    const QString resolvedVoice = resolveVoiceModelSelection(voicePath);

    const bool whisperOk = !resolvedWhisper.isEmpty();
    const bool whisperModelOk = !resolvedWhisperModel.isEmpty();
    const bool piperOk = !resolvedPiper.isEmpty();
    const bool ffmpegOk = !resolvedFfmpeg.isEmpty();
    const bool voiceOk = !resolvedVoice.isEmpty();

    const QString whisperVersion = whisperOk ? probeToolVersion(resolvedWhisper, {QStringLiteral("--version")}) : QString{};
    const QString piperVersion = piperOk ? probeToolVersion(resolvedPiper, {QStringLiteral("--version")}) : QString{};
    const QString ffmpegVersion = ffmpegOk ? probeToolVersion(resolvedFfmpeg, {QStringLiteral("-version")}) : QString{};

    const QString whisperLatest = whisperOk ? fetchLatestReleaseTag(QStringLiteral("ggerganov/whisper.cpp")) : QString{};
    const QString piperLatest = piperOk ? fetchLatestReleaseTag(QStringLiteral("rhasspy/piper")) : QString{};
    const QString ffmpegLatest = ffmpegOk ? fetchLatestReleaseTag(QStringLiteral("BtbN/FFmpeg-Builds")) : QString{};

    result.insert(QStringLiteral("endpointOk"), endpointOk);
    result.insert(QStringLiteral("modelOk"), modelOk);
    result.insert(QStringLiteral("whisperOk"), whisperOk);
    result.insert(QStringLiteral("whisperModelOk"), whisperModelOk);
    result.insert(QStringLiteral("piperOk"), piperOk);
    result.insert(QStringLiteral("voiceOk"), voiceOk);
    result.insert(QStringLiteral("ffmpegOk"), ffmpegOk);

    result.insert(QStringLiteral("whisperPathResolved"), resolvedWhisper);
    result.insert(QStringLiteral("whisperModelPathResolved"), resolvedWhisperModel);
    result.insert(QStringLiteral("piperPathResolved"), resolvedPiper);
    result.insert(QStringLiteral("voicePathResolved"), resolvedVoice);
    result.insert(QStringLiteral("ffmpegPathResolved"), resolvedFfmpeg);

    result.insert(QStringLiteral("whisperVersion"), whisperVersion);
    result.insert(QStringLiteral("piperVersion"), piperVersion);
    result.insert(QStringLiteral("ffmpegVersion"), ffmpegVersion);
    result.insert(QStringLiteral("whisperLatestTag"), whisperLatest);
    result.insert(QStringLiteral("piperLatestTag"), piperLatest);
    result.insert(QStringLiteral("ffmpegLatestTag"), ffmpegLatest);
    result.insert(QStringLiteral("whisperLatestOk"), looksLatestEnough(whisperVersion, whisperLatest));
    result.insert(QStringLiteral("piperLatestOk"), looksLatestEnough(piperVersion, piperLatest));
    result.insert(QStringLiteral("ffmpegLatestOk"), looksLatestEnough(ffmpegVersion, ffmpegLatest));

    const bool allValid = endpointOk && modelOk && whisperOk && whisperModelOk && piperOk && voiceOk && ffmpegOk;
    result.insert(QStringLiteral("allValid"), allValid);

    return result;
}

void BackendFacade::openContainingDirectory(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QFileInfo info(trimmed);
    QString directoryPath;
    if (info.exists() && info.isDir()) {
        directoryPath = info.absoluteFilePath();
    } else if (info.exists() && info.isFile()) {
        directoryPath = info.absolutePath();
    } else {
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(directoryPath));
}

void BackendFacade::setToolInstallStatus(const QString &status)
{
    if (m_toolInstallStatus == status) {
        return;
    }

    m_toolInstallStatus = status;
    emit toolInstallStatusChanged();
}

bool BackendFacade::autoDetectVoiceTools()
{
    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString toolsRoot = appDataRoot + QStringLiteral("/tools");
    QDir().mkpath(appDataRoot + QStringLiteral("/tools"));

    QString whisper = resolveExecutable(
        {QStringLiteral("whisper-cli"), QStringLiteral("whisper"), QStringLiteral("main")},
        {
            appDataRoot + QStringLiteral("/tools/whisper/whisper-cli.exe"),
            appDataRoot + QStringLiteral("/tools/whisper/main.exe"),
            appDataRoot + QStringLiteral("/tools/whisper/Release/whisper-cli.exe"),
            appDataRoot + QStringLiteral("/tools/whisper/Release/main.exe"),
            appDataRoot + QStringLiteral("/tools/whisper/bin/whisper-cli.exe"),
            appDataRoot + QStringLiteral("/tools/whisper/bin/main.exe")
        });
    if (whisper.isEmpty()) {
        whisper = findFileRecursive(toolsRoot, QStringLiteral("whisper-cli.exe"));
    }
    if (whisper.isEmpty()) {
        whisper = findFileRecursive(toolsRoot, QStringLiteral("main.exe"));
    }

    QString whisperModel = m_settings->whisperModelPath();
    if (whisperModel.isEmpty() || !QFileInfo::exists(whisperModel)) {
        whisperModel = detectWhisperModel(appDataRoot);
    }

    QString piper = resolveExecutable(
        {QStringLiteral("piper")},
        {
            appDataRoot + QStringLiteral("/tools/piper/piper.exe"),
            appDataRoot + QStringLiteral("/tools/piper/bin/piper.exe"),
            appDataRoot + QStringLiteral("/tools/piper/piper/piper.exe")
        });
    if (piper.isEmpty()) {
        piper = findFileRecursive(toolsRoot, QStringLiteral("piper.exe"));
    }

    QString ffmpeg = resolveExecutable(
        {QStringLiteral("ffmpeg")},
        {
            appDataRoot + QStringLiteral("/tools/ffmpeg/ffmpeg.exe"),
            appDataRoot + QStringLiteral("/tools/ffmpeg/bin/ffmpeg.exe"),
            appDataRoot + QStringLiteral("/tools/ffmpeg_build/bin/ffmpeg.exe")
        });
    if (ffmpeg.isEmpty()) {
        ffmpeg = findFileRecursive(toolsRoot, QStringLiteral("ffmpeg.exe"));
    }

    QString voiceModel = m_settings->piperVoiceModel();
    if (voiceModel.isEmpty() || !QFileInfo::exists(voiceModel)) {
        voiceModel = detectPiperVoiceModel(appDataRoot);
    }

    if (!whisper.isEmpty()) {
        m_settings->setWhisperExecutable(whisper);
    }
    if (!whisperModel.isEmpty()) {
        m_settings->setWhisperModelPath(whisperModel);
    }
    if (!piper.isEmpty()) {
        m_settings->setPiperExecutable(piper);
    }
    if (!ffmpeg.isEmpty()) {
        m_settings->setFfmpegExecutable(ffmpeg);
    }
    if (!voiceModel.isEmpty()) {
        m_settings->setPiperVoiceModel(voiceModel);
        const QString detectedVoicePresetId = detectVoicePresetIdFromPath(voiceModel);
        if (!detectedVoicePresetId.isEmpty()) {
            m_settings->setSelectedVoicePresetId(detectedVoicePresetId);
        }
    }

    const bool complete = !m_settings->whisperExecutable().isEmpty()
        && !m_settings->whisperModelPath().isEmpty()
        && !m_settings->piperExecutable().isEmpty()
        && !m_settings->ffmpegExecutable().isEmpty()
        && !m_settings->piperVoiceModel().isEmpty();

    setToolInstallStatus(complete
            ? QStringLiteral("Voice tools detected and fields populated.")
            : QStringLiteral("Some tools are still missing. Use Install Missing Tools."));

    m_settings->save();
    emit settingsChanged();
    return complete;
}

bool BackendFacade::installAndDetectVoiceTools()
{
    if (autoDetectVoiceTools()) {
        return true;
    }

#ifdef Q_OS_WIN
    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString toolsRoot = QDir::toNativeSeparators(appDataRoot + QStringLiteral("/tools"));
    const PiperVoicePreset *preset = findVoicePreset(m_settings->selectedVoicePresetId());
    const PiperVoicePreset &voicePreset = preset != nullptr ? *preset : voicePresets().first();
    setToolInstallStatus(QStringLiteral("Installing missing voice tools. This can take a few minutes..."));

    const QString script = QStringLiteral(R"POWERSHELL(
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$toolsRoot = '%1'
New-Item -ItemType Directory -Force -Path $toolsRoot | Out-Null

function Install-LatestZip {
    param(
        [string]$Repo,
        [string[]]$NamePatterns,
        [string]$Destination
    )

    $release = Invoke-RestMethod -Uri ("https://api.github.com/repos/{0}/releases/latest" -f $Repo)
    $asset = $null
    foreach ($pattern in $NamePatterns) {
        $asset = $release.assets | Where-Object { $_.name -like $pattern } | Select-Object -First 1
        if ($asset) { break }
    }
    if (-not $asset) {
        throw "Unable to find a matching release asset for $Repo"
    }

    $zipPath = Join-Path $toolsRoot (($Repo -replace '/', '_') + '.zip')
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zipPath
    if (Test-Path $Destination) { Remove-Item -Recurse -Force $Destination }
    Expand-Archive -Path $zipPath -DestinationPath $Destination -Force
}

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    winget install --id Gyan.FFmpeg.Essentials --exact --accept-source-agreements --accept-package-agreements --silent
}

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    $ffmpegDir = Join-Path $toolsRoot 'ffmpeg_build'
    Install-LatestZip -Repo 'BtbN/FFmpeg-Builds' -NamePatterns @('*win64-lgpl-shared*.zip', '*win64-gpl-shared*.zip') -Destination $ffmpegDir
}

$whisperDir = Join-Path $toolsRoot 'whisper'
if (-not (Test-Path (Join-Path $whisperDir 'whisper-cli.exe')) -and -not (Test-Path (Join-Path $whisperDir 'main.exe'))) {
    Install-LatestZip -Repo 'ggerganov/whisper.cpp' -NamePatterns @('*bin*x64*.zip', '*windows*x64*.zip') -Destination $whisperDir
}

$whisperModelDir = Join-Path $whisperDir 'models'
New-Item -ItemType Directory -Force -Path $whisperModelDir | Out-Null
$whisperModel = Join-Path $whisperModelDir 'ggml-base.en.bin'
if (-not (Test-Path $whisperModel)) {
    Invoke-WebRequest -Uri 'https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin?download=true' -OutFile $whisperModel
}

$piperDir = Join-Path $toolsRoot 'piper'
if (-not (Test-Path (Join-Path $piperDir 'piper.exe'))) {
    Install-LatestZip -Repo 'rhasspy/piper' -NamePatterns @('*windows*amd64*.zip', '*win*amd64*.zip') -Destination $piperDir
}

$voiceDir = Join-Path $toolsRoot 'piper-voices'
New-Item -ItemType Directory -Force -Path $voiceDir | Out-Null
$voiceModel = Join-Path $voiceDir '%2.onnx'
$voiceJson = Join-Path $voiceDir '%2.onnx.json'

if (-not (Test-Path $voiceModel)) {
    Invoke-WebRequest -Uri '%3' -OutFile $voiceModel
}
if (-not (Test-Path $voiceJson)) {
    Invoke-WebRequest -Uri '%4' -OutFile $voiceJson
}
)POWERSHELL").arg(toolsRoot, voicePreset.id, voicePreset.modelUrl, voicePreset.configUrl);

    QProcess installer;
    installer.start(
        QStringLiteral("powershell"),
        {
            QStringLiteral("-NoProfile"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-Command"),
            script
        });

    installer.waitForFinished(15 * 60 * 1000);
    if (installer.exitStatus() != QProcess::NormalExit || installer.exitCode() != 0) {
        setToolInstallStatus(QStringLiteral("Automatic install failed. Please run as Administrator and try again."));
        return autoDetectVoiceTools();
    }

    const bool complete = autoDetectVoiceTools();
    setToolInstallStatus(complete
            ? QStringLiteral("Voice tools installed and configured.")
            : QStringLiteral("Install finished, but some paths still need manual review."));
    return complete;
#else
    setToolInstallStatus(QStringLiteral("Automatic install is currently implemented for Windows only."));
    return autoDetectVoiceTools();
#endif
}
