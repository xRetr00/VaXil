#include "gui/BackendFacade.h"

#include <QDir>
#include <QDirIterator>
#include <QAudioDevice>
#include <QDesktopServices>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QVariantMap>
#include <QStandardPaths>
#include <QUuid>
#include <QUrl>
#include <QDateTime>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "companion/contracts/BehaviorTraceEvent.h"
#include "companion/contracts/FocusModeState.h"
#include "core/AssistantController.h"
#include "core/PermissionOverrideSettings.h"
#include "core/ToolPermissionRegistry.h"
#include "logging/LoggingService.h"
#include "overlay/OverlayController.h"
#include "platform/PlatformRuntime.h"
#include "settings/AppSettings.h"
#include "settings/IdentityProfileService.h"
#include "smart_home/SmartHomeRuntime.h"
#include "tools/ToolManager.h"
#include "tts/TtsVoiceProfilePolicy.h"

namespace {
struct PiperVoicePreset {
    QString id;
    QString label;
    QString modelUrl;
    QString configUrl;
    bool recommended = false;
};

struct WhisperModelPreset {
    QString id;
    QString label;
    QString modelUrl;
};

struct IntentModelPreset {
    QString id;
    QString label;
    QString toolName;
    QString relativePath;
    QString recommendation;
};

struct McpQuickServerPreset {
    QString id;
    QString name;
    QString description;
    QString packageName;
    QString packageVersion;
    QString defaultCatalogUrl;
};

struct NetworkFetchResult {
    bool ok = false;
    int statusCode = 0;
    QString error;
    QByteArray body;
};

QVariantMap toVariantMap(const SkillManifest &skill)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), skill.id);
    map.insert(QStringLiteral("name"), skill.name);
    map.insert(QStringLiteral("version"), skill.version);
    map.insert(QStringLiteral("description"), skill.description);
    map.insert(QStringLiteral("promptTemplatePath"), skill.promptTemplatePath);
    return map;
}

QVariantMap toVariantMap(const AgentToolSpec &tool)
{
    QVariantMap map;
    map.insert(QStringLiteral("name"), tool.name);
    map.insert(QStringLiteral("description"), tool.description);
    map.insert(QStringLiteral("parameters"), QString::fromStdString(tool.parameters.dump(2)));
    return map;
}

QVariantMap toVariantMap(const PermissionCapabilityInfo &capability)
{
    QVariantMap map;
    map.insert(QStringLiteral("capabilityId"), capability.capabilityId);
    map.insert(QStringLiteral("label"), capability.label);
    map.insert(QStringLiteral("description"), capability.description);
    map.insert(QStringLiteral("defaultScope"), capability.defaultScope);
    return map;
}

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
            QStringLiteral("Alba Medium  |  UK  |  Calm"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_GB/alba/medium/en_GB-alba-medium.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_GB/alba/medium/en_GB-alba-medium.onnx.json?download=true"),
            false
        },
        {
            QStringLiteral("en_GB-alan-medium"),
            QStringLiteral("Alan Medium  |  UK  |  Neutral male"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_GB/alan/medium/en_GB-alan-medium.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_GB/alan/medium/en_GB-alan-medium.onnx.json?download=true"),
            false
        },
        {
            QStringLiteral("en_GB-cori-high"),
            QStringLiteral("Cori High  |  UK  |  Recommended quality"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_GB/cori/high/en_GB-cori-high.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_GB/cori/high/en_GB-cori-high.onnx.json?download=true"),
            true
        },
        {
            QStringLiteral("en_GB-northern_english_male-medium"),
            QStringLiteral("Northern English Male  |  UK  |  Deep tone"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_GB/northern_english_male/medium/en_GB-northern_english_male-medium.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_GB/northern_english_male/medium/en_GB-northern_english_male-medium.onnx.json?download=true"),
            false
        },
        {
            QStringLiteral("en_GB-semaine-medium"),
            QStringLiteral("Semaine Medium  |  UK  |  Controlled female"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_GB/semaine/medium/en_GB-semaine-medium.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_GB/semaine/medium/en_GB-semaine-medium.onnx.json?download=true"),
            false
        },
        {
            QStringLiteral("en_US-ryan-medium"),
            QStringLiteral("Ryan Medium  |  US  |  Neutral male"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/ryan/medium/en_US-ryan-medium.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/ryan/medium/en_US-ryan-medium.onnx.json?download=true"),
            false
        },
        {
            QStringLiteral("en_US-ryan-high"),
            QStringLiteral("Ryan High  |  US  |  Recommended quality"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/ryan/high/en_US-ryan-high.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/ryan/high/en_US-ryan-high.onnx.json?download=true"),
            true
        },
        {
            QStringLiteral("en_US-lessac-medium"),
            QStringLiteral("Lessac Medium  |  US  |  Clear neutral"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx.json?download=true"),
            false
        },
        {
            QStringLiteral("en_US-lessac-high"),
            QStringLiteral("Lessac High  |  US  |  Recommended quality"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/high/en_US-lessac-high.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/high/en_US-lessac-high.onnx.json?download=true"),
            true
        },
        {
            QStringLiteral("en_US-libritts-high"),
            QStringLiteral("LibriTTS High  |  US  |  Recommended quality"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/libritts/high/en_US-libritts-high.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/libritts/high/en_US-libritts-high.onnx.json?download=true"),
            true
        },
        {
            QStringLiteral("en_US-ljspeech-high"),
            QStringLiteral("LJSpeech High  |  US  |  Expressive female"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/ljspeech/high/en_US-ljspeech-high.onnx?download=true"),
            QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/ljspeech/high/en_US-ljspeech-high.onnx.json?download=true"),
            true
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

const QList<WhisperModelPreset> &whisperModelPresets()
{
    static const QList<WhisperModelPreset> presets{
        {
            QStringLiteral("ggml-tiny.en"),
            QStringLiteral("Tiny English  |  Fastest  |  Lowest accuracy"),
            QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin?download=true")
        },
        {
            QStringLiteral("ggml-base.en"),
            QStringLiteral("Base English  |  Recommended balance"),
            QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin?download=true")
        },
        {
            QStringLiteral("ggml-small.en"),
            QStringLiteral("Small English  |  Better accuracy"),
            QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin?download=true")
        },
        {
            QStringLiteral("ggml-base"),
            QStringLiteral("Base Multilingual  |  General use"),
            QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin?download=true")
        }
    };

    return presets;
}

const QList<IntentModelPreset> &intentModelPresets()
{
    static const QList<IntentModelPreset> presets{
        {
            QStringLiteral("intent-minilm-int8"),
            QStringLiteral("MiniLM Intent INT8  |  Fastest  |  Best for 8 GB and below"),
            QStringLiteral("intent-minilm-int8"),
            QStringLiteral("models/intent/intent-minilm-int8/model.onnx"),
            QStringLiteral("Recommended for low-memory CPUs and always-on background use.")
        },
        {
            QStringLiteral("intent-minilm-q4f16"),
            QStringLiteral("MiniLM Intent Q4F16  |  Balanced  |  Best for mid-range CPUs"),
            QStringLiteral("intent-minilm-q4f16"),
            QStringLiteral("models/intent/intent-minilm-q4f16/model.onnx"),
            QStringLiteral("Recommended for typical desktop hardware with 8-16 GB RAM.")
        },
        {
            QStringLiteral("intent-minilm-fp32"),
            QStringLiteral("MiniLM Intent FP32  |  Highest quality  |  Best for strong desktops"),
            QStringLiteral("intent-minilm-fp32"),
            QStringLiteral("models/intent/intent-minilm-fp32/model.onnx"),
            QStringLiteral("Recommended when RAM and CPU headroom are available and latency is less critical.")
        }
    };

    return presets;
}

const IntentModelPreset *findIntentModelPreset(const QString &modelId)
{
    for (const IntentModelPreset &preset : intentModelPresets()) {
        if (preset.id == modelId || preset.toolName == modelId) {
            return &preset;
        }
    }

    return nullptr;
}

const QList<McpQuickServerPreset> &mcpQuickServerPresets()
{
    static const QList<McpQuickServerPreset> presets{
        {
            QStringLiteral("playwright"),
            QStringLiteral("Playwright Browser Automation"),
            QStringLiteral("Official Playwright MCP server for browser automation tasks."),
            QStringLiteral("@playwright/mcp"),
            QStringLiteral("latest"),
            QStringLiteral("https://registry.modelcontextprotocol.io/")
        },
        {
            QStringLiteral("filesystem"),
            QStringLiteral("Filesystem Tools"),
            QStringLiteral("Official MCP filesystem server for safe local file operations."),
            QStringLiteral("@modelcontextprotocol/server-filesystem"),
            QStringLiteral("latest"),
            QStringLiteral("https://registry.modelcontextprotocol.io/")
        },
        {
            QStringLiteral("memory"),
            QStringLiteral("Persistent Memory"),
            QStringLiteral("Official MCP memory server for persistent knowledge storage."),
            QStringLiteral("@modelcontextprotocol/server-memory"),
            QStringLiteral("latest"),
            QStringLiteral("https://registry.modelcontextprotocol.io/")
        },
        {
            QStringLiteral("brave-search"),
            QStringLiteral("Brave Search"),
            QStringLiteral("Official MCP Brave Search server for web-search fallback when direct web tools fail."),
            QStringLiteral("@modelcontextprotocol/server-brave-search"),
            QStringLiteral("latest"),
            QStringLiteral("https://registry.modelcontextprotocol.io/")
        }
    };

    return presets;
}

const McpQuickServerPreset *findMcpQuickServerPreset(const QString &presetId)
{
    for (const McpQuickServerPreset &preset : mcpQuickServerPresets()) {
        if (preset.id == presetId) {
            return &preset;
        }
    }

    return nullptr;
}

QString mcpToolsRootPath()
{
    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataRoot.isEmpty()) {
        return {};
    }
    return appDataRoot + QStringLiteral("/tools/mcp");
}

QString mcpPackageManifestPath(const QString &rootPath, const QString &packageName)
{
    const QString normalized = packageName.trimmed();
    if (rootPath.isEmpty() || normalized.isEmpty()) {
        return {};
    }

    const QStringList parts = normalized.split(QStringLiteral("/"), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return {};
    }

    if (normalized.startsWith(QStringLiteral("@")) && parts.size() >= 2) {
        return rootPath + QStringLiteral("/node_modules/") + parts[0] + QStringLiteral("/") + parts[1] + QStringLiteral("/package.json");
    }

    return rootPath + QStringLiteral("/node_modules/") + parts[0] + QStringLiteral("/package.json");
}

QVariantMap probeMcpPackageStatus(const QString &mcpRootPath, const QString &packageName)
{
    QVariantMap status;
    status.insert(QStringLiteral("installed"), false);
    status.insert(QStringLiteral("status"), QStringLiteral("missing"));
    status.insert(QStringLiteral("statusLabel"), QStringLiteral("Not installed"));
    status.insert(QStringLiteral("installedVersion"), QString());

    const QString manifestPath = mcpPackageManifestPath(mcpRootPath, packageName);
    QFile manifestFile(manifestPath);
    if (!manifestFile.exists()) {
        return status;
    }

    if (!manifestFile.open(QIODevice::ReadOnly)) {
        status.insert(QStringLiteral("status"), QStringLiteral("broken"));
        status.insert(QStringLiteral("statusLabel"), QStringLiteral("Installed (unreadable)"));
        return status;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll());
    manifestFile.close();
    if (!doc.isObject()) {
        status.insert(QStringLiteral("status"), QStringLiteral("broken"));
        status.insert(QStringLiteral("statusLabel"), QStringLiteral("Installed (invalid manifest)"));
        return status;
    }

    const QJsonObject obj = doc.object();
    const QString installedVersion = obj.value(QStringLiteral("version")).toString();
    const QJsonValue binValue = obj.value(QStringLiteral("bin"));
    const bool hasRunnableEntry = (binValue.isString() && !binValue.toString().trimmed().isEmpty())
        || (binValue.isObject() && !binValue.toObject().isEmpty());

    status.insert(QStringLiteral("installed"), true);
    status.insert(QStringLiteral("installedVersion"), installedVersion);
    if (hasRunnableEntry) {
        status.insert(QStringLiteral("status"), QStringLiteral("working"));
        status.insert(QStringLiteral("statusLabel"), QStringLiteral("Working"));
    } else {
        status.insert(QStringLiteral("status"), QStringLiteral("installed"));
        status.insert(QStringLiteral("statusLabel"), QStringLiteral("Installed (entrypoint unknown)"));
    }

    return status;
}

quint64 totalMemoryBytes()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return static_cast<quint64>(status.ullTotalPhys);
    }
#endif
    return 0;
}

QString detectHardwareTier()
{
    const int logicalCores = QThread::idealThreadCount() > 1 ? QThread::idealThreadCount() : 1;
    const double memoryGb = static_cast<double>(totalMemoryBytes()) / (1024.0 * 1024.0 * 1024.0);
    if (memoryGb > 0.0 && (memoryGb <= 8.5 || logicalCores <= 4)) {
        return QStringLiteral("entry");
    }
    if (memoryGb > 0.0 && (memoryGb <= 16.5 || logicalCores <= 8)) {
        return QStringLiteral("balanced");
    }
    return QStringLiteral("performance");
}

QString recommendedIntentModelId()
{
    const QString tier = detectHardwareTier();
    if (tier == QStringLiteral("entry")) {
        return QStringLiteral("intent-minilm-int8");
    }
    if (tier == QStringLiteral("balanced")) {
        return QStringLiteral("intent-minilm-q4f16");
    }
    return QStringLiteral("intent-minilm-fp32");
}

QString intentHardwareSummary()
{
    const int logicalCores = QThread::idealThreadCount() > 1 ? QThread::idealThreadCount() : 1;
    const double memoryGb = static_cast<double>(totalMemoryBytes()) / (1024.0 * 1024.0 * 1024.0);
    const QString tier = detectHardwareTier();
    const QString ramLabel = memoryGb > 0.0
        ? QString::number(memoryGb, 'f', 1) + QStringLiteral(" GB RAM")
        : QStringLiteral("RAM unknown");
    return QStringLiteral("%1 logical cores  |  %2  |  %3 tier")
        .arg(logicalCores)
        .arg(ramLabel)
        .arg(tier.left(1).toUpper() + tier.mid(1));
}

QString resolveIntentModelPath(const QString &modelId)
{
    const IntentModelPreset *preset = findIntentModelPreset(modelId);
    if (preset == nullptr) {
        return {};
    }

    const QStringList candidates = {
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/third_party/") + preset->relativePath,
        QDir::currentPath() + QStringLiteral("/third_party/") + preset->relativePath,
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/third_party/") + preset->relativePath,
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/") + preset->relativePath
    };

    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    return {};
}

const WhisperModelPreset *findWhisperModelPreset(const QString &modelId)
{
    for (const WhisperModelPreset &preset : whisperModelPresets()) {
        if (preset.id == modelId) {
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

QString detectWhisperModelPresetIdFromPath(const QString &path)
{
    const QString fileName = QFileInfo(path).fileName();
    for (const WhisperModelPreset &preset : whisperModelPresets()) {
        if (fileName.compare(preset.id + QStringLiteral(".bin"), Qt::CaseInsensitive) == 0) {
            return preset.id;
        }
    }

    return {};
}

QString piperVoicesRoot(const QString &appDataRoot)
{
    return appDataRoot + QStringLiteral("/tools/piper-voices");
}

QString whisperModelsRoot(const QString &appDataRoot)
{
    return appDataRoot + QStringLiteral("/tools/whisper/models");
}

QString qwenToolsRoot(const QString &appDataRoot)
{
    return appDataRoot + QStringLiteral("/tools/qwen3-tts");
}

QString qwenModelRoot(const QString &appDataRoot)
{
    return qwenToolsRoot(appDataRoot) + QStringLiteral("/models/0.6b-base");
}

bool hasQwenModelAssets(const QString &modelDir)
{
    const QDir dir(modelDir);
    if (!dir.exists()) {
        return false;
    }
    const bool hasMainModel = !dir.entryList({QStringLiteral("qwen3-tts-0.6b*.gguf")}, QDir::Files).isEmpty();
    const bool hasTokenizer = !dir.entryList({QStringLiteral("qwen3-tts-tokenizer*.gguf")}, QDir::Files).isEmpty();
    return hasMainModel && hasTokenizer;
}

QString joinExecutableNames(const QStringList &names)
{
    QStringList quoted;
    for (const QString &name : names) {
        quoted.push_back(QStringLiteral("`%1`").arg(name));
    }
    return quoted.join(QStringLiteral(", "));
}

QString compactBackendSurfaceText(QString text, int maxLength = 60)
{
    text = text.simplified();
    if (text.size() > maxLength) {
        text = text.left(maxLength - 3).trimmed() + QStringLiteral("...");
    }
    return text;
}

QString whisperExecutableValidationMessage()
{
    return PlatformRuntime::isWindows()
        ? QStringLiteral("Whisper executable is invalid. Use whisper-cli.exe or main.exe from the whisper Release folder.")
        : QStringLiteral("Whisper executable is invalid. Select one of %1 from your Linux install.")
            .arg(joinExecutableNames(PlatformRuntime::whisperExecutableNames()));
}

QString piperExecutableValidationMessage()
{
    return PlatformRuntime::isWindows()
        ? QStringLiteral("Piper executable is invalid. Select piper.exe.")
        : QStringLiteral("Piper executable is invalid. Select %1.")
            .arg(joinExecutableNames(PlatformRuntime::piperExecutableNames()));
}

QString ffmpegExecutableValidationMessage()
{
    return PlatformRuntime::isWindows()
        ? QStringLiteral("FFmpeg executable is invalid. Select ffmpeg.exe.")
        : QStringLiteral("FFmpeg executable is invalid. Select %1.")
            .arg(joinExecutableNames(PlatformRuntime::ffmpegExecutableNames()));
}

QString autoInstallUnavailableMessage()
{
    return QStringLiteral("Automatic downloads are not supported on this platform.");
}

QString quotePowerShell(const QString &value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("'"), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(escaped);
}

bool downloadFileWithPowerShell(const QString &url, const QString &destinationPath, int timeoutMs, QString *error)
{
    QDir().mkpath(QFileInfo(destinationPath).absolutePath());

#ifdef Q_OS_WIN
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
        process.waitForFinished(2000);
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
#else
    QProcess process;
    QString program = QStringLiteral("curl");
    QStringList args = {
        QStringLiteral("-L"),
        QStringLiteral("--fail"),
        QStringLiteral("--silent"),
        QStringLiteral("--show-error"),
        QStringLiteral("-o"),
        destinationPath,
        url
    };

    process.start(program, args);
    if (!process.waitForStarted(3000)) {
        program = QStringLiteral("wget");
        args = {
            QStringLiteral("-q"),
            QStringLiteral("-O"),
            destinationPath,
            url
        };
        process.start(program, args);
    }

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
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
                *error = QStringLiteral("Download failed. Ensure curl or wget is installed.");
            }
        }
        return false;
    }

    return QFileInfo::exists(destinationPath);
#endif
}

NetworkFetchResult httpGet(const QUrl &url,
                           const QList<QPair<QByteArray, QByteArray>> &headers = {},
                           int timeoutMs = 20000)
{
    NetworkFetchResult result;
    if (!url.isValid()) {
        result.error = QStringLiteral("Invalid URL.");
        return result;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VAXIL/1.0"));
    for (const auto &header : headers) {
        request.setRawHeader(header.first, header.second);
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (timer.isActive()) {
        timer.stop();
    } else {
        reply->abort();
        result.error = QStringLiteral("Request timed out.");
        reply->deleteLater();
        return result;
    }

    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    result.ok = reply->error() == QNetworkReply::NoError;
    if (!result.ok) {
        result.error = reply->errorString();
    }
    reply->deleteLater();
    return result;
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

    const QString fromPath = QStandardPaths::findExecutable(trimmed);
    if (!fromPath.isEmpty()) {
        QFileInfo resolvedInfo(fromPath);
        for (const QString &name : candidateNames) {
            if (resolvedInfo.fileName().compare(name, Qt::CaseInsensitive) == 0) {
                return resolvedInfo.absoluteFilePath();
            }
        }
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

QString resolveExistingFileSelection(const QString &selection, const QStringList &exactFileNames, const QStringList &fallbackPatterns = {})
{
    const QString trimmed = selection.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QFileInfo info(trimmed);
    if (info.exists() && info.isFile()) {
        if (exactFileNames.isEmpty()) {
            return info.absoluteFilePath();
        }
        for (const QString &fileName : exactFileNames) {
            if (info.fileName().compare(fileName, Qt::CaseInsensitive) == 0) {
                return info.absoluteFilePath();
            }
        }
        return {};
    }

    if (info.exists() && info.isDir()) {
        for (const QString &fileName : exactFileNames) {
            const QString match = findFileRecursive(info.absoluteFilePath(), fileName);
            if (!match.isEmpty()) {
                return match;
            }
        }
        if (!fallbackPatterns.isEmpty()) {
            return findFirstMatchingFileRecursive(info.absoluteFilePath(), fallbackPatterns);
        }
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
        process.waitForFinished(2000);
        return {};
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput())
        + QString::fromUtf8(process.readAllStandardError());
    return extractVersionToken(output);
}

QString fetchLatestReleaseTag(const QString &repo)
{
    const NetworkFetchResult fetch = httpGet(
        QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repo)),
        {{QByteArray("Accept"), QByteArray("application/vnd.github+json")}},
        6000);
    if (!fetch.ok) {
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(fetch.body);
    if (!doc.isObject()) {
        return {};
    }
    return doc.object().value(QStringLiteral("tag_name")).toString().trimmed();
}

bool looksLatestEnough(const QString &installedVersion, const QString &latestTag)
{
    if (installedVersion.isEmpty() || latestTag.isEmpty()) {
        return false;
    }

    return latestTag.contains(installedVersion, Qt::CaseInsensitive);
}

QVariantList compactOverrideRows(const QVariantList &rows)
{
    QVariantList compact;
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        compact.push_back(QVariantMap{
            {QStringLiteral("capabilityId"), row.value(QStringLiteral("capabilityId")).toString()},
            {QStringLiteral("decision"), row.value(QStringLiteral("decision")).toString()},
            {QStringLiteral("scope"), row.value(QStringLiteral("scope")).toString()}
        });
    }
    return compact;
}

QString overrideReasonCode(const QVariantList &before, const QVariantList &after)
{
    if (before.isEmpty() && !after.isEmpty()) {
        return QStringLiteral("permission_overrides.created");
    }
    if (!before.isEmpty() && after.isEmpty()) {
        return QStringLiteral("permission_overrides.cleared");
    }
    return QStringLiteral("permission_overrides.changed");
}
}

BackendFacade::BackendFacade(
    AppSettings *settings,
    IdentityProfileService *identityProfileService,
    AssistantController *assistantController,
    OverlayController *overlayController,
    LoggingService *loggingService,
    QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_identityProfileService(identityProfileService)
    , m_assistantController(assistantController)
    , m_overlayController(overlayController)
    , m_loggingService(loggingService)
{
    m_toolManager = new ToolManager(this);
    connect(m_assistantController, &AssistantController::stateChanged, this, &BackendFacade::stateNameChanged);
    connect(m_assistantController, &AssistantController::transcriptChanged, this, &BackendFacade::transcriptChanged);
    connect(m_assistantController, &AssistantController::responseTextChanged, this, &BackendFacade::responseTextChanged);
    connect(m_assistantController, &AssistantController::statusTextChanged, this, &BackendFacade::statusTextChanged);
    connect(m_assistantController, &AssistantController::assistantSurfaceChanged, this, &BackendFacade::assistantSurfaceChanged);
    connect(m_assistantController, &AssistantController::audioLevelChanged, this, &BackendFacade::audioLevelChanged);
    connect(m_assistantController, &AssistantController::wakeTriggerTokenChanged, this, &BackendFacade::wakeTriggerTokenChanged);
    connect(m_assistantController, &AssistantController::modelsChanged, this, [this]() {
        emit modelsChanged();
        emit selectedModelChanged();
    });
    connect(m_assistantController, &AssistantController::agentStateChanged, this, &BackendFacade::agentStateChanged);
    connect(m_assistantController, &AssistantController::agentTraceChanged, this, &BackendFacade::agentTraceChanged);
    connect(m_assistantController, &AssistantController::backgroundTaskResultsChanged, this, &BackendFacade::backgroundTaskResultsChanged);
    connect(m_assistantController, &AssistantController::backgroundPanelVisibleChanged, this, &BackendFacade::backgroundPanelVisibleChanged);
    connect(m_assistantController, &AssistantController::latestTaskToastChanged, this, &BackendFacade::latestTaskToastChanged);
    connect(m_assistantController, &AssistantController::latestProactiveSuggestionChanged, this, &BackendFacade::latestProactiveSuggestionChanged);
    connect(m_overlayController, &OverlayController::visibilityChanged, this, &BackendFacade::overlayVisibleChanged);
    connect(m_overlayController, &OverlayController::presenceOffsetChanged, this, &BackendFacade::presenceOffsetChanged);
    connect(m_settings, &AppSettings::settingsChanged, this, &BackendFacade::settingsChanged);

    auto *mediaDevices = new QMediaDevices(this);
    connect(mediaDevices, &QMediaDevices::audioInputsChanged, this, &BackendFacade::audioDevicesChanged);
    connect(mediaDevices, &QMediaDevices::audioOutputsChanged, this, &BackendFacade::audioDevicesChanged);
    connect(m_toolManager, &ToolManager::toolsUpdated, this, &BackendFacade::toolStatusesChanged);
    connect(m_toolManager, &ToolManager::downloadProgress, this, [this](const QString &name, qint64 received, qint64 total) {
        clearSurfaceBackendError();
        QString primary = m_surfaceToolActivityPrimary;
        if (primary.isEmpty()) {
            primary = findIntentModelPreset(name) != nullptr
                ? QStringLiteral("Downloading model...")
                : QStringLiteral("Installing tool...");
        }

        QString secondary = compactBackendSurfaceText(name, 44);
        if (total > 0) {
            const int percent = static_cast<int>((received * 100) / total);
            secondary = secondary.isEmpty()
                ? QStringLiteral("%1%").arg(percent)
                : QStringLiteral("%1  %2%").arg(secondary).arg(percent);
        }
        setSurfaceToolActivity(primary, secondary);
        setToolInstallStatus(total > 0
            ? QStringLiteral("%1: %2 / %3 bytes").arg(name).arg(received).arg(total)
            : QStringLiteral("%1: %2 bytes").arg(name).arg(received));
    });
    connect(m_toolManager, &ToolManager::downloadFinished, this, [this](const QString &name, bool success, const QString &message) {
        if (success) {
            if (const IntentModelPreset *preset = findIntentModelPreset(name); preset != nullptr) {
                const QString resolvedPath = resolveIntentModelPath(preset->id);
                if (!resolvedPath.isEmpty()) {
                    m_settings->setIntentModelPath(resolvedPath);
                    m_settings->setSelectedIntentModelId(preset->id);
                    m_settings->save();
                }
            } else {
                autoDetectVoiceTools();
            }
        }
        clearSurfaceToolActivity();
        setToolInstallStatus(QStringLiteral("%1: %2").arg(name, success ? message : QStringLiteral("failed - %1").arg(message)));
        if (!success) {
            setSurfaceBackendError(QStringLiteral("Install failed"), compactBackendSurfaceText(name + QStringLiteral("  ") + message, 60));
        }
        emit toolStatusesChanged();
    });
    m_toolManager->rescan();
}

void BackendFacade::logOrbRendererStatus(const QString &eventName, const QVariantMap &details)
{
    if (m_loggingService == nullptr) {
        return;
    }

    QVariantMap diagnosticDetails = details;
    diagnosticDetails.insert(QStringLiteral("authoritativeStateName"), stateName());
    diagnosticDetails.insert(QStringLiteral("authoritativeSurfaceState"), assistantSurfaceState());

    const QString normalizedEvent = eventName.trimmed().isEmpty()
        ? QStringLiteral("status")
        : eventName.trimmed().toLower();
    const QString tag = diagnosticDetails.value(QStringLiteral("tag")).toString().trimmed();
    const QString rateLimitKey = normalizedEvent + QStringLiteral("|")
        + (tag.isEmpty() ? QStringLiteral("default") : tag);

    if (normalizedEvent == QStringLiteral("heartbeat")) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 previousMs = m_orbHeartbeatLogTimes.value(rateLimitKey, 0);
        if (previousMs > 0 && (nowMs - previousMs) < 10000) {
            return;
        }
        m_orbHeartbeatLogTimes.insert(rateLimitKey, nowMs);
    }

    const QString payloadText = QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(diagnosticDetails)).toJson(QJsonDocument::Compact));
    const QString message = QStringLiteral("[orb] event=\"%1\" details=%2")
        .arg(normalizedEvent, payloadText);

    if (normalizedEvent.contains(QStringLiteral("error")) || normalizedEvent.contains(QStringLiteral("failed"))) {
        m_loggingService->errorFor(QStringLiteral("orb_render"), message);
        return;
    }
    if (normalizedEvent.contains(QStringLiteral("warn"))) {
        m_loggingService->warnFor(QStringLiteral("orb_render"), message);
        return;
    }

    m_loggingService->infoFor(QStringLiteral("orb_render"), message);
}

bool BackendFacade::saveSmartHomeSettings(bool enabled,
                                          const QString &provider,
                                          const QString &baseUrl,
                                          const QString &tokenEnvVar,
                                          const QString &presenceEntityId,
                                          const QString &lightEntityId,
                                          const QString &identityMode,
                                          const QString &identityEntityId,
                                          int pollIntervalMs,
                                          int requestTimeoutMs,
                                          bool sensorOnlyWelcomeEnabled,
                                          int welcomeCooldownMinutes,
                                          int roomAbsenceGraceMinutes,
                                          int identityMissingTimeoutMinutes,
                                          const QString &bleBeaconUuid,
                                          int bleMissingTimeoutMinutes,
                                          int bleScanIntervalMs,
                                          int bleRssiThreshold,
                                          bool welcomeEnabled,
                                          bool welcomeCooldownEnabled,
                                          bool personalWelcomeEnabled,
                                          bool unknownOccupantBlocksWelcomeEnabled,
                                          bool unknownOccupantSpokenAlertsEnabled,
                                          const QString &personalWelcomeTemplate,
                                          const QString &personalWelcomeWithAlertTemplate,
                                          const QString &unknownOccupantMessageTemplate,
                                          const QString &unknownOccupantAlertResponseTemplate)
{
    if (!m_settings) {
        return false;
    }
    m_settings->setSmartHomeEnabled(enabled);
    m_settings->setSmartHomeProvider(provider);
    m_settings->setSmartHomeHomeAssistantBaseUrl(baseUrl);
    m_settings->setSmartHomeHomeAssistantTokenEnvVar(tokenEnvVar);
    m_settings->setSmartHomePresenceEntityId(presenceEntityId);
    m_settings->setSmartHomeLightEntityId(lightEntityId);
    m_settings->setSmartHomeIdentityMode(identityMode);
    m_settings->setSmartHomeHomeAssistantIdentityEntityId(identityEntityId);
    m_settings->setSmartHomePollIntervalMs(pollIntervalMs);
    m_settings->setSmartHomeRequestTimeoutMs(requestTimeoutMs);
    m_settings->setSmartHomeSensorOnlyWelcomeEnabled(sensorOnlyWelcomeEnabled);
    m_settings->setSmartHomeWelcomeCooldownMinutes(welcomeCooldownMinutes);
    m_settings->setSmartHomeRoomAbsenceGraceMinutes(roomAbsenceGraceMinutes);
    m_settings->setSmartHomeIdentityMissingTimeoutMinutes(identityMissingTimeoutMinutes);
    m_settings->setSmartHomeBleBeaconUuid(bleBeaconUuid);
    m_settings->setSmartHomeBleMissingTimeoutMinutes(bleMissingTimeoutMinutes);
    m_settings->setSmartHomeBleScanIntervalMs(bleScanIntervalMs);
    m_settings->setSmartHomeBleRssiThreshold(bleRssiThreshold);
    m_settings->setSmartHomeWelcomeEnabled(welcomeEnabled);
    m_settings->setSmartHomeWelcomeCooldownEnabled(welcomeCooldownEnabled);
    m_settings->setSmartHomePersonalWelcomeEnabled(personalWelcomeEnabled);
    m_settings->setSmartHomeUnknownOccupantBlocksWelcomeEnabled(unknownOccupantBlocksWelcomeEnabled);
    m_settings->setSmartHomeUnknownOccupantSpokenAlertsEnabled(unknownOccupantSpokenAlertsEnabled);
    m_settings->setSmartHomePersonalWelcomeTemplate(personalWelcomeTemplate);
    m_settings->setSmartHomePersonalWelcomeWithAlertTemplate(personalWelcomeWithAlertTemplate);
    m_settings->setSmartHomeUnknownOccupantMessageTemplate(unknownOccupantMessageTemplate);
    m_settings->setSmartHomeUnknownOccupantAlertResponseTemplate(unknownOccupantAlertResponseTemplate);
    const bool saved = m_settings->save();
    emit settingsChanged();
    return saved;
}

QString BackendFacade::testSmartHomeWelcome() const
{
    SmartRoomUnknownOccupantEvent emptyEvent;
    return SmartHomeRuntime::renderTemplate(smartHomePersonalWelcomeTemplate(), userName(), emptyEvent);
}

QString BackendFacade::testSmartHomeOccupancyAlert() const
{
    SmartRoomUnknownOccupantEvent event;
    event.hasEvent = true;
    event.firstDetectedAtUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    event.lastSeenAtUtcMs = event.firstDetectedAtUtcMs;
    return SmartHomeRuntime::renderTemplate(smartHomeUnknownOccupantAlertResponseTemplate(), userName(), event);
}

QString BackendFacade::stateName() const { return m_assistantController->stateName(); }
QString BackendFacade::transcript() const { return m_assistantController->transcript(); }
QString BackendFacade::responseText() const { return m_assistantController->responseText(); }
QString BackendFacade::statusText() const { return m_assistantController->statusText(); }
int BackendFacade::assistantSurfaceState() const
{
    if (!m_surfaceBackendErrorPrimary.isEmpty()) {
        return static_cast<int>(AssistantSurfaceState::Error);
    }

    const AssistantSurfaceState controllerState = m_assistantController->assistantSurfaceState();
    if (controllerState == AssistantSurfaceState::Error) {
        return static_cast<int>(AssistantSurfaceState::Error);
    }

    if (m_surfaceToolActivityActive && !m_surfaceToolActivityPrimary.isEmpty()) {
        return static_cast<int>(AssistantSurfaceState::ToolRunning);
    }

    return static_cast<int>(controllerState);
}

QString BackendFacade::assistantSurfaceActivityPrimary() const
{
    if (!m_surfaceBackendErrorPrimary.isEmpty()) {
        return m_surfaceBackendErrorPrimary;
    }

    if (m_assistantController->assistantSurfaceState() == AssistantSurfaceState::Error) {
        return m_assistantController->assistantSurfaceActivityPrimary();
    }

    if (m_surfaceToolActivityActive && !m_surfaceToolActivityPrimary.isEmpty()) {
        return m_surfaceToolActivityPrimary;
    }

    return m_assistantController->assistantSurfaceActivityPrimary();
}

QString BackendFacade::assistantSurfaceActivitySecondary() const
{
    if (!m_surfaceBackendErrorPrimary.isEmpty()) {
        return m_surfaceBackendErrorSecondary;
    }

    if (m_assistantController->assistantSurfaceState() == AssistantSurfaceState::Error) {
        return m_assistantController->assistantSurfaceActivitySecondary();
    }

    if (m_surfaceToolActivityActive) {
        return m_surfaceToolActivitySecondary;
    }

    return m_assistantController->assistantSurfaceActivitySecondary();
}
double BackendFacade::audioLevel() const { return m_assistantController->audioLevel(); }
int BackendFacade::wakeTriggerToken() const { return m_assistantController->wakeTriggerToken(); }
QStringList BackendFacade::models() const { return m_assistantController->availableModelIds(); }
QString BackendFacade::selectedModel() const { return m_assistantController->selectedModel(); }
QStringList BackendFacade::voicePresetNames() const
{
    QStringList values;
    const QString currentPath = QFileInfo(m_settings->piperVoiceModel()).fileName().toLower();
    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString voiceRoot = piperVoicesRoot(appDataRoot);
    for (const PiperVoicePreset &preset : voicePresets()) {
        QString label = preset.label;
        const QString installedPath = voiceRoot + QStringLiteral("/") + preset.id + QStringLiteral(".onnx");
        const bool installed = QFileInfo::exists(installedPath);
        if (preset.recommended) {
            label += QStringLiteral("  [Recommended]");
        }
        if (installed) {
            label += QStringLiteral("  [Installed]");
        }
        if (!currentPath.isEmpty()
            && currentPath.compare(preset.id + QStringLiteral(".onnx"), Qt::CaseInsensitive) == 0) {
            label = QStringLiteral("Current  |  ") + label;
        }
        values.push_back(label);
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
QStringList BackendFacade::whisperModelPresetNames() const
{
    QStringList values;
    for (const WhisperModelPreset &preset : whisperModelPresets()) {
        values.push_back(preset.label);
    }
    return values;
}
QStringList BackendFacade::whisperModelPresetIds() const
{
    QStringList values;
    for (const WhisperModelPreset &preset : whisperModelPresets()) {
        values.push_back(preset.id);
    }
    return values;
}
QString BackendFacade::selectedWhisperModelPresetId() const
{
    return detectWhisperModelPresetIdFromPath(m_settings->whisperModelPath());
}
QStringList BackendFacade::intentModelPresetNames() const
{
    QStringList values;
    for (const IntentModelPreset &preset : intentModelPresets()) {
        values.push_back(preset.label);
    }
    return values;
}
QStringList BackendFacade::intentModelPresetIds() const
{
    QStringList values;
    for (const IntentModelPreset &preset : intentModelPresets()) {
        values.push_back(preset.id);
    }
    return values;
}
QString BackendFacade::selectedIntentModelId() const { return m_settings->selectedIntentModelId(); }
QString BackendFacade::intentModelPath() const
{
    const QString configured = m_settings->intentModelPath().trimmed();
    return configured.isEmpty() ? resolveIntentModelPath(m_settings->selectedIntentModelId()) : configured;
}
QString BackendFacade::recommendedIntentModelId() const { return ::recommendedIntentModelId(); }
QString BackendFacade::recommendedIntentModelLabel() const
{
    const IntentModelPreset *preset = findIntentModelPreset(::recommendedIntentModelId());
    return preset != nullptr ? preset->label : QStringLiteral("No recommendation available");
}
QString BackendFacade::intentHardwareSummary() const { return ::intentHardwareSummary(); }
bool BackendFacade::overlayVisible() const { return m_overlayController->isVisible(); }
double BackendFacade::presenceOffsetX() const { return m_overlayController->presenceOffsetX(); }
double BackendFacade::presenceOffsetY() const { return m_overlayController->presenceOffsetY(); }
QString BackendFacade::lmStudioEndpoint() const { return m_settings->lmStudioEndpoint(); }
QString BackendFacade::chatProviderKind() const { return m_settings->chatBackendKind(); }
QString BackendFacade::chatProviderApiKey() const { return m_settings->chatBackendApiKey(); }
int BackendFacade::defaultReasoningMode() const { return static_cast<int>(m_settings->defaultReasoningMode()); }
bool BackendFacade::autoRoutingEnabled() const { return m_settings->autoRoutingEnabled(); }
bool BackendFacade::streamingEnabled() const { return m_settings->streamingEnabled(); }
int BackendFacade::requestTimeoutMs() const { return m_settings->requestTimeoutMs(); }
bool BackendFacade::visionEnabled() const { return m_settings->visionEnabled(); }
QString BackendFacade::visionEndpoint() const { return m_settings->visionEndpoint(); }
int BackendFacade::visionTimeoutMs() const { return m_settings->visionTimeoutMs(); }
int BackendFacade::visionStaleThresholdMs() const { return m_settings->visionStaleThresholdMs(); }
bool BackendFacade::visionContextAlwaysOn() const { return m_settings->visionContextAlwaysOn(); }
double BackendFacade::visionObjectsMinConfidence() const { return m_settings->visionObjectsMinConfidence(); }
double BackendFacade::visionGesturesMinConfidence() const { return m_settings->visionGesturesMinConfidence(); }
bool BackendFacade::aecEnabled() const { return m_settings->aecEnabled(); }
bool BackendFacade::rnnoiseEnabled() const { return m_settings->rnnoiseEnabled(); }
double BackendFacade::vadSensitivity() const { return m_settings->vadSensitivity(); }
QString BackendFacade::wakeEngineKind() const { return QStringLiteral("sherpa-onnx"); }
QString BackendFacade::whisperExecutable() const { return m_settings->whisperExecutable(); }
QString BackendFacade::whisperModelPath() const { return m_settings->whisperModelPath(); }
QString BackendFacade::ttsEngineKind() const { return m_settings->ttsEngineKind(); }
QString BackendFacade::piperExecutable() const { return m_settings->piperExecutable(); }
QString BackendFacade::piperVoiceModel() const { return m_settings->piperVoiceModel(); }
QString BackendFacade::qwenTtsExecutable() const { return m_settings->qwenTtsExecutable(); }
QString BackendFacade::qwenTtsModelDir() const { return m_settings->qwenTtsModelDir(); }
QString BackendFacade::qwenTtsLanguage() const { return m_settings->qwenTtsLanguage(); }
int BackendFacade::qwenTtsThreads() const { return m_settings->qwenTtsThreads(); }
double BackendFacade::wakeTriggerThreshold() const { return m_settings->wakeTriggerThreshold(); }
int BackendFacade::wakeTriggerCooldownMs() const { return m_settings->wakeTriggerCooldownMs(); }
QString BackendFacade::ffmpegExecutable() const { return m_settings->ffmpegExecutable(); }
double BackendFacade::voiceSpeed() const { return m_settings->voiceSpeed(); }
double BackendFacade::voicePitch() const { return m_settings->voicePitch(); }
double BackendFacade::piperNoiseScale() const { return m_settings->piperNoiseScale(); }
double BackendFacade::piperNoiseW() const { return m_settings->piperNoiseW(); }
double BackendFacade::piperSentenceSilence() const { return m_settings->piperSentenceSilence(); }
QString BackendFacade::ttsPostProcessMode() const { return m_settings->ttsPostProcessMode(); }
QStringList BackendFacade::ttsPostProcessModes() const
{
    return {
        QStringLiteral("off"),
        QStringLiteral("light"),
        QStringLiteral("presence"),
        QStringLiteral("legacy")
    };
}
QStringList BackendFacade::ttsVoiceProfileNames() const
{
    return TtsVoiceProfiles::profileDisplayNames();
}
QStringList BackendFacade::ttsVoiceProfileIds() const
{
    return TtsVoiceProfiles::profileIds();
}
QString BackendFacade::ttsVoiceProfileId() const { return m_settings->ttsVoiceProfileId(); }
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
QString BackendFacade::uiMode() const { return m_settings->uiMode(); }
QString BackendFacade::assistantName() const { return m_identityProfileService->identity().assistantName; }
QString BackendFacade::userName() const
{
    return m_identityProfileService->userProfile().userName;
}
bool BackendFacade::initialSetupCompleted() const { return m_settings->initialSetupCompleted(); }
QString BackendFacade::toolInstallStatus() const { return m_toolInstallStatus; }
QVariantList BackendFacade::toolStatuses() const { return m_toolManager != nullptr ? m_toolManager->toolStatusList() : QVariantList{}; }
int BackendFacade::toolDownloadPercent() const { return m_toolManager != nullptr ? m_toolManager->activeDownloadPercent() : -1; }
QString BackendFacade::activeToolDownloadName() const { return m_toolManager != nullptr ? m_toolManager->activeDownloadName() : QString{}; }
QString BackendFacade::toolsRoot() const { return m_toolManager != nullptr ? m_toolManager->toolsRoot() : QString{}; }
QString BackendFacade::wakeWordPhrase() const { return m_settings->wakeWordPhrase(); }
bool BackendFacade::agentEnabled() const { return m_settings->agentEnabled(); }
QString BackendFacade::agentProviderMode() const { return m_settings->agentProviderMode(); }
double BackendFacade::conversationTemperature() const { return m_settings->conversationTemperature(); }
double BackendFacade::conversationTopP() const { return m_settings->conversationTopP().value_or(0.0); }
double BackendFacade::toolUseTemperature() const { return m_settings->toolUseTemperature(); }
int BackendFacade::providerTopK() const { return m_settings->providerTopK().value_or(0); }
int BackendFacade::maxOutputTokens() const { return m_settings->maxOutputTokens(); }
bool BackendFacade::budgetEnforcementDisabled() const { return m_settings->budgetEnforcementDisabled(); }
bool BackendFacade::memoryAutoWrite() const { return m_settings->memoryAutoWrite(); }
QString BackendFacade::webSearchProvider() const { return m_settings->webSearchProvider(); }
QString BackendFacade::braveSearchApiKey() const { return m_settings->braveSearchApiKey(); }
bool BackendFacade::mcpEnabled() const { return m_settings->mcpEnabled(); }
QString BackendFacade::mcpCatalogUrl() const { return m_settings->mcpCatalogUrl(); }
QString BackendFacade::mcpServerUrl() const { return m_settings->mcpServerUrl(); }
QVariantList BackendFacade::mcpQuickServers() const
{
    const QString npmExecutable = resolveExecutable(
        {
            QStringLiteral("npm"),
            QStringLiteral("npm.cmd")
        },
        {});
    const bool canInstall = !npmExecutable.isEmpty();
    const QString mcpRootPath = mcpToolsRootPath();

    QVariantList list;
    for (const McpQuickServerPreset &preset : mcpQuickServerPresets()) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), preset.id);
        row.insert(QStringLiteral("name"), preset.name);
        row.insert(QStringLiteral("description"), preset.description);
        row.insert(QStringLiteral("package"), preset.packageName);
        row.insert(QStringLiteral("version"), preset.packageVersion);
        row.insert(QStringLiteral("canInstall"), canInstall);
        row.insert(QStringLiteral("npmAvailable"), canInstall);

        const QVariantMap probe = probeMcpPackageStatus(mcpRootPath, preset.packageName);
        row.insert(QStringLiteral("installed"), probe.value(QStringLiteral("installed"), false));
        row.insert(QStringLiteral("status"), probe.value(QStringLiteral("status"), QStringLiteral("missing")));
        row.insert(QStringLiteral("statusLabel"), probe.value(QStringLiteral("statusLabel"), QStringLiteral("Not installed")));
        row.insert(QStringLiteral("installedVersion"), probe.value(QStringLiteral("installedVersion"), QString()));
        list.push_back(row);
    }
    return list;
}
bool BackendFacade::focusModeEnabled() const { return m_settings->focusModeEnabled(); }
bool BackendFacade::focusModeAllowCriticalAlerts() const { return m_settings->focusModeAllowCriticalAlerts(); }
int BackendFacade::focusModeDurationMinutes() const { return m_settings->focusModeDurationMinutes(); }
qlonglong BackendFacade::focusModeUntilEpochMs() const { return m_settings->focusModeUntilEpochMs(); }
bool BackendFacade::privateModeEnabled() const { return m_settings->privateModeEnabled(); }
QVariantList BackendFacade::permissionOverrides() const { return m_settings->permissionOverrides(); }
QVariantList BackendFacade::permissionCapabilityOptions() const
{
    QVariantList rows;
    for (const PermissionCapabilityInfo &capability : ToolPermissionRegistry::capabilityOptions()) {
        rows.push_back(toVariantMap(capability));
    }
    return rows;
}
bool BackendFacade::tracePanelEnabled() const { return m_settings->tracePanelEnabled(); }
QString BackendFacade::agentStatus() const { return m_assistantController->agentCapabilities().status; }
bool BackendFacade::agentAvailable() const { return m_settings->agentEnabled(); }
QVariantMap BackendFacade::providerUsage() const { return m_assistantController->latestProviderUsage(); }
QString BackendFacade::providerUsageSummary() const
{
    const QVariantMap usage = providerUsage();
    if (usage.isEmpty()) {
        return QStringLiteral("No usage metrics yet.");
    }

    const QString model = usage.value(QStringLiteral("model")).toString();
    const QString requestKind = usage.value(QStringLiteral("request_kind")).toString();
    const QString effort = usage.value(QStringLiteral("reasoning_effort")).toString();
    const QString prompt = usage.value(QStringLiteral("prompt_tokens")).toString();
    const QString completion = usage.value(QStringLiteral("completion_tokens")).toString();
    const QString total = usage.value(QStringLiteral("total_tokens")).toString();
    const QString reasoning = usage.value(QStringLiteral("reasoning_tokens")).toString();
    const QString cost = usage.value(QStringLiteral("usage_usd")).toString();
    const QString totalCost = usage.value(QStringLiteral("total_cost_usd")).toString();

    QStringList parts;
    if (!model.isEmpty()) {
        parts.push_back(QStringLiteral("model=%1").arg(model));
    }
    if (!requestKind.isEmpty()) {
        parts.push_back(QStringLiteral("kind=%1").arg(requestKind));
    }
    if (!effort.isEmpty()) {
        parts.push_back(QStringLiteral("effort=%1").arg(effort));
    }
    if (!prompt.isEmpty() || !completion.isEmpty() || !total.isEmpty()) {
        parts.push_back(QStringLiteral("tokens p/c/t=%1/%2/%3").arg(prompt, completion, total));
    }
    if (!reasoning.isEmpty()) {
        parts.push_back(QStringLiteral("reasoning=%1").arg(reasoning));
    }
    if (!cost.isEmpty() || !totalCost.isEmpty()) {
        parts.push_back(QStringLiteral("cost usage/total=%1/%2 USD").arg(cost, totalCost));
    }
    return parts.isEmpty() ? QStringLiteral("No usage metrics yet.") : parts.join(QStringLiteral(" | "));
}
QVariantList BackendFacade::agentTraceEntries() const
{
    QVariantList list;
    for (const auto &entry : m_assistantController->agentTrace()) {
        QVariantMap row;
        row.insert(QStringLiteral("timestamp"), entry.timestamp);
        row.insert(QStringLiteral("kind"), entry.kind);
        row.insert(QStringLiteral("title"), entry.title);
        row.insert(QStringLiteral("detail"), entry.detail);
        row.insert(QStringLiteral("success"), entry.success);
        list.push_back(row);
    }
    return list;
}
QVariantList BackendFacade::availableAgentTools() const
{
    QVariantList list;
    for (const auto &tool : m_assistantController->availableAgentTools()) {
        list.push_back(toVariantMap(tool));
    }
    return list;
}
QVariantList BackendFacade::installedSkills() const
{
    QVariantList list;
    for (const auto &skill : m_assistantController->installedSkills()) {
        list.push_back(toVariantMap(skill));
    }
    return list;
}
QVariantList BackendFacade::backgroundTaskResults() const
{
    QVariantList list;
    for (const auto &entry : m_assistantController->backgroundTaskResults()) {
        QVariantMap row;
        row.insert(QStringLiteral("taskId"), entry.taskId);
        row.insert(QStringLiteral("type"), entry.type);
        row.insert(QStringLiteral("success"), entry.success);
        row.insert(QStringLiteral("state"), static_cast<int>(entry.state));
        row.insert(QStringLiteral("title"), entry.title);
        row.insert(QStringLiteral("summary"), entry.summary);
        row.insert(QStringLiteral("detail"), entry.detail);
        row.insert(QStringLiteral("payload"), QString::fromUtf8(QJsonDocument(entry.payload).toJson(QJsonDocument::Indented)));
        row.insert(QStringLiteral("finishedAt"), entry.finishedAt);
        list.push_back(row);
    }
    return list;
}
bool BackendFacade::backgroundPanelVisible() const { return m_assistantController->backgroundPanelVisible(); }
QString BackendFacade::latestTaskToast() const { return m_assistantController->latestTaskToast(); }
QString BackendFacade::latestTaskToastTone() const { return m_assistantController->latestTaskToastTone(); }
int BackendFacade::latestTaskToastTaskId() const { return m_assistantController->latestTaskToastTaskId(); }
QString BackendFacade::latestTaskToastType() const { return m_assistantController->latestTaskToastType(); }
QString BackendFacade::latestProactiveSuggestion() const { return m_assistantController->latestProactiveSuggestion(); }
QString BackendFacade::latestProactiveSuggestionTone() const { return m_assistantController->latestProactiveSuggestionTone(); }
QString BackendFacade::latestProactiveSuggestionType() const { return m_assistantController->latestProactiveSuggestionType(); }
QString BackendFacade::platformName() const { return PlatformRuntime::platformLabel(); }
QVariantMap BackendFacade::platformCapabilities() const { return platformCapabilitiesToVariantMap(PlatformRuntime::currentCapabilities()); }
bool BackendFacade::supportsAutoToolInstall() const { return PlatformRuntime::currentCapabilities().supportsAutoToolInstall; }
QString BackendFacade::skillsRoot() const { return QDir::currentPath() + QStringLiteral("/skills"); }
bool BackendFacade::smartHomeEnabled() const { return m_settings && m_settings->smartHomeEnabled(); }
QString BackendFacade::smartHomeProvider() const { return m_settings ? m_settings->smartHomeProvider() : QStringLiteral("home_assistant"); }
QString BackendFacade::smartHomeHomeAssistantBaseUrl() const { return m_settings ? m_settings->smartHomeHomeAssistantBaseUrl() : QString(); }
QString BackendFacade::smartHomeHomeAssistantTokenEnvVar() const { return m_settings ? m_settings->smartHomeHomeAssistantTokenEnvVar() : QStringLiteral("VAXIL_HOME_ASSISTANT_TOKEN"); }
QString BackendFacade::smartHomePresenceEntityId() const { return m_settings ? m_settings->smartHomePresenceEntityId() : QString(); }
QString BackendFacade::smartHomeLightEntityId() const { return m_settings ? m_settings->smartHomeLightEntityId() : QString(); }
QString BackendFacade::smartHomeIdentityMode() const { return m_settings ? m_settings->smartHomeIdentityMode() : QStringLiteral("none"); }
QString BackendFacade::smartHomeHomeAssistantIdentityEntityId() const { return m_settings ? m_settings->smartHomeHomeAssistantIdentityEntityId() : QString(); }
int BackendFacade::smartHomePollIntervalMs() const { return m_settings ? m_settings->smartHomePollIntervalMs() : 5000; }
int BackendFacade::smartHomeRequestTimeoutMs() const { return m_settings ? m_settings->smartHomeRequestTimeoutMs() : 5000; }
bool BackendFacade::smartHomeSensorOnlyWelcomeEnabled() const { return m_settings && m_settings->smartHomeSensorOnlyWelcomeEnabled(); }
int BackendFacade::smartHomeWelcomeCooldownMinutes() const { return m_settings ? m_settings->smartHomeWelcomeCooldownMinutes() : 30; }
int BackendFacade::smartHomeRoomAbsenceGraceMinutes() const { return m_settings ? m_settings->smartHomeRoomAbsenceGraceMinutes() : 6; }
int BackendFacade::smartHomeIdentityMissingTimeoutMinutes() const { return m_settings ? m_settings->smartHomeIdentityMissingTimeoutMinutes() : 10; }
QString BackendFacade::smartHomeBleBeaconUuid() const { return m_settings ? m_settings->smartHomeBleBeaconUuid() : QString(); }
int BackendFacade::smartHomeBleMissingTimeoutMinutes() const { return m_settings ? m_settings->smartHomeBleMissingTimeoutMinutes() : 10; }
int BackendFacade::smartHomeBleScanIntervalMs() const { return m_settings ? m_settings->smartHomeBleScanIntervalMs() : 1000; }
int BackendFacade::smartHomeBleRssiThreshold() const { return m_settings ? m_settings->smartHomeBleRssiThreshold() : -127; }
bool BackendFacade::smartHomeWelcomeEnabled() const { return m_settings == nullptr || m_settings->smartHomeWelcomeEnabled(); }
bool BackendFacade::smartHomeWelcomeCooldownEnabled() const { return m_settings == nullptr || m_settings->smartHomeWelcomeCooldownEnabled(); }
bool BackendFacade::smartHomePersonalWelcomeEnabled() const { return m_settings == nullptr || m_settings->smartHomePersonalWelcomeEnabled(); }
bool BackendFacade::smartHomeUnknownOccupantBlocksWelcomeEnabled() const { return m_settings == nullptr || m_settings->smartHomeUnknownOccupantBlocksWelcomeEnabled(); }
bool BackendFacade::smartHomeUnknownOccupantSpokenAlertsEnabled() const { return m_settings == nullptr || m_settings->smartHomeUnknownOccupantSpokenAlertsEnabled(); }
QString BackendFacade::smartHomePersonalWelcomeTemplate() const { return m_settings ? m_settings->smartHomePersonalWelcomeTemplate() : QStringLiteral("Welcome back, {user_name}."); }
QString BackendFacade::smartHomePersonalWelcomeWithAlertTemplate() const { return m_settings ? m_settings->smartHomePersonalWelcomeWithAlertTemplate() : QStringLiteral("Welcome back, {user_name}. Someone entered your room at {event_time}."); }
QString BackendFacade::smartHomeUnknownOccupantMessageTemplate() const { return m_settings ? m_settings->smartHomeUnknownOccupantMessageTemplate() : QStringLiteral("There appears to be someone in the room."); }
QString BackendFacade::smartHomeUnknownOccupantAlertResponseTemplate() const { return m_settings ? m_settings->smartHomeUnknownOccupantAlertResponseTemplate() : QStringLiteral("Someone was detected in your room at {event_time}."); }
void BackendFacade::toggleOverlay() { m_overlayController->toggleOverlay(); }
void BackendFacade::setUiMode(const QString &mode)
{
    m_settings->setUiMode(mode);
    m_settings->save();
}
void BackendFacade::refreshModels() { m_assistantController->refreshModels(); }
void BackendFacade::submitText(const QString &text) { m_assistantController->submitText(text); }
void BackendFacade::startListening() { m_assistantController->startListening(); }
void BackendFacade::interruptSpeechAndListen() { m_assistantController->interruptSpeechAndListen(); }
void BackendFacade::cancelRequest() { m_assistantController->cancelActiveRequest(); }
void BackendFacade::setSelectedModel(const QString &modelId) { m_assistantController->setSelectedModel(modelId); }
void BackendFacade::openToolsHub() { emit toolsWindowRequested(); }
void BackendFacade::openSettingsWindow() { emit settingsWindowRequested(); }
void BackendFacade::openSetupWizard() { emit setupWindowRequested(); }
void BackendFacade::setSelectedIntentModelId(const QString &modelId)
{
    const IntentModelPreset *preset = findIntentModelPreset(modelId);
    m_settings->setSelectedIntentModelId(preset != nullptr ? preset->id : modelId);
    m_settings->setIntentModelPath(resolveIntentModelPath(m_settings->selectedIntentModelId()));
    m_settings->save();
    emit settingsChanged();
}
void BackendFacade::setAgentEnabled(bool enabled) { m_assistantController->setAgentEnabled(enabled); }
void BackendFacade::setBackgroundPanelVisible(bool visible) { m_assistantController->setBackgroundPanelVisible(visible); }
void BackendFacade::notifyTaskToastShown(int taskId) { m_assistantController->noteTaskToastShown(taskId); }
void BackendFacade::notifyProactiveSuggestionFeedback(const QString &signalType,
                                                      const QString &suggestionType)
{
    m_assistantController->noteProactiveSuggestionFeedback(signalType, suggestionType);
}
void BackendFacade::notifyTaskPanelRendered() { m_assistantController->noteTaskPanelRendered(); }
void BackendFacade::saveAgentSettings(bool enabled,
                                      const QString &providerMode,
                                      double conversationTemperature,
                                      double conversationTopP,
                                      double toolUseTemperature,
                                      int providerTopK,
                                      int maxOutputTokens,
                                      bool memoryAutoWrite,
                                      const QString &webSearchProvider,
                                      const QString &braveSearchApiKey,
                                      bool tracePanelEnabled,
                                      bool budgetEnforcementDisabled)
{
    m_assistantController->saveAgentSettings(enabled,
                                             providerMode,
                                             conversationTemperature,
                                             conversationTopP,
                                             toolUseTemperature,
                                             providerTopK,
                                             maxOutputTokens,
                                             memoryAutoWrite,
                                             webSearchProvider,
                                             braveSearchApiKey,
                                             tracePanelEnabled,
                                             budgetEnforcementDisabled);
}
void BackendFacade::setWakeEngineKind(const QString &kind)
{
    Q_UNUSED(kind);
    m_settings->setWakeEngineKind(QStringLiteral("sherpa-onnx"));
    m_settings->save();
    emit settingsChanged();
}

void BackendFacade::activateFocusMode(int durationMinutes, bool allowCriticalAlerts)
{
    const int normalizedMinutes = qMax(0, durationMinutes);
    const qint64 untilEpochMs = normalizedMinutes > 0
        ? QDateTime::currentMSecsSinceEpoch() + (static_cast<qint64>(normalizedMinutes) * 60 * 1000)
        : 0;

    m_settings->setFocusModeEnabled(true);
    m_settings->setFocusModeAllowCriticalAlerts(allowCriticalAlerts);
    m_settings->setFocusModeDurationMinutes(normalizedMinutes);
    m_settings->setFocusModeUntilEpochMs(untilEpochMs);
    m_settings->save();

    if (m_loggingService) {
        FocusModeState state;
        state.enabled = true;
        state.allowCriticalAlerts = allowCriticalAlerts;
        state.durationMinutes = normalizedMinutes;
        state.untilEpochMs = untilEpochMs;
        state.source = QStringLiteral("backend_facade");
        m_loggingService->logFocusModeTransition(state,
                                                QStringLiteral("manual"),
                                                QStringLiteral("focus_mode.enabled"));
    }

    emit settingsChanged();
}

void BackendFacade::deactivateFocusMode()
{
    m_settings->setFocusModeEnabled(false);
    m_settings->setFocusModeDurationMinutes(0);
    m_settings->setFocusModeUntilEpochMs(0);
    m_settings->save();

    if (m_loggingService) {
        FocusModeState state;
        state.enabled = false;
        state.allowCriticalAlerts = m_settings->focusModeAllowCriticalAlerts();
        state.source = QStringLiteral("backend_facade");
        m_loggingService->logFocusModeTransition(state,
                                                QStringLiteral("manual"),
                                                QStringLiteral("focus_mode.disabled"));
    }

    emit settingsChanged();
}

void BackendFacade::setPrivateModeEnabled(bool enabled)
{
    m_settings->setPrivateModeEnabled(enabled);
    m_settings->save();

    if (m_loggingService) {
        BehaviorTraceEvent event = BehaviorTraceEvent::create(
            QStringLiteral("private_mode"),
            QStringLiteral("state_transition"),
            enabled ? QStringLiteral("private_mode.enabled") : QStringLiteral("private_mode.disabled"),
            {
                { QStringLiteral("enabled"), enabled }
            },
            QStringLiteral("user"));
        event.capabilityId = QStringLiteral("private_mode");
        event.threadId = QStringLiteral("companion::private_mode");
        m_loggingService->logBehaviorEvent(event);
    }

    emit settingsChanged();
}

bool BackendFacade::savePermissionOverrides(const QVariantList &overrides)
{
    const QVariantList before = PermissionOverrideSettings::sanitize(m_settings->permissionOverrides());
    const QVariantList sanitized = PermissionOverrideSettings::sanitize(overrides);
    m_settings->setPermissionOverrides(sanitized);
    const bool saved = m_settings->save();
    if (saved) {
        setToolInstallStatus(QStringLiteral("Permission overrides saved."));
        if (m_loggingService && before != sanitized) {
            BehaviorTraceEvent event = BehaviorTraceEvent::create(
                QStringLiteral("permission"),
                QStringLiteral("overrides_saved"),
                overrideReasonCode(before, sanitized),
                {
                    {QStringLiteral("previousCount"), before.size()},
                    {QStringLiteral("currentCount"), sanitized.size()},
                    {QStringLiteral("previousOverrides"), compactOverrideRows(before)},
                    {QStringLiteral("currentOverrides"), compactOverrideRows(sanitized)}
                },
                QStringLiteral("user"));
            event.traceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            event.capabilityId = QStringLiteral("permission_override_settings");
            m_loggingService->logBehaviorEvent(event);
        }
    } else {
        setToolInstallStatus(QStringLiteral("Could not save permission overrides."));
    }
    emit settingsChanged();
    return saved;
}

void BackendFacade::setTtsEngineKind(const QString &kind)
{
    m_settings->setTtsEngineKind(kind);
    m_settings->save();
    emit settingsChanged();
}
void BackendFacade::saveAudioProcessing(bool aecEnabled, bool rnnoiseEnabled, double vadSensitivity)
{
    m_settings->setAecEnabled(aecEnabled);
    m_settings->setRnnoiseEnabled(rnnoiseEnabled);
    m_settings->setVadSensitivity(vadSensitivity);
    m_settings->save();
    emit settingsChanged();
}

void BackendFacade::saveVisionSettings(bool enabled,
                                       const QString &endpoint,
                                       int timeoutMs,
                                       int staleThresholdMs,
                                       bool contextAlwaysOn,
                                       double objectsMinConfidence,
                                       double gesturesMinConfidence)
{
    m_settings->setVisionEnabled(enabled);
    m_settings->setVisionEndpoint(endpoint);
    m_settings->setVisionTimeoutMs(timeoutMs);
    m_settings->setVisionStaleThresholdMs(staleThresholdMs);
    m_settings->setVisionContextAlwaysOn(contextAlwaysOn);
    m_settings->setVisionObjectsMinConfidence(objectsMinConfidence);
    m_settings->setVisionGesturesMinConfidence(gesturesMinConfidence);
    m_settings->save();
    emit settingsChanged();
}

void BackendFacade::setSelectedVoicePresetId(const QString &voiceId)
{
    const PiperVoicePreset *preset = findVoicePreset(voiceId);
    if (preset == nullptr) {
        return;
    }

    m_settings->setSelectedVoicePresetId(voiceId);

    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString modelPath = piperVoicesRoot(appDataRoot) + QStringLiteral("/") + preset->id + QStringLiteral(".onnx");
    if (QFileInfo::exists(modelPath)) {
        m_settings->setPiperVoiceModel(modelPath);
    }

    m_settings->save();
    emit settingsChanged();
}

void BackendFacade::setTtsVoiceProfileId(const QString &profileId)
{
    const QString normalizedProfile = TtsVoiceProfiles::normalizeProfileId(profileId);
    if (normalizedProfile != QStringLiteral("custom")) {
        if (!TtsVoiceProfiles::applyProfile(normalizedProfile, m_settings)) {
            m_settings->setTtsVoiceProfileId(QStringLiteral("balanced"));
        }
    } else {
        m_settings->setTtsVoiceProfileId(QStringLiteral("custom"));
    }

    m_settings->save();
    emit settingsChanged();
}

void BackendFacade::refreshAudioDevices()
{
    emit audioDevicesChanged();
    emit settingsChanged();
}

void BackendFacade::saveProviderSettings(const QString &providerKind,
                                         const QString &apiKey,
                                         const QString &endpoint)
{
    saveSettings(endpoint,
                 providerKind,
                 apiKey,
                 m_settings->selectedModel(),
                 static_cast<int>(m_settings->defaultReasoningMode()),
                 m_settings->autoRoutingEnabled(),
                 m_settings->streamingEnabled(),
                 m_settings->requestTimeoutMs(),
                 m_settings->aecEnabled(),
                 m_settings->rnnoiseEnabled(),
                 m_settings->vadSensitivity(),
                 m_settings->wakeEngineKind(),
                 m_settings->whisperExecutable(),
                 m_settings->whisperModelPath(),
                 m_settings->wakeTriggerThreshold(),
                 m_settings->wakeTriggerCooldownMs(),
                 m_settings->ttsEngineKind(),
                 m_settings->piperExecutable(),
                 m_settings->piperVoiceModel(),
                 m_settings->qwenTtsExecutable(),
                 m_settings->qwenTtsModelDir(),
                 m_settings->qwenTtsLanguage(),
                 m_settings->qwenTtsThreads(),
                 m_settings->ffmpegExecutable(),
                 m_settings->voiceSpeed(),
                 m_settings->voicePitch(),
                 m_settings->piperNoiseScale(),
                 m_settings->piperNoiseW(),
                 m_settings->piperSentenceSilence(),
                 m_settings->ttsPostProcessMode(),
                 m_settings->ttsVoiceProfileId(),
                 m_settings->micSensitivity(),
                 m_settings->selectedAudioInputDeviceId(),
                 m_settings->selectedAudioOutputDeviceId(),
                 m_settings->clickThroughEnabled());
}

void BackendFacade::saveSettings(
    const QString &endpoint,
    const QString &providerKind,
    const QString &apiKey,
    const QString &modelId,
    int defaultMode,
    bool autoRouting,
    bool streaming,
    int timeoutMs,
    bool aecEnabled,
    bool rnnoiseEnabled,
    double vadSensitivity,
    const QString &wakeEngineKind,
    const QString &whisperPath,
    const QString &whisperModelPath,
    double wakeThreshold,
    int wakeCooldownMs,
    const QString &ttsEngineKind,
    const QString &piperPath,
    const QString &voicePath,
    const QString &qwenTtsExecutable,
    const QString &qwenTtsModelDir,
    const QString &qwenTtsLanguage,
    int qwenTtsThreads,
    const QString &ffmpegPath,
    double voiceSpeed,
    double voicePitch,
    double piperNoiseScale,
    double piperNoiseW,
    double piperSentenceSilence,
    const QString &ttsPostProcessMode,
    const QString &ttsVoiceProfileId,
    double micSensitivity,
    const QString &audioInputDeviceId,
    const QString &audioOutputDeviceId,
    bool clickThrough)
{
    Q_UNUSED(wakeEngineKind);

    const QString normalizedProviderRaw = providerKind.trimmed().toLower();
    const QString normalizedProviderKind = (normalizedProviderRaw.isEmpty() || normalizedProviderRaw == QStringLiteral("lmstudio"))
        ? QStringLiteral("openai_compatible_local")
        : normalizedProviderRaw;
    QString normalizedEndpoint = endpoint.trimmed();
    const QString openRouterEndpoint = QStringLiteral("https://openrouter.ai/api");
    if (normalizedProviderKind == QStringLiteral("openrouter")) {
        if (normalizedEndpoint.isEmpty()) {
            normalizedEndpoint = openRouterEndpoint;
        }
    } else {
        if (normalizedEndpoint.isEmpty() || normalizedEndpoint.compare(openRouterEndpoint, Qt::CaseInsensitive) == 0) {
            QString preferredLocalEndpoint = m_settings->lmStudioEndpoint().trimmed();
            if (preferredLocalEndpoint.isEmpty()
                || preferredLocalEndpoint.compare(openRouterEndpoint, Qt::CaseInsensitive) == 0) {
                preferredLocalEndpoint = QStringLiteral("http://localhost:1234");
            }
            normalizedEndpoint = preferredLocalEndpoint;
        }
    }

    const QString detectedVoicePresetId = detectVoicePresetIdFromPath(voicePath);
    if (!detectedVoicePresetId.isEmpty()) {
        m_settings->setSelectedVoicePresetId(detectedVoicePresetId);
    }
    m_settings->setQwenTtsExecutable(qwenTtsExecutable);
    m_settings->setQwenTtsModelDir(qwenTtsModelDir);
    m_settings->setQwenTtsLanguage(qwenTtsLanguage);
    m_settings->setQwenTtsThreads(qwenTtsThreads);
    m_settings->setVoiceSpeed(voiceSpeed);
    m_settings->setVoicePitch(voicePitch);
    m_settings->setPiperNoiseScale(piperNoiseScale);
    m_settings->setPiperNoiseW(piperNoiseW);
    m_settings->setPiperSentenceSilence(piperSentenceSilence);
    m_settings->setTtsPostProcessMode(ttsPostProcessMode);
    const QString explicitProfile = TtsVoiceProfiles::normalizeProfileId(ttsVoiceProfileId);
    const QString inferredProfile = TtsVoiceProfiles::detectClosestProfileId(
        m_settings->voiceSpeed(),
        m_settings->voicePitch(),
        m_settings->piperNoiseScale(),
        m_settings->piperNoiseW(),
        m_settings->piperSentenceSilence(),
        m_settings->ttsPostProcessMode());
    m_settings->setTtsVoiceProfileId(explicitProfile == QStringLiteral("custom")
        ? QStringLiteral("custom")
        : inferredProfile);

    m_assistantController->saveSettings(
        normalizedProviderKind,
        apiKey,
        normalizedEndpoint, modelId, defaultMode, autoRouting, streaming, timeoutMs,
        aecEnabled,
        rnnoiseEnabled,
        vadSensitivity,
        QStringLiteral("sherpa-onnx"),
        whisperPath,
        whisperModelPath,
        wakeThreshold,
        wakeCooldownMs,
        ttsEngineKind,
        piperPath,
        voicePath,
        ffmpegPath,
        m_settings->voiceSpeed(),
        m_settings->voicePitch(),
        micSensitivity,
        audioInputDeviceId,
        audioOutputDeviceId,
        clickThrough);
    m_overlayController->setClickThrough(clickThrough);
    emit settingsChanged();
}

bool BackendFacade::downloadVoiceModel(const QString &voiceId)
{
    if (!PlatformRuntime::currentCapabilities().supportsAutoToolInstall) {
        setToolInstallStatus(autoInstallUnavailableMessage());
        return false;
    }

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

    clearSurfaceBackendError();
    setSurfaceToolActivity(QStringLiteral("Downloading model..."), compactBackendSurfaceText(preset->label, 52));
    setToolInstallStatus(QStringLiteral("Downloading Piper voice %1...").arg(preset->id));

    QString errorText;
    if (!QFileInfo::exists(modelPath) && !downloadFileWithPowerShell(preset->modelUrl, modelPath, 5 * 60 * 1000, &errorText)) {
        clearSurfaceToolActivity();
        setSurfaceBackendError(QStringLiteral("Voice download failed"), compactBackendSurfaceText(errorText, 60));
        setToolInstallStatus(QStringLiteral("Voice download failed: %1").arg(errorText));
        return false;
    }

    if (!QFileInfo::exists(configPath) && !downloadFileWithPowerShell(preset->configUrl, configPath, 60 * 1000, &errorText)) {
        clearSurfaceToolActivity();
        setSurfaceBackendError(QStringLiteral("Voice config download failed"), compactBackendSurfaceText(errorText, 60));
        setToolInstallStatus(QStringLiteral("Voice config download failed: %1").arg(errorText));
        return false;
    }

    m_settings->setSelectedVoicePresetId(preset->id);
    m_settings->setPiperVoiceModel(modelPath);
    m_settings->save();
    clearSurfaceToolActivity();
    setToolInstallStatus(QStringLiteral("Voice ready: %1").arg(preset->label));
    emit settingsChanged();
    return true;
}

bool BackendFacade::downloadQwenTtsModel()
{
    if (!PlatformRuntime::currentCapabilities().supportsAutoToolInstall) {
        setToolInstallStatus(autoInstallUnavailableMessage());
        return false;
    }

    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString modelRoot = qwenModelRoot(appDataRoot);
    QDir().mkpath(modelRoot);

    const QString modelPath = modelRoot + QStringLiteral("/qwen3-tts-0.6b-f16.gguf");
    const QString tokenizerPath = modelRoot + QStringLiteral("/qwen3-tts-tokenizer-f16.gguf");
    const QString modelUrl = QStringLiteral("https://huggingface.co/endo5501/qwen3-tts.cpp/resolve/main/qwen3-tts-0.6b-f16.gguf?download=true");
    const QString tokenizerUrl = QStringLiteral("https://huggingface.co/endo5501/qwen3-tts.cpp/resolve/main/qwen3-tts-tokenizer-f16.gguf?download=true");

    clearSurfaceBackendError();
    setSurfaceToolActivity(QStringLiteral("Downloading model..."), QStringLiteral("Qwen3-TTS 0.6B"));
    setToolInstallStatus(QStringLiteral("Downloading Qwen3-TTS 0.6B model assets..."));

    QString errorText;
    if (!QFileInfo::exists(modelPath) && !downloadFileWithPowerShell(modelUrl, modelPath, 30 * 60 * 1000, &errorText)) {
        clearSurfaceToolActivity();
        setSurfaceBackendError(QStringLiteral("Qwen model download failed"), compactBackendSurfaceText(errorText, 60));
        setToolInstallStatus(QStringLiteral("Qwen model download failed: %1").arg(errorText));
        return false;
    }
    if (!QFileInfo::exists(tokenizerPath) && !downloadFileWithPowerShell(tokenizerUrl, tokenizerPath, 10 * 60 * 1000, &errorText)) {
        clearSurfaceToolActivity();
        setSurfaceBackendError(QStringLiteral("Qwen tokenizer download failed"), compactBackendSurfaceText(errorText, 60));
        setToolInstallStatus(QStringLiteral("Qwen tokenizer download failed: %1").arg(errorText));
        return false;
    }

    m_settings->setQwenTtsModelDir(modelRoot);
    m_settings->save();
    clearSurfaceToolActivity();
    setToolInstallStatus(QStringLiteral("Qwen3-TTS 0.6B model assets are ready."));
    emit settingsChanged();
    return true;
}

bool BackendFacade::downloadWhisperModel(const QString &modelId)
{
    if (!PlatformRuntime::currentCapabilities().supportsAutoToolInstall) {
        setToolInstallStatus(autoInstallUnavailableMessage());
        return false;
    }

    const WhisperModelPreset *preset = findWhisperModelPreset(modelId);
    if (preset == nullptr) {
        setToolInstallStatus(QStringLiteral("Selected Whisper model is not recognized."));
        return false;
    }

    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString modelsRoot = whisperModelsRoot(appDataRoot);
    QDir().mkpath(modelsRoot);

    const QString modelPath = modelsRoot + QStringLiteral("/") + preset->id + QStringLiteral(".bin");
    clearSurfaceBackendError();
    setSurfaceToolActivity(QStringLiteral("Downloading model..."), compactBackendSurfaceText(preset->label, 52));
    setToolInstallStatus(QStringLiteral("Downloading Whisper model %1...").arg(preset->id));

    QString errorText;
    if (!QFileInfo::exists(modelPath) && !downloadFileWithPowerShell(preset->modelUrl, modelPath, 10 * 60 * 1000, &errorText)) {
        clearSurfaceToolActivity();
        setSurfaceBackendError(QStringLiteral("Model download failed"), compactBackendSurfaceText(errorText, 60));
        setToolInstallStatus(QStringLiteral("Whisper model download failed: %1").arg(errorText));
        return false;
    }

    m_settings->setWhisperModelPath(modelPath);
    m_settings->save();
    clearSurfaceToolActivity();
    setToolInstallStatus(QStringLiteral("Whisper model ready: %1").arg(preset->label));
    emit settingsChanged();
    return true;
}

bool BackendFacade::completeInitialSetup(
    const QString &userName,
    const QString &providerKind,
    const QString &apiKey,
    const QString &endpoint,
    const QString &modelId,
    const QString &whisperPath,
    const QString &whisperModelPath,
    double wakeThreshold,
    int wakeCooldownMs,
    const QString &piperPath,
    const QString &voicePath,
    const QString &ffmpegPath,
    const QString &audioInputDeviceId,
    const QString &audioOutputDeviceId,
    bool clickThrough)
{
    const QString normalizedProviderRaw = providerKind.trimmed().toLower();
    const QString normalizedProviderKind = (normalizedProviderRaw.isEmpty() || normalizedProviderRaw == QStringLiteral("lmstudio"))
        ? QStringLiteral("openai_compatible_local")
        : normalizedProviderRaw;
    QString normalizedEndpoint = endpoint.trimmed();
    const QString openRouterEndpoint = QStringLiteral("https://openrouter.ai/api");
    if (normalizedProviderKind == QStringLiteral("openrouter")) {
        if (normalizedEndpoint.isEmpty()) {
            normalizedEndpoint = openRouterEndpoint;
        }
    } else {
        if (normalizedEndpoint.isEmpty() || normalizedEndpoint.compare(openRouterEndpoint, Qt::CaseInsensitive) == 0) {
            QString preferredLocalEndpoint = m_settings->lmStudioEndpoint().trimmed();
            if (preferredLocalEndpoint.isEmpty()
                || preferredLocalEndpoint.compare(openRouterEndpoint, Qt::CaseInsensitive) == 0) {
                preferredLocalEndpoint = QStringLiteral("http://localhost:1234");
            }
            normalizedEndpoint = preferredLocalEndpoint;
        }
    }
    if (normalizedEndpoint.isEmpty()) {
        setToolInstallStatus(QStringLiteral("Provider endpoint is required."));
        return false;
    }

    if (modelId.trimmed().isEmpty()) {
        setToolInstallStatus(QStringLiteral("Model name is required."));
        return false;
    }

    const QString resolvedWhisper = resolveExecutableSelection(
        whisperPath,
        PlatformRuntime::whisperExecutableNames());
    if (resolvedWhisper.isEmpty()) {
        setToolInstallStatus(whisperExecutableValidationMessage());
        return false;
    }

    const QString resolvedWhisperModel = resolveWhisperModelSelection(whisperModelPath);
    if (resolvedWhisperModel.isEmpty()) {
        setToolInstallStatus(QStringLiteral("Whisper model is invalid. Select a valid ggml-*.bin model file."));
        return false;
    }

    const QString resolvedPiper = resolveExecutableSelection(
        piperPath,
        PlatformRuntime::piperExecutableNames());
    if (resolvedPiper.isEmpty()) {
        setToolInstallStatus(piperExecutableValidationMessage());
        return false;
    }

    const QString resolvedFfmpeg = resolveExecutableSelection(
        ffmpegPath,
        PlatformRuntime::ffmpegExecutableNames());
    if (resolvedFfmpeg.isEmpty()) {
        setToolInstallStatus(ffmpegExecutableValidationMessage());
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

    m_identityProfileService->setUserName(userName.trimmed());

    const QString detectedVoicePresetId = detectVoicePresetIdFromPath(resolvedVoiceModel);
    if (!detectedVoicePresetId.isEmpty()) {
        m_settings->setSelectedVoicePresetId(detectedVoicePresetId);
    }

    m_assistantController->saveSettings(
        normalizedProviderKind,
        apiKey,
        normalizedEndpoint,
        modelId,
        static_cast<int>(ReasoningMode::Balanced),
        true,
        true,
        12000,
        m_settings->aecEnabled(),
        m_settings->rnnoiseEnabled(),
        m_settings->vadSensitivity(),
        QStringLiteral("sherpa-onnx"),
        resolvedWhisper,
        resolvedWhisperModel,
        wakeThreshold,
        wakeCooldownMs,
        QStringLiteral("piper"),
        resolvedPiper,
        resolvedVoiceModel,
        resolvedFfmpeg,
        0.95,
        1.00,
        0.02,
        audioInputDeviceId,
        audioOutputDeviceId,
        clickThrough);
    m_settings->setPiperNoiseScale(0.67);
    m_settings->setPiperNoiseW(0.80);
    m_settings->setPiperSentenceSilence(0.06);
    m_settings->setTtsPostProcessMode(QStringLiteral("light"));
    m_settings->setTtsVoiceProfileId(QStringLiteral("balanced"));

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
    const QString &userName,
    const QString &providerKind,
    const QString &apiKey,
    const QString &endpoint,
    const QString &modelId,
    const QString &whisperPath,
    const QString &whisperModelPath,
    double wakeThreshold,
    int wakeCooldownMs,
    const QString &piperPath,
    const QString &voicePath,
    const QString &ffmpegPath,
    const QString &audioInputDeviceId,
    const QString &audioOutputDeviceId,
    bool clickThrough,
    const QString &scenarioId)
{
    const QString normalizedProviderRaw = providerKind.trimmed().toLower();
    const QString normalizedProviderKind = (normalizedProviderRaw.isEmpty() || normalizedProviderRaw == QStringLiteral("lmstudio"))
        ? QStringLiteral("openai_compatible_local")
        : normalizedProviderRaw;
    QString normalizedEndpoint = endpoint.trimmed();
    const QString openRouterEndpoint = QStringLiteral("https://openrouter.ai/api");
    if (normalizedProviderKind == QStringLiteral("openrouter")) {
        if (normalizedEndpoint.isEmpty()) {
            normalizedEndpoint = openRouterEndpoint;
        }
    } else {
        if (normalizedEndpoint.isEmpty() || normalizedEndpoint.compare(openRouterEndpoint, Qt::CaseInsensitive) == 0) {
            QString preferredLocalEndpoint = m_settings->lmStudioEndpoint().trimmed();
            if (preferredLocalEndpoint.isEmpty()
                || preferredLocalEndpoint.compare(openRouterEndpoint, Qt::CaseInsensitive) == 0) {
                preferredLocalEndpoint = QStringLiteral("http://localhost:1234");
            }
            normalizedEndpoint = preferredLocalEndpoint;
        }
    }
    if (normalizedEndpoint.isEmpty()) {
        setToolInstallStatus(QStringLiteral("Provider endpoint is required before running a setup scenario."));
        return false;
    }

    if (modelId.trimmed().isEmpty()) {
        setToolInstallStatus(QStringLiteral("Model name is required."));
        return false;
    }

    const QString resolvedWhisper = resolveExecutableSelection(
        whisperPath,
        PlatformRuntime::whisperExecutableNames());
    if (resolvedWhisper.isEmpty()) {
        setToolInstallStatus(whisperExecutableValidationMessage());
        return false;
    }

    const QString resolvedWhisperModel = resolveWhisperModelSelection(whisperModelPath);
    if (resolvedWhisperModel.isEmpty()) {
        setToolInstallStatus(QStringLiteral("Whisper model is invalid. Select a valid ggml-*.bin model file."));
        return false;
    }

    const QString resolvedPiper = resolveExecutableSelection(
        piperPath,
        PlatformRuntime::piperExecutableNames());
    if (resolvedPiper.isEmpty()) {
        setToolInstallStatus(piperExecutableValidationMessage());
        return false;
    }

    const QString resolvedFfmpeg = resolveExecutableSelection(
        ffmpegPath,
        PlatformRuntime::ffmpegExecutableNames());
    if (resolvedFfmpeg.isEmpty()) {
        setToolInstallStatus(ffmpegExecutableValidationMessage());
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

    m_identityProfileService->setUserName(userName.trimmed());

    const QString detectedVoicePresetId = detectVoicePresetIdFromPath(resolvedVoiceModel);
    if (!detectedVoicePresetId.isEmpty()) {
        m_settings->setSelectedVoicePresetId(detectedVoicePresetId);
    }

    m_assistantController->saveSettings(
        normalizedProviderKind,
        apiKey,
        normalizedEndpoint,
        modelId,
        static_cast<int>(ReasoningMode::Balanced),
        true,
        true,
        12000,
        m_settings->aecEnabled(),
        m_settings->rnnoiseEnabled(),
        m_settings->vadSensitivity(),
        QStringLiteral("sherpa-onnx"),
        resolvedWhisper,
        resolvedWhisperModel,
        wakeThreshold,
        wakeCooldownMs,
        QStringLiteral("piper"),
        resolvedPiper,
        resolvedVoiceModel,
        resolvedFfmpeg,
        0.95,
        1.00,
        0.02,
        audioInputDeviceId,
        audioOutputDeviceId,
        clickThrough);
    m_settings->setPiperNoiseScale(0.67);
    m_settings->setPiperNoiseW(0.80);
    m_settings->setPiperSentenceSilence(0.06);
    m_settings->setTtsPostProcessMode(QStringLiteral("light"));
    m_settings->setTtsVoiceProfileId(QStringLiteral("balanced"));

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
    const PlatformCapabilities capabilities = PlatformRuntime::currentCapabilities();

    const bool endpointOk = !endpoint.trimmed().isEmpty();
    const bool modelOk = !modelId.trimmed().isEmpty();

    const QString resolvedWhisper = resolveExecutableSelection(
        whisperPath,
        PlatformRuntime::whisperExecutableNames());
    const QString resolvedWhisperModel = resolveWhisperModelSelection(whisperModelPath);
    const QString resolvedPiper = resolveExecutableSelection(
        piperPath,
        PlatformRuntime::piperExecutableNames());
    const QString resolvedQwen = resolveExecutableSelection(
        m_settings->qwenTtsExecutable(),
        PlatformRuntime::qwenTtsExecutableNames());
    const QString resolvedFfmpeg = resolveExecutableSelection(
        ffmpegPath,
        PlatformRuntime::ffmpegExecutableNames());
    const QString resolvedVoice = resolveVoiceModelSelection(voicePath);

    const bool whisperOk = !resolvedWhisper.isEmpty();
    const bool whisperModelOk = !resolvedWhisperModel.isEmpty();
    const bool piperOk = !resolvedPiper.isEmpty();
    const bool qwenOk = !resolvedQwen.isEmpty();
    const bool qwenModelOk = hasQwenModelAssets(m_settings->qwenTtsModelDir());
    const bool ffmpegOk = !resolvedFfmpeg.isEmpty();
    const bool voiceOk = !resolvedVoice.isEmpty();
    const QString resolvedIntentModel = resolveIntentModelPath(m_settings->selectedIntentModelId());
    const bool intentModelOk = !resolvedIntentModel.isEmpty();

    const QString whisperVersion = whisperOk ? probeToolVersion(resolvedWhisper, {QStringLiteral("--version")}) : QString{};
    const QString piperVersion = piperOk ? probeToolVersion(resolvedPiper, {QStringLiteral("--version")}) : QString{};
    const QString ffmpegVersion = ffmpegOk ? probeToolVersion(resolvedFfmpeg, {QStringLiteral("-version")}) : QString{};

    const bool autoInstallSupported = capabilities.supportsAutoToolInstall;
    const QString whisperLatest = (autoInstallSupported && whisperOk) ? fetchLatestReleaseTag(QStringLiteral("ggerganov/whisper.cpp")) : QString{};
    const QString piperLatest = (autoInstallSupported && piperOk) ? fetchLatestReleaseTag(QStringLiteral("rhasspy/piper")) : QString{};
    const QString ffmpegLatest = (autoInstallSupported && ffmpegOk) ? fetchLatestReleaseTag(QStringLiteral("BtbN/FFmpeg-Builds")) : QString{};

    result.insert(QStringLiteral("endpointOk"), endpointOk);
    result.insert(QStringLiteral("modelOk"), modelOk);
    result.insert(QStringLiteral("platformName"), capabilities.platformLabel);
    result.insert(QStringLiteral("supportsAutoToolInstall"), capabilities.supportsAutoToolInstall);
    result.insert(QStringLiteral("whisperOk"), whisperOk);
    result.insert(QStringLiteral("whisperModelOk"), whisperModelOk);
    result.insert(QStringLiteral("piperOk"), piperOk);
    result.insert(QStringLiteral("qwenOk"), qwenOk);
    result.insert(QStringLiteral("qwenModelOk"), qwenModelOk);
    result.insert(QStringLiteral("voiceOk"), voiceOk);
    result.insert(QStringLiteral("ffmpegOk"), ffmpegOk);
    result.insert(QStringLiteral("intentModelOk"), intentModelOk);
    result.insert(QStringLiteral("intentModelPathResolved"), resolvedIntentModel);
    result.insert(QStringLiteral("recommendedIntentModelId"), ::recommendedIntentModelId());
    result.insert(QStringLiteral("intentHardwareSummary"), ::intentHardwareSummary());

    result.insert(QStringLiteral("whisperPathResolved"), resolvedWhisper);
    result.insert(QStringLiteral("whisperModelPathResolved"), resolvedWhisperModel);
    result.insert(QStringLiteral("piperPathResolved"), resolvedPiper);
    result.insert(QStringLiteral("qwenPathResolved"), resolvedQwen);
    result.insert(QStringLiteral("qwenModelPathResolved"), m_settings->qwenTtsModelDir());
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

    const bool allValid = endpointOk
        && modelOk
        && whisperOk
        && whisperModelOk
        && piperOk
        && voiceOk
        && ffmpegOk;
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

void BackendFacade::rescanTools()
{
    if (m_toolManager != nullptr) {
        m_toolManager->rescan();
    }
}

void BackendFacade::downloadTool(const QString &name)
{
    if (!PlatformRuntime::currentCapabilities().supportsAutoToolInstall) {
        setToolInstallStatus(autoInstallUnavailableMessage());
        return;
    }
    if (m_toolManager != nullptr) {
        clearSurfaceBackendError();
        setSurfaceToolActivity(QStringLiteral("Installing tool..."), compactBackendSurfaceText(name, 44));
        m_toolManager->downloadTool(name);
    }
}

void BackendFacade::downloadModel(const QString &name)
{
    if (!PlatformRuntime::currentCapabilities().supportsAutoToolInstall) {
        setToolInstallStatus(autoInstallUnavailableMessage());
        return;
    }
    if (m_toolManager != nullptr) {
        clearSurfaceBackendError();
        setSurfaceToolActivity(QStringLiteral("Downloading model..."), compactBackendSurfaceText(name, 44));
        m_toolManager->downloadModel(name);
    }
}

void BackendFacade::installAllTools()
{
    if (!PlatformRuntime::currentCapabilities().supportsAutoToolInstall) {
        setToolInstallStatus(autoInstallUnavailableMessage());
        return;
    }
    if (m_toolManager != nullptr) {
        clearSurfaceBackendError();
        setSurfaceToolActivity(QStringLiteral("Installing tools..."));
        m_toolManager->installAll();
    }
}

void BackendFacade::setSurfaceToolActivity(const QString &primary, const QString &secondary)
{
    const QString normalizedPrimary = compactBackendSurfaceText(primary);
    const QString normalizedSecondary = compactBackendSurfaceText(secondary, 56);
    if (normalizedPrimary.isEmpty()) {
        clearSurfaceToolActivity();
        return;
    }

    if (m_surfaceToolActivityActive
        && m_surfaceToolActivityPrimary == normalizedPrimary
        && m_surfaceToolActivitySecondary == normalizedSecondary) {
        return;
    }

    m_surfaceToolActivityActive = true;
    m_surfaceToolActivityPrimary = normalizedPrimary;
    m_surfaceToolActivitySecondary = normalizedSecondary;
    emit assistantSurfaceChanged();
}

void BackendFacade::clearSurfaceToolActivity()
{
    if (!m_surfaceToolActivityActive
        && m_surfaceToolActivityPrimary.isEmpty()
        && m_surfaceToolActivitySecondary.isEmpty()) {
        return;
    }

    m_surfaceToolActivityActive = false;
    m_surfaceToolActivityPrimary.clear();
    m_surfaceToolActivitySecondary.clear();
    emit assistantSurfaceChanged();
}

void BackendFacade::setSurfaceBackendError(const QString &primary, const QString &secondary)
{
    const QString normalizedPrimary = compactBackendSurfaceText(primary);
    const QString normalizedSecondary = compactBackendSurfaceText(secondary, 60);
    if (normalizedPrimary.isEmpty()) {
        clearSurfaceBackendError();
        return;
    }

    if (m_surfaceBackendErrorPrimary == normalizedPrimary
        && m_surfaceBackendErrorSecondary == normalizedSecondary) {
        return;
    }

    m_surfaceBackendErrorPrimary = normalizedPrimary;
    m_surfaceBackendErrorSecondary = normalizedSecondary;
    emit assistantSurfaceChanged();
}

void BackendFacade::clearSurfaceBackendError()
{
    if (m_surfaceBackendErrorPrimary.isEmpty() && m_surfaceBackendErrorSecondary.isEmpty()) {
        return;
    }

    m_surfaceBackendErrorPrimary.clear();
    m_surfaceBackendErrorSecondary.clear();
    emit assistantSurfaceChanged();
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
    const QString linuxUserToolsRoot = PlatformRuntime::isLinux()
        ? QDir::homePath() + QStringLiteral("/.local/share/vaxil/tools")
        : QString{};
    const QString linuxLegacyToolsRoot = PlatformRuntime::isLinux()
        ? QDir::homePath() + QStringLiteral("/.local/share/jarvis/tools")
        : QString{};
    const QString repoRoot = QDir::currentPath();
    const QStringList whisperNames = PlatformRuntime::whisperExecutableNames();
    const QStringList piperNames = PlatformRuntime::piperExecutableNames();
    const QStringList qwenNames = PlatformRuntime::qwenTtsExecutableNames();
    const QStringList ffmpegNames = PlatformRuntime::ffmpegExecutableNames();
    QDir().mkpath(appDataRoot + QStringLiteral("/tools"));

    QString whisper = resolveExecutable(
        whisperNames,
        {
            repoRoot + QStringLiteral("/tools/whisper/") + whisperNames.value(0),
            repoRoot + QStringLiteral("/tools/whisper/") + whisperNames.value(1),
            linuxUserToolsRoot + QStringLiteral("/whisper/") + whisperNames.value(0),
            linuxUserToolsRoot + QStringLiteral("/whisper/") + whisperNames.value(1),
            linuxUserToolsRoot + QStringLiteral("/whisper/bin/") + whisperNames.value(0),
            linuxUserToolsRoot + QStringLiteral("/whisper/bin/") + whisperNames.value(1),
            appDataRoot + QStringLiteral("/tools/whisper/") + whisperNames.value(0),
            appDataRoot + QStringLiteral("/tools/whisper/") + whisperNames.value(1),
            appDataRoot + QStringLiteral("/tools/whisper/Release/") + whisperNames.value(0),
            appDataRoot + QStringLiteral("/tools/whisper/Release/") + whisperNames.value(1),
            appDataRoot + QStringLiteral("/tools/whisper/bin/") + whisperNames.value(0),
            appDataRoot + QStringLiteral("/tools/whisper/bin/") + whisperNames.value(1),
            linuxLegacyToolsRoot + QStringLiteral("/whisper/") + whisperNames.value(0),
            linuxLegacyToolsRoot + QStringLiteral("/whisper/") + whisperNames.value(1),
            linuxLegacyToolsRoot + QStringLiteral("/whisper/bin/") + whisperNames.value(0),
            linuxLegacyToolsRoot + QStringLiteral("/whisper/bin/") + whisperNames.value(1)
        });
    for (const QString &name : whisperNames) {
        if (whisper.isEmpty()) {
            whisper = findFileRecursive(toolsRoot, name);
        }
        if (whisper.isEmpty()) {
            whisper = findFileRecursive(repoRoot + QStringLiteral("/tools"), name);
        }
        if (whisper.isEmpty() && !linuxUserToolsRoot.isEmpty()) {
            whisper = findFileRecursive(linuxUserToolsRoot, name);
        }
        if (whisper.isEmpty() && !linuxLegacyToolsRoot.isEmpty()) {
            whisper = findFileRecursive(linuxLegacyToolsRoot, name);
        }
    }

    QString whisperModel = m_settings->whisperModelPath();
    if (whisperModel.isEmpty() || !QFileInfo::exists(whisperModel)) {
        whisperModel = detectWhisperModel(appDataRoot);
    }
    if (whisperModel.isEmpty()) {
        whisperModel = detectWhisperModel(repoRoot);
    }

    QString piper = resolveExecutable(
        piperNames,
        {
            repoRoot + QStringLiteral("/tools/piper/") + piperNames.value(0),
            appDataRoot + QStringLiteral("/tools/piper/") + piperNames.value(0),
            appDataRoot + QStringLiteral("/tools/piper/bin/") + piperNames.value(0),
            appDataRoot + QStringLiteral("/tools/piper/piper/") + piperNames.value(0)
        });
    if (piper.isEmpty()) {
        piper = findFirstMatchingFileRecursive(toolsRoot, piperNames);
    }
    if (piper.isEmpty()) {
        piper = findFirstMatchingFileRecursive(repoRoot + QStringLiteral("/tools"), piperNames);
    }

    QString qwen = resolveExecutable(
        qwenNames,
        {
            repoRoot + QStringLiteral("/tools/qwen3-tts/") + qwenNames.value(0),
            repoRoot + QStringLiteral("/tools/qwen3-tts/bin/") + qwenNames.value(0),
            appDataRoot + QStringLiteral("/tools/qwen3-tts/") + qwenNames.value(0),
            appDataRoot + QStringLiteral("/tools/qwen3-tts/bin/") + qwenNames.value(0)
        });
    if (qwen.isEmpty()) {
        qwen = findFirstMatchingFileRecursive(toolsRoot, qwenNames);
    }
    if (qwen.isEmpty()) {
        qwen = findFirstMatchingFileRecursive(repoRoot + QStringLiteral("/tools"), qwenNames);
    }

    QString ffmpeg = resolveExecutable(
        ffmpegNames,
        {
            repoRoot + QStringLiteral("/tools/ffmpeg/") + ffmpegNames.value(0),
            appDataRoot + QStringLiteral("/tools/ffmpeg/") + ffmpegNames.value(0),
            appDataRoot + QStringLiteral("/tools/ffmpeg/bin/") + ffmpegNames.value(0),
            appDataRoot + QStringLiteral("/tools/ffmpeg_build/bin/") + ffmpegNames.value(0)
        });
    if (ffmpeg.isEmpty()) {
        ffmpeg = findFirstMatchingFileRecursive(toolsRoot, ffmpegNames);
    }
    if (ffmpeg.isEmpty()) {
        ffmpeg = findFirstMatchingFileRecursive(repoRoot + QStringLiteral("/tools"), ffmpegNames);
    }

    QString voiceModel = m_settings->piperVoiceModel();
    if (voiceModel.isEmpty() || !QFileInfo::exists(voiceModel)) {
        voiceModel = detectPiperVoiceModel(appDataRoot);
    }
    if (voiceModel.isEmpty()) {
        voiceModel = detectPiperVoiceModel(repoRoot);
    }

    QString qwenModelDir = m_settings->qwenTtsModelDir();
    if (!hasQwenModelAssets(qwenModelDir)) {
        const QString appDataQwenModelDir = qwenModelRoot(appDataRoot);
        if (hasQwenModelAssets(appDataQwenModelDir)) {
            qwenModelDir = appDataQwenModelDir;
        }
    }
    if (!hasQwenModelAssets(qwenModelDir)) {
        const QString repoQwenModelDir = repoRoot + QStringLiteral("/tools/qwen3-tts/models/0.6b-base");
        if (hasQwenModelAssets(repoQwenModelDir)) {
            qwenModelDir = repoQwenModelDir;
        }
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
    if (!qwen.isEmpty()) {
        m_settings->setQwenTtsExecutable(qwen);
    }
    if (!voiceModel.isEmpty()) {
        m_settings->setPiperVoiceModel(voiceModel);
        const QString detectedVoicePresetId = detectVoicePresetIdFromPath(voiceModel);
        if (!detectedVoicePresetId.isEmpty()) {
            m_settings->setSelectedVoicePresetId(detectedVoicePresetId);
        }
    }
    if (hasQwenModelAssets(qwenModelDir)) {
        m_settings->setQwenTtsModelDir(qwenModelDir);
    }

    const bool complete = !m_settings->whisperExecutable().isEmpty()
        && !m_settings->whisperModelPath().isEmpty()
        && !m_settings->piperExecutable().isEmpty()
        && !m_settings->ffmpegExecutable().isEmpty()
        && !m_settings->piperVoiceModel().isEmpty();

    setToolInstallStatus(complete
            ? QStringLiteral("Voice tools detected and fields populated.")
            : PlatformRuntime::currentCapabilities().supportsAutoToolInstall
                ? QStringLiteral("Some voice tools are still missing.")
                : QStringLiteral("Some voice tools are still missing. Point Vaxil to existing binaries and model files."));

    m_settings->save();
    emit settingsChanged();
    return complete;
}

bool BackendFacade::setUserName(const QString &userName)
{
    const bool updated = m_identityProfileService->setUserName(userName);
    if (updated) {
        emit profileChanged();
    }
    return updated;
}

bool BackendFacade::installSkill(const QString &url)
{
    QString error;
    const bool ok = m_assistantController->installSkill(url, &error);
    setToolInstallStatus(ok ? QStringLiteral("Skill installed.") : error);
    if (ok) {
        emit agentTraceChanged();
        emit toolStatusesChanged();
    }
    return ok;
}

bool BackendFacade::createSkill(const QString &id, const QString &name, const QString &description)
{
    QString error;
    const bool ok = m_assistantController->createSkill(id, name, description, &error);
    setToolInstallStatus(ok ? QStringLiteral("Skill created.") : error);
    if (ok) {
        emit agentTraceChanged();
        emit toolStatusesChanged();
    }
    return ok;
}

QVariantMap BackendFacade::validateBraveSearchConnection(const QString &apiKey)
{
    QString effectiveKey = apiKey.trimmed();
    if (effectiveKey.isEmpty()) {
        effectiveKey = m_settings->braveSearchApiKey().trimmed();
    }
    if (effectiveKey.isEmpty()) {
        effectiveKey = qEnvironmentVariable("BRAVE_SEARCH_API_KEY").trimmed();
    }

    if (effectiveKey.isEmpty()) {
        return {
            {QStringLiteral("ok"), false},
            {QStringLiteral("connected"), false},
            {QStringLiteral("message"), QStringLiteral("No Brave API key found. Add one in settings or BRAVE_SEARCH_API_KEY.")}
        };
    }

    const NetworkFetchResult fetch = httpGet(
        QUrl(QStringLiteral("https://api.search.brave.com/res/v1/web/search?q=connectivity+test&count=1")),
        {
            {QByteArray("Accept"), QByteArray("application/json")},
            {QByteArray("X-Subscription-Token"), effectiveKey.toUtf8()}
        },
        18000);

    if (!fetch.ok) {
        return {
            {QStringLiteral("ok"), false},
            {QStringLiteral("connected"), false},
            {QStringLiteral("message"), fetch.error.isEmpty() ? QStringLiteral("Brave Search API validation failed.") : fetch.error},
            {QStringLiteral("statusCode"), fetch.statusCode}
        };
    }

    const QJsonDocument parsed = QJsonDocument::fromJson(fetch.body);
    const int resultCount = parsed.isObject()
        ? parsed.object().value(QStringLiteral("web")).toObject().value(QStringLiteral("results")).toArray().size()
        : 0;
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("connected"), true},
        {QStringLiteral("message"), QStringLiteral("Connected to Brave Search API.")},
        {QStringLiteral("resultCount"), resultCount}
    };
}

bool BackendFacade::saveToolsStoreSettings(const QString &webSearchProviderValue,
                                           bool mcpEnabledValue,
                                           const QString &mcpCatalogUrlValue,
                                           const QString &mcpServerUrlValue)
{
    m_settings->setWebSearchProvider(webSearchProviderValue);
    m_settings->setMcpEnabled(mcpEnabledValue);
    m_settings->setMcpCatalogUrl(mcpCatalogUrlValue);
    m_settings->setMcpServerUrl(mcpServerUrlValue);
    const bool ok = m_settings->save();
    if (!ok) {
        setToolInstallStatus(QStringLiteral("Failed to save tools and store settings."));
        return false;
    }
    setToolInstallStatus(QStringLiteral("Tools and store settings saved."));
    emit settingsChanged();
    return true;
}

bool BackendFacade::installMcpQuickServer(const QString &presetId)
{
    const McpQuickServerPreset *preset = findMcpQuickServerPreset(presetId);
    if (preset == nullptr) {
        setToolInstallStatus(QStringLiteral("Unknown MCP quick install preset."));
        return false;
    }

    const QString mcpToolsRoot = mcpToolsRootPath();
    if (mcpToolsRoot.isEmpty()) {
        setToolInstallStatus(QStringLiteral("Failed to resolve app data directory for MCP packages."));
        return false;
    }

    QDir().mkpath(mcpToolsRoot);

    const QString npmExecutable = resolveExecutable(
        {
            QStringLiteral("npm"),
            QStringLiteral("npm.cmd")
        },
        {});
    if (npmExecutable.isEmpty()) {
        setToolInstallStatus(QStringLiteral("npm is not available. Install Node.js LTS, then retry MCP install."));
        emit settingsChanged();
        return false;
    }

    QProcess process;
    process.setWorkingDirectory(mcpToolsRoot);
    clearSurfaceBackendError();
    setSurfaceToolActivity(QStringLiteral("Installing MCP package..."), compactBackendSurfaceText(preset->packageName, 52));
    setToolInstallStatus(QStringLiteral("Installing %1...").arg(preset->name));
    emit settingsChanged();
    process.start(
        npmExecutable,
        {
            QStringLiteral("install"),
            QStringLiteral("--no-audit"),
            QStringLiteral("--no-fund"),
            QStringLiteral("--silent"),
            QStringLiteral("--save-exact"),
            QStringLiteral("%1@%2").arg(preset->packageName, preset->packageVersion)
        });

    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(2000);
        clearSurfaceToolActivity();
        setSurfaceBackendError(QStringLiteral("MCP install timed out"), compactBackendSurfaceText(preset->name, 52));
        setToolInstallStatus(QStringLiteral("MCP install timed out for %1.").arg(preset->name));
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (detail.isEmpty()) {
            detail = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        }
        if (detail.isEmpty()) {
            detail = QStringLiteral("npm install failed");
        }
        clearSurfaceToolActivity();
        setSurfaceBackendError(QStringLiteral("MCP install failed"), compactBackendSurfaceText(detail, 60));
        setToolInstallStatus(QStringLiteral("%1 install failed: %2").arg(preset->name, detail));
        return false;
    }

    m_settings->setMcpEnabled(true);
    if (m_settings->mcpCatalogUrl().trimmed().isEmpty()) {
        m_settings->setMcpCatalogUrl(preset->defaultCatalogUrl);
    }
    m_settings->setMcpServerUrl(preset->packageName);
    if (!m_settings->save()) {
        clearSurfaceToolActivity();
        setSurfaceBackendError(QStringLiteral("MCP settings save failed"), compactBackendSurfaceText(preset->name, 52));
        setToolInstallStatus(QStringLiteral("Installed %1 but failed to save MCP settings.").arg(preset->name));
        return false;
    }

    clearSurfaceToolActivity();
    setToolInstallStatus(QStringLiteral("Installed %1 (%2). MCP is enabled.").arg(preset->name, preset->packageName));
    emit settingsChanged();
    return true;
}

bool BackendFacade::installMcpPackage(const QString &packageSpec, const QString &serverIdHint)
{
    const QString normalizedSpec = packageSpec.trimmed();
    if (normalizedSpec.isEmpty()) {
        setToolInstallStatus(QStringLiteral("MCP package name is required."));
        return false;
    }

    const QString mcpToolsRoot = mcpToolsRootPath();
    if (mcpToolsRoot.isEmpty()) {
        setToolInstallStatus(QStringLiteral("Failed to resolve app data directory for MCP packages."));
        return false;
    }

    QDir().mkpath(mcpToolsRoot);

    const QString npmExecutable = resolveExecutable(
        {
            QStringLiteral("npm"),
            QStringLiteral("npm.cmd")
        },
        {});
    if (npmExecutable.isEmpty()) {
        setToolInstallStatus(QStringLiteral("npm is not available. Install Node.js LTS, then retry MCP install."));
        emit settingsChanged();
        return false;
    }

    QProcess process;
    process.setWorkingDirectory(mcpToolsRoot);
    clearSurfaceBackendError();
    setSurfaceToolActivity(QStringLiteral("Installing MCP package..."), compactBackendSurfaceText(normalizedSpec, 52));
    setToolInstallStatus(QStringLiteral("Installing MCP package %1...").arg(normalizedSpec));
    emit settingsChanged();
    process.start(
        npmExecutable,
        {
            QStringLiteral("install"),
            QStringLiteral("--no-audit"),
            QStringLiteral("--no-fund"),
            QStringLiteral("--silent"),
            QStringLiteral("--save-exact"),
            normalizedSpec
        });

    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(2000);
        clearSurfaceToolActivity();
        setSurfaceBackendError(QStringLiteral("MCP install timed out"), compactBackendSurfaceText(normalizedSpec, 52));
        setToolInstallStatus(QStringLiteral("MCP install timed out for %1.").arg(normalizedSpec));
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (detail.isEmpty()) {
            detail = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        }
        if (detail.isEmpty()) {
            detail = QStringLiteral("npm install failed");
        }
        clearSurfaceToolActivity();
        setSurfaceBackendError(QStringLiteral("MCP install failed"), compactBackendSurfaceText(detail, 60));
        setToolInstallStatus(QStringLiteral("Custom MCP install failed: %1").arg(detail));
        return false;
    }

    m_settings->setMcpEnabled(true);
    if (m_settings->mcpCatalogUrl().trimmed().isEmpty()) {
        m_settings->setMcpCatalogUrl(QStringLiteral("https://registry.modelcontextprotocol.io/"));
    }

    QString serverId = serverIdHint.trimmed();
    if (serverId.isEmpty()) {
        serverId = normalizedSpec;
    }
    m_settings->setMcpServerUrl(serverId);
    if (!m_settings->save()) {
        clearSurfaceToolActivity();
        setSurfaceBackendError(QStringLiteral("MCP settings save failed"), compactBackendSurfaceText(normalizedSpec, 52));
        setToolInstallStatus(QStringLiteral("Installed %1 but failed to save MCP settings.").arg(normalizedSpec));
        return false;
    }

    clearSurfaceToolActivity();
    setToolInstallStatus(QStringLiteral("Installed custom MCP package %1.").arg(normalizedSpec));
    emit settingsChanged();
    return true;
}

QVariantList BackendFacade::recentBehaviorEvents(int limit) const
{
    return m_loggingService ? m_loggingService->recentBehaviorEvents(limit) : QVariantList();
}

QString BackendFacade::behaviorLedgerDatabasePath() const
{
    return m_loggingService ? m_loggingService->behaviorLedgerDatabasePath() : QString();
}

QString BackendFacade::behaviorLedgerNdjsonPath() const
{
    return m_loggingService ? m_loggingService->behaviorLedgerNdjsonPath() : QString();
}
