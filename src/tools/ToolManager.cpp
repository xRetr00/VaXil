#include "tools/ToolManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDirIterator>
#include <QVariantMap>

#include "platform/PlatformRuntime.h"

namespace {
QString firstExisting(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}

QString versionFromText(const QString &text)
{
    const QRegularExpression pattern(QStringLiteral("(v?\\d+\\.\\d+(?:\\.\\d+)*)"));
    const QRegularExpressionMatch match = pattern.match(text);
    return match.hasMatch() ? match.captured(1) : QString{};
}

QString sha256ForFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }

    return QString::fromLatin1(hash.result().toHex());
}

QString findFirstRecursive(const QString &rootPath, const QStringList &patterns)
{
    if (rootPath.isEmpty() || !QFileInfo::exists(rootPath)) {
        return {};
    }

    QDirIterator it(rootPath, patterns, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        return QFileInfo(it.next()).absoluteFilePath();
    }

    return {};
}

QString appToolsRoot()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/tools");
    QDir().mkpath(root);
    return root;
}

QVariantMap toVariantMap(const ToolInfo &info)
{
    QVariantMap map;
    map.insert(QStringLiteral("name"), info.name);
    map.insert(QStringLiteral("category"), info.category);
    map.insert(QStringLiteral("installed"), info.installed);
    map.insert(QStringLiteral("version"), info.version);
    map.insert(QStringLiteral("path"), info.path);
    map.insert(QStringLiteral("downloadable"), info.downloadable);
    map.insert(QStringLiteral("critical"), info.critical);
    map.insert(QStringLiteral("autoInstallSupported"), info.autoInstallSupported);
    return map;
}

QString firstExecutableOnPath(const QStringList &names)
{
    QStringList candidates;
    for (const QString &name : names) {
        candidates.push_back(QStandardPaths::findExecutable(name));
    }
    return firstExisting(candidates);
}
}

ToolManager::ToolManager(QObject *parent)
    : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
}

QList<ToolInfo> ToolManager::scan()
{
    const QString root = toolsRoot();
    const QString appTools = appToolsRoot();
    const QString sourceRoot = QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/third_party");
    const QString whisperExecutableName = PlatformRuntime::helperExecutableName(QStringLiteral("whisper-cli"));
    const QString whisperFallbackExecutableName = PlatformRuntime::helperExecutableName(QStringLiteral("main"));
    const QString piperExecutableName = PlatformRuntime::helperExecutableName(QStringLiteral("piper"));
    const QString ffmpegExecutableName = PlatformRuntime::helperExecutableName(QStringLiteral("ffmpeg"));
    QStringList sherpaPatterns = PlatformRuntime::sharedLibraryPatterns(QStringLiteral("sherpa-onnx-c-api"));
    sherpaPatterns.push_front(PlatformRuntime::helperExecutableName(QStringLiteral("sherpa-onnx")));
    m_tools = {
        probeTool(QStringLiteral("onnxruntime"), QStringLiteral("runtime"), {
                      findFirstRecursive(root + QStringLiteral("/onnxruntime"), PlatformRuntime::sharedLibraryPatterns(QStringLiteral("onnxruntime"))),
                      findFirstRecursive(sourceRoot + QStringLiteral("/onnxruntime"), PlatformRuntime::sharedLibraryPatterns(QStringLiteral("onnxruntime")))
                  }, true, true),
        probeTool(QStringLiteral("sherpa-onnx"), QStringLiteral("wake"), {
                      findFirstRecursive(root + QStringLiteral("/sherpa-onnx"), sherpaPatterns),
                      findFirstRecursive(sourceRoot + QStringLiteral("/sherpa-onnx"), sherpaPatterns)
                  }, true, true),
        probeTool(QStringLiteral("sherpa-kws-model"), QStringLiteral("wake"), {
                      findFirstRecursive(root + QStringLiteral("/sherpa-kws-model"), {QStringLiteral("tokens.txt"), QStringLiteral("encoder-*.onnx")}),
                      findFirstRecursive(sourceRoot + QStringLiteral("/sherpa-kws-model"), {QStringLiteral("tokens.txt"), QStringLiteral("encoder-*.onnx")})
                  }, true, true),
        probeTool(QStringLiteral("sentencepiece"), QStringLiteral("wake"), {
                      findFirstRecursive(root + QStringLiteral("/sentencepiece"), {QStringLiteral("CMakeLists.txt")}),
                      sourceRoot + QStringLiteral("/sentencepiece/CMakeLists.txt")
                  }, false, true),
        probeTool(QStringLiteral("silero-vad-model"), QStringLiteral("vad"), {
                      root + QStringLiteral("/models/silero/silero_vad.onnx"),
                      sourceRoot + QStringLiteral("/models/silero_vad.onnx")
                  }, true, true),
        probeTool(QStringLiteral("intent-minilm-int8"), QStringLiteral("intent"), {
                      root + QStringLiteral("/models/intent/intent-minilm-int8/model.onnx"),
                      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/models/intent/intent-minilm-int8/model.onnx")
                  }, false, true),
        probeTool(QStringLiteral("intent-minilm-q4f16"), QStringLiteral("intent"), {
                      root + QStringLiteral("/models/intent/intent-minilm-q4f16/model.onnx"),
                      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/models/intent/intent-minilm-q4f16/model.onnx")
                  }, false, true),
        probeTool(QStringLiteral("intent-minilm-fp32"), QStringLiteral("intent"), {
                      root + QStringLiteral("/models/intent/intent-minilm-fp32/model.onnx"),
                      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/models/intent/intent-minilm-fp32/model.onnx")
                  }, false, true),
        probeTool(QStringLiteral("rnnoise"), QStringLiteral("audio"), {
                      findFirstRecursive(root + QStringLiteral("/rnnoise"), {QStringLiteral("rnnoise.h")}),
                      findFirstRecursive(sourceRoot + QStringLiteral("/rnnoise"), {QStringLiteral("rnnoise.h")})
                  }, false, true),
        probeTool(QStringLiteral("speexdsp"), QStringLiteral("audio"), {
                      findFirstRecursive(root + QStringLiteral("/speexdsp"), {QStringLiteral("speex_echo.h")}),
                      findFirstRecursive(sourceRoot + QStringLiteral("/speexdsp"), {QStringLiteral("speex_echo.h")})
                  }, false, true),
        probeTool(QStringLiteral("whisper.cpp"), QStringLiteral("stt"), {
                      firstExecutableOnPath(PlatformRuntime::whisperExecutableNames()),
                      appTools + QStringLiteral("/whisper/") + whisperExecutableName,
                      appTools + QStringLiteral("/whisper/") + whisperFallbackExecutableName,
                      appTools + QStringLiteral("/whisper/bin/") + whisperExecutableName,
                      appTools + QStringLiteral("/whisper/bin/") + whisperFallbackExecutableName,
                      appTools + QStringLiteral("/whisper/Release/") + whisperExecutableName,
                      appTools + QStringLiteral("/whisper/Release/") + whisperFallbackExecutableName,
                      findFirstRecursive(appTools + QStringLiteral("/whisper"), PlatformRuntime::whisperExecutableNames())
                  }, true, true),
        probeTool(QStringLiteral("ffmpeg"), QStringLiteral("audio"), {
                      firstExecutableOnPath(PlatformRuntime::ffmpegExecutableNames()),
                      appTools + QStringLiteral("/ffmpeg/") + ffmpegExecutableName,
                      appTools + QStringLiteral("/ffmpeg/bin/") + ffmpegExecutableName,
                      appTools + QStringLiteral("/ffmpeg_build/bin/") + ffmpegExecutableName,
                      findFirstRecursive(appTools + QStringLiteral("/ffmpeg"), PlatformRuntime::ffmpegExecutableNames())
                  }, true, false),
        probeTool(QStringLiteral("piper"), QStringLiteral("tts"), {
                      firstExecutableOnPath(PlatformRuntime::piperExecutableNames()),
                      appTools + QStringLiteral("/piper/") + piperExecutableName,
                      appTools + QStringLiteral("/piper/bin/") + piperExecutableName,
                      appTools + QStringLiteral("/piper/piper/") + piperExecutableName,
                      findFirstRecursive(appTools + QStringLiteral("/piper"), PlatformRuntime::piperExecutableNames())
                  }, false, true)
    };
    return m_tools;
}

QVariantList ToolManager::toolStatusList() const
{
    QVariantList values;
    for (const ToolInfo &tool : m_tools) {
        values.push_back(toVariantMap(tool));
    }
    return values;
}

QString ToolManager::toolsRoot() const
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/third_party");
    QDir().mkpath(root);
    return root;
}

QString ToolManager::activeDownloadName() const
{
    return m_activeDownloadName;
}

int ToolManager::activeDownloadPercent() const
{
    return m_activeDownloadPercent;
}

void ToolManager::rescan()
{
    scan();
    emit toolsUpdated();
}

void ToolManager::downloadTool(const QString &name)
{
    if (!PlatformRuntime::currentCapabilities().supportsAutoToolInstall) {
        emit downloadFinished(name, false, QStringLiteral("Automatic tool downloads are not supported on this platform."));
        return;
    }
    beginDownload(descriptorForName(name));
}

void ToolManager::downloadModel(const QString &name)
{
    if (!PlatformRuntime::currentCapabilities().supportsAutoToolInstall) {
        emit downloadFinished(name, false, QStringLiteral("Automatic model downloads are not supported on this platform."));
        return;
    }
    beginDownload(descriptorForName(name));
}

void ToolManager::installAll()
{
    if (!PlatformRuntime::currentCapabilities().supportsAutoToolInstall) {
        emit downloadFinished(QStringLiteral("all"), false, QStringLiteral("Automatic tool installation is not supported on this platform."));
        return;
    }
    for (const ToolInfo &tool : scan()) {
        if (!tool.installed && tool.downloadable && tool.category != QStringLiteral("intent")) {
            beginDownload(descriptorForName(tool.name));
        }
    }
}

ToolInfo ToolManager::probeTool(const QString &name, const QString &category, const QStringList &candidateFiles, bool critical, bool downloadable) const
{
    ToolInfo info;
    info.name = name;
    info.category = category;
    info.critical = critical;
    info.autoInstallSupported = PlatformRuntime::currentCapabilities().supportsAutoToolInstall;
    info.downloadable = downloadable && info.autoInstallSupported;
    info.path = resolveExistingPath(candidateFiles);
    info.installed = !info.path.isEmpty();
    if (info.installed && QFileInfo(info.path).isFile() && QFileInfo(info.path).isExecutable()) {
        info.version = probeVersion(info.path, {QStringLiteral("--version")});
    }
    return info;
}

QString ToolManager::resolveExistingPath(const QStringList &candidateFiles) const
{
    return firstExisting(candidateFiles);
}

QString ToolManager::probeVersion(const QString &path, const QStringList &args) const
{
    if (path.isEmpty()) {
        return {};
    }

    QProcess process;
    process.start(path, args);
    if (!process.waitForFinished(2500)) {
        process.kill();
        process.waitForFinished(2000);
        return {};
    }
    return versionFromText(QString::fromUtf8(process.readAllStandardOutput()) + QString::fromUtf8(process.readAllStandardError()));
}

ToolManager::DownloadDescriptor ToolManager::descriptorForName(const QString &name) const
{
    const bool isWindows = PlatformRuntime::isWindows();
    const bool isLinux = PlatformRuntime::isLinux();

    if (name == QStringLiteral("silero-vad-model")) {
        return {
            .name = name,
            .category = QStringLiteral("vad"),
            .url = QStringLiteral("https://raw.githubusercontent.com/snakers4/silero-vad/master/files/silero_vad.onnx"),
            .relativeTargetPath = QStringLiteral("models/silero/silero_vad.onnx")
        };
    }
    if (name == QStringLiteral("onnxruntime")) {
        if (isWindows) {
            return {
                .name = name,
                .category = QStringLiteral("runtime"),
                .url = QStringLiteral("https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-win-x64-1.23.2.zip"),
                .relativeTargetPath = QStringLiteral("archives/onnxruntime-win-x64-1.23.2.zip"),
                .extractArchive = true,
                .extractDestinationDir = QStringLiteral("onnxruntime")
            };
        }
        if (isLinux) {
            return {
                .name = name,
                .category = QStringLiteral("runtime"),
                .url = QStringLiteral("https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-linux-x64-1.23.2.tgz"),
                .relativeTargetPath = QStringLiteral("archives/onnxruntime-linux-x64-1.23.2.tgz"),
                .extractArchive = true,
                .extractDestinationDir = QStringLiteral("onnxruntime")
            };
        }
        return {
            .name = name
        };
    }
    if (name == QStringLiteral("intent-minilm-int8")) {
        return {
            .name = name,
            .category = QStringLiteral("intent"),
            .url = QStringLiteral("https://huggingface.co/kousik-2310/intent-classifier-minilm/resolve/main/onnx/model_int8.onnx?download=true"),
            .relativeTargetPath = QStringLiteral("models/intent/intent-minilm-int8/model.onnx")
        };
    }
    if (name == QStringLiteral("intent-minilm-q4f16")) {
        return {
            .name = name,
            .category = QStringLiteral("intent"),
            .url = QStringLiteral("https://huggingface.co/kousik-2310/intent-classifier-minilm/resolve/main/onnx/model_q4f16.onnx?download=true"),
            .relativeTargetPath = QStringLiteral("models/intent/intent-minilm-q4f16/model.onnx")
        };
    }
    if (name == QStringLiteral("intent-minilm-fp32")) {
        return {
            .name = name,
            .category = QStringLiteral("intent"),
            .url = QStringLiteral("https://huggingface.co/kousik-2310/intent-classifier-minilm/resolve/main/onnx/model.onnx?download=true"),
            .relativeTargetPath = QStringLiteral("models/intent/intent-minilm-fp32/model.onnx")
        };
    }
    if (name == QStringLiteral("rnnoise-source") || name == QStringLiteral("rnnoise")) {
        return {
            .name = name,
            .category = QStringLiteral("audio"),
            .url = QStringLiteral("https://github.com/xiph/rnnoise/archive/refs/heads/main.zip"),
            .relativeTargetPath = QStringLiteral("archives/rnnoise-main.zip"),
            .extractArchive = true,
            .extractDestinationDir = QStringLiteral("rnnoise")
        };
    }
    if (name == QStringLiteral("speexdsp")) {
        return {
            .name = name,
            .category = QStringLiteral("audio"),
            .url = QStringLiteral("https://github.com/xiph/speexdsp/archive/refs/heads/master.zip"),
            .relativeTargetPath = QStringLiteral("archives/speexdsp-master.zip"),
            .extractArchive = true,
            .extractDestinationDir = QStringLiteral("speexdsp")
        };
    }
    if (name == QStringLiteral("whisper.cpp")) {
        if (isLinux) {
            return {
                .name = name,
                .category = QStringLiteral("stt"),
                .url = QStringLiteral("https://github.com/ggml-org/whisper.cpp/archive/refs/tags/v1.8.4.tar.gz"),
                .relativeTargetPath = QStringLiteral("archives/whisper.cpp-v1.8.4.tar.gz"),
                .extractArchive = true,
                .extractDestinationDir = QStringLiteral("whisper-src")
            };
        }
        return {
            .name = name
        };
    }
    if (name == QStringLiteral("piper")) {
        if (isLinux) {
            return {
                .name = name,
                .category = QStringLiteral("tts"),
                .url = QStringLiteral("https://github.com/rhasspy/piper/releases/latest/download/piper_linux_x86_64.tar.gz"),
                .relativeTargetPath = QStringLiteral("archives/piper_linux_x86_64.tar.gz"),
                .extractArchive = true,
                .extractDestinationDir = QStringLiteral("piper")
            };
        }
        return {
            .name = name
        };
    }
    if (name == QStringLiteral("sherpa-onnx-source") || name == QStringLiteral("sherpa-onnx")) {
        if (isWindows) {
            return {
                .name = name,
                .category = QStringLiteral("wake"),
                .url = QStringLiteral("https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.12.33/sherpa-onnx-v1.12.33-win-x64-shared-MD-Release-no-tts.tar.bz2"),
                .relativeTargetPath = QStringLiteral("archives/sherpa-onnx-v1.12.33-win-x64-shared-MD-Release-no-tts.tar.bz2"),
                .extractArchive = true,
                .extractDestinationDir = QStringLiteral("sherpa-onnx")
            };
        }
        if (isLinux) {
            return {
                .name = name,
                .category = QStringLiteral("wake"),
                .url = QStringLiteral("https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.12.33/sherpa-onnx-v1.12.33-linux-x64-shared.tar.bz2"),
                .relativeTargetPath = QStringLiteral("archives/sherpa-onnx-v1.12.33-linux-x64-shared.tar.bz2"),
                .extractArchive = true,
                .extractDestinationDir = QStringLiteral("sherpa-onnx")
            };
        }
        return {
            .name = name
        };
    }
    if (name == QStringLiteral("sherpa-kws-model")) {
        return {
            .name = name,
            .category = QStringLiteral("wake"),
            .url = QStringLiteral("https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01.tar.bz2"),
            .relativeTargetPath = QStringLiteral("archives/sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01.tar.bz2"),
            .extractArchive = true,
            .extractDestinationDir = QStringLiteral("sherpa-kws-model")
        };
    }
    if (name == QStringLiteral("sentencepiece")) {
        return {
            .name = name,
            .category = QStringLiteral("wake"),
            .url = QStringLiteral("https://github.com/google/sentencepiece/archive/refs/tags/v0.2.1.zip"),
            .relativeTargetPath = QStringLiteral("archives/sentencepiece-v0.2.1.zip"),
            .extractArchive = true,
            .extractDestinationDir = QStringLiteral("sentencepiece")
        };
    }
    return {
        .name = name,
        .category = QStringLiteral("unknown")
    };
}

void ToolManager::beginDownload(const DownloadDescriptor &descriptor)
{
    if (descriptor.url.isEmpty() || descriptor.relativeTargetPath.isEmpty()) {
        emit downloadFinished(descriptor.name, false, QStringLiteral("No download manifest is defined for this item yet."));
        return;
    }

    const QString targetPath = toolsRoot() + QStringLiteral("/") + descriptor.relativeTargetPath;
    QDir().mkpath(QFileInfo(targetPath).absolutePath());

    QFile *file = new QFile(targetPath);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete file;
        emit downloadFinished(descriptor.name, false, QStringLiteral("Failed to open download target."));
        return;
    }

    QNetworkReply *reply = m_network->get(QNetworkRequest(QUrl(descriptor.url)));
    reply->setProperty("toolName", descriptor.name);
    reply->setProperty("targetPath", targetPath);
    reply->setProperty("extractArchive", descriptor.extractArchive);
    reply->setProperty("extractDestinationDir", descriptor.extractDestinationDir);
    reply->setProperty("sha256", descriptor.sha256);
    reply->setProperty("filePointer", QVariant::fromValue(static_cast<quintptr>(reinterpret_cast<quintptr>(file))));

    m_activeDownloadName = descriptor.name;
    m_activeDownloadPercent = 0;

    connect(reply, &QNetworkReply::readyRead, this, [reply]() {
        auto *file = reinterpret_cast<QFile *>(reply->property("filePointer").value<quintptr>());
        if (file != nullptr) {
            file->write(reply->readAll());
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64 total) {
        m_activeDownloadName = reply->property("toolName").toString();
        m_activeDownloadPercent = total > 0 ? static_cast<int>((received * 100) / total) : -1;
        emit downloadProgress(reply->property("toolName").toString(), received, total);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        finalizeDownload(reply);
    });
}

void ToolManager::finalizeDownload(QNetworkReply *reply)
{
    const QString toolName = reply->property("toolName").toString();
    const QString targetPath = reply->property("targetPath").toString();
    const bool extractArchive = reply->property("extractArchive").toBool();
    const QString extractDestinationDir = reply->property("extractDestinationDir").toString();
    const QString expectedSha256 = reply->property("sha256").toString();
    auto *file = reinterpret_cast<QFile *>(reply->property("filePointer").value<quintptr>());

    if (file != nullptr) {
        file->flush();
        file->close();
        delete file;
    }

    if (reply->error() != QNetworkReply::NoError) {
        m_activeDownloadName.clear();
        m_activeDownloadPercent = -1;
        emit downloadFinished(toolName, false, reply->errorString());
        reply->deleteLater();
        return;
    }

    if (!expectedSha256.isEmpty()) {
        const QString actualSha256 = sha256ForFile(targetPath);
        if (actualSha256.compare(expectedSha256, Qt::CaseInsensitive) != 0) {
            m_activeDownloadName.clear();
            m_activeDownloadPercent = -1;
            emit downloadFinished(toolName, false, QStringLiteral("Checksum verification failed."));
            reply->deleteLater();
            return;
        }
    }

    if (extractArchive) {
        const QString destinationDir = extractDestinationDir.isEmpty()
            ? QFileInfo(targetPath).absolutePath() + QStringLiteral("/") + QFileInfo(targetPath).completeBaseName()
            : toolsRoot() + QStringLiteral("/") + extractDestinationDir;
        QDir().mkpath(destinationDir);
        QProcess extractor;
        QString program;
        QStringList args;
        const bool isZip = targetPath.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive);
        if (isZip && PlatformRuntime::isWindows()) {
            program = QStringLiteral("powershell");
            args = {
                QStringLiteral("-NoProfile"),
                QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                QStringLiteral("-Command"),
                QStringLiteral("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                    .arg(targetPath, destinationDir)
            };
        } else if (isZip) {
            program = QStringLiteral("unzip");
            args = {
                QStringLiteral("-o"),
                targetPath,
                QStringLiteral("-d"),
                destinationDir
            };
        } else {
            program = QStringLiteral("tar");
            args = {
                QStringLiteral("-xf"),
                targetPath,
                QStringLiteral("-C"),
                destinationDir
            };
        }
        extractor.start(program, args);
        if (!extractor.waitForFinished(60000) || extractor.exitCode() != 0) {
            if (extractor.state() != QProcess::NotRunning) {
                extractor.kill();
                extractor.waitForFinished(2000);
            }
            m_activeDownloadName.clear();
            m_activeDownloadPercent = -1;
            const QString stderrText = QString::fromUtf8(extractor.readAllStandardError()).trimmed();
            emit downloadFinished(toolName, false,
                                  stderrText.isEmpty()
                                      ? QStringLiteral("Downloaded archive but extraction failed.")
                                      : QStringLiteral("Downloaded archive but extraction failed: %1").arg(stderrText));
            reply->deleteLater();
            return;
        }

        if (toolName == QStringLiteral("whisper.cpp") && PlatformRuntime::isLinux()) {
            const QString sourceCmakePath = findFirstRecursive(destinationDir, {QStringLiteral("CMakeLists.txt")});
            if (sourceCmakePath.isEmpty()) {
                m_activeDownloadName.clear();
                m_activeDownloadPercent = -1;
                emit downloadFinished(toolName, false, QStringLiteral("Whisper source extracted, but CMakeLists.txt was not found."));
                reply->deleteLater();
                return;
            }

            const QString sourceDir = QFileInfo(sourceCmakePath).absolutePath();
            const QString buildDir = sourceDir + QStringLiteral("/build-jarvis");
            QDir().mkpath(buildDir);

            QProcess configure;
            configure.start(QStringLiteral("cmake"), {
                QStringLiteral("-S"), sourceDir,
                QStringLiteral("-B"), buildDir,
                QStringLiteral("-DCMAKE_BUILD_TYPE=Release"),
                QStringLiteral("-DWHISPER_BUILD_TESTS=OFF"),
                QStringLiteral("-DWHISPER_BUILD_EXAMPLES=ON")
            });
            if (!configure.waitForFinished(300000) || configure.exitCode() != 0) {
                const QString stderrText = QString::fromUtf8(configure.readAllStandardError()).trimmed();
                m_activeDownloadName.clear();
                m_activeDownloadPercent = -1;
                emit downloadFinished(
                    toolName,
                    false,
                    stderrText.isEmpty()
                        ? QStringLiteral("Whisper configure failed.")
                        : QStringLiteral("Whisper configure failed: %1").arg(stderrText));
                reply->deleteLater();
                return;
            }

            auto buildWhisperTarget = [&buildDir](const QString &target) {
                QProcess build;
                build.start(QStringLiteral("cmake"), {
                    QStringLiteral("--build"), buildDir,
                    QStringLiteral("--parallel"),
                    QStringLiteral("--target"), target
                });
                if (!build.waitForFinished(300000) || build.exitCode() != 0) {
                    const QString stderrText = QString::fromUtf8(build.readAllStandardError()).trimmed();
                    return stderrText.isEmpty() ? QStringLiteral("build failed") : stderrText;
                }
                return QString();
            };

            QString buildError = buildWhisperTarget(QStringLiteral("whisper-cli"));
            if (!buildError.isEmpty()) {
                buildError = buildWhisperTarget(QStringLiteral("main"));
            }
            if (!buildError.isEmpty()) {
                m_activeDownloadName.clear();
                m_activeDownloadPercent = -1;
                emit downloadFinished(toolName, false, QStringLiteral("Whisper build failed: %1").arg(buildError));
                reply->deleteLater();
                return;
            }

            const QString builtWhisperPath = findFirstRecursive(buildDir, PlatformRuntime::whisperExecutableNames());
            if (builtWhisperPath.isEmpty()) {
                m_activeDownloadName.clear();
                m_activeDownloadPercent = -1;
                emit downloadFinished(toolName, false, QStringLiteral("Whisper build completed, but executable was not found."));
                reply->deleteLater();
                return;
            }

            const QString installDir = appToolsRoot() + QStringLiteral("/whisper/bin");
            const QString installedWhisperPath = installDir + QStringLiteral("/") + PlatformRuntime::helperExecutableName(QStringLiteral("whisper-cli"));
            QDir().mkpath(installDir);
            QFile::remove(installedWhisperPath);
            if (!QFile::copy(builtWhisperPath, installedWhisperPath)) {
                m_activeDownloadName.clear();
                m_activeDownloadPercent = -1;
                emit downloadFinished(toolName, false, QStringLiteral("Whisper executable copy failed."));
                reply->deleteLater();
                return;
            }

            QFile installedFile(installedWhisperPath);
            installedFile.setPermissions(
                QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                QFileDevice::ReadOther | QFileDevice::ExeOther);
        }
    }

    m_activeDownloadName.clear();
    m_activeDownloadPercent = -1;
    rescan();
    emit downloadFinished(toolName, true, QStringLiteral("Download completed."));
    reply->deleteLater();
}
