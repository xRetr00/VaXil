#include "gui/BackendFacade.h"

#include <QDir>
#include <QDirIterator>
#include <QAudioDevice>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMediaDevices>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include "core/AssistantController.h"
#include "overlay/OverlayController.h"
#include "settings/AppSettings.h"
#include "settings/IdentityProfileService.h"

namespace {
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

QString detectPiperVoiceModel(const QString &appDataRoot)
{
    const QStringList roots = {
        appDataRoot + QStringLiteral("/tools/piper-voices"),
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
bool BackendFacade::overlayVisible() const { return m_overlayController->isVisible(); }
QString BackendFacade::lmStudioEndpoint() const { return m_settings->lmStudioEndpoint(); }
int BackendFacade::defaultReasoningMode() const { return static_cast<int>(m_settings->defaultReasoningMode()); }
bool BackendFacade::autoRoutingEnabled() const { return m_settings->autoRoutingEnabled(); }
bool BackendFacade::streamingEnabled() const { return m_settings->streamingEnabled(); }
int BackendFacade::requestTimeoutMs() const { return m_settings->requestTimeoutMs(); }
QString BackendFacade::whisperExecutable() const { return m_settings->whisperExecutable(); }
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
QString BackendFacade::userName() const { return m_identityProfileService->userProfile().userName; }
bool BackendFacade::initialSetupCompleted() const { return m_settings->initialSetupCompleted(); }
QString BackendFacade::toolInstallStatus() const { return m_toolInstallStatus; }
void BackendFacade::toggleOverlay() { m_overlayController->toggleOverlay(); }
void BackendFacade::refreshModels() { m_assistantController->refreshModels(); }
void BackendFacade::submitText(const QString &text) { m_assistantController->submitText(text); }
void BackendFacade::startListening() { m_assistantController->startListening(); }
void BackendFacade::cancelRequest() { m_assistantController->cancelActiveRequest(); }
void BackendFacade::setSelectedModel(const QString &modelId) { m_assistantController->setSelectedModel(modelId); }
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
    m_assistantController->saveSettings(
        endpoint, modelId, defaultMode, autoRouting, streaming, timeoutMs,
        whisperPath,
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

bool BackendFacade::completeInitialSetup(
    const QString &userName,
    const QString &endpoint,
    const QString &modelId,
    const QString &whisperPath,
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

    if (!userName.trimmed().isEmpty()) {
        m_identityProfileService->setUserName(userName.trimmed());
    }

    m_assistantController->saveSettings(
        normalizedEndpoint,
        modelId,
        static_cast<int>(ReasoningMode::Balanced),
        true,
        true,
        12000,
        resolvedWhisper,
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

bool BackendFacade::runSetupVoiceTest(
    const QString &userName,
    const QString &endpoint,
    const QString &modelId,
    const QString &whisperPath,
    const QString &piperPath,
    const QString &voicePath,
    const QString &ffmpegPath,
    const QString &audioInputDeviceId,
    const QString &audioOutputDeviceId,
    bool clickThrough)
{
    const QString normalizedEndpoint = endpoint.trimmed();
    if (normalizedEndpoint.isEmpty()) {
        setToolInstallStatus(QStringLiteral("LM Studio endpoint is required before running a voice test."));
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

    m_assistantController->saveSettings(
        normalizedEndpoint,
        modelId,
        static_cast<int>(ReasoningMode::Balanced),
        true,
        true,
        12000,
        resolvedWhisper,
        resolvedPiper,
        resolvedVoiceModel,
        resolvedFfmpeg,
        0.89,
        0.93,
        0.02,
        audioInputDeviceId,
        audioOutputDeviceId,
        clickThrough);

    const QString caller = userName.trimmed().isEmpty() ? QStringLiteral("Sir") : userName.trimmed();
    const QString testPrompt = QStringLiteral("Reply with exactly one calm, confident sentence greeting %1. No markdown.").arg(caller);
    m_assistantController->submitText(testPrompt);
    setToolInstallStatus(QStringLiteral("Voice test sent to the AI model. Listen for J.A.R.V.I.S output."));
    emit settingsChanged();
    return true;
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
    if (!piper.isEmpty()) {
        m_settings->setPiperExecutable(piper);
    }
    if (!ffmpeg.isEmpty()) {
        m_settings->setFfmpegExecutable(ffmpeg);
    }
    if (!voiceModel.isEmpty()) {
        m_settings->setPiperVoiceModel(voiceModel);
    }

    const bool complete = !m_settings->whisperExecutable().isEmpty()
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

$piperDir = Join-Path $toolsRoot 'piper'
if (-not (Test-Path (Join-Path $piperDir 'piper.exe'))) {
    Install-LatestZip -Repo 'rhasspy/piper' -NamePatterns @('*windows*amd64*.zip', '*win*amd64*.zip') -Destination $piperDir
}

$voiceDir = Join-Path $toolsRoot 'piper-voices'
New-Item -ItemType Directory -Force -Path $voiceDir | Out-Null
$voiceModel = Join-Path $voiceDir 'en_GB-alba-medium.onnx'
$voiceJson = Join-Path $voiceDir 'en_GB-alba-medium.onnx.json'

if (-not (Test-Path $voiceModel)) {
    Invoke-WebRequest -Uri 'https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_GB/alba/medium/en_GB-alba-medium.onnx?download=true' -OutFile $voiceModel
}
if (-not (Test-Path $voiceJson)) {
    Invoke-WebRequest -Uri 'https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_GB/alba/medium/en_GB-alba-medium.onnx.json?download=true' -OutFile $voiceJson
}
)POWERSHELL").arg(toolsRoot);

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
