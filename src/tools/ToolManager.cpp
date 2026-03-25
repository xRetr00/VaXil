#include "tools/ToolManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVariantMap>

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
    return map;
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
    const QString sourceRoot = QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/third_party");
    m_tools = {
        probeTool(QStringLiteral("onnxruntime"), QStringLiteral("runtime"), {
                      root + QStringLiteral("/onnxruntime/lib/onnxruntime.dll"),
                      root + QStringLiteral("/onnxruntime/bin/onnxruntime.dll"),
                      sourceRoot + QStringLiteral("/onnxruntime/onnxruntime-win-x64-1.24.4/lib/onnxruntime.dll")
                  }, true, true),
        probeTool(QStringLiteral("sherpa-onnx"), QStringLiteral("wake"), {
                      root + QStringLiteral("/sherpa-onnx/sherpa-onnx.exe"),
                      root + QStringLiteral("/sherpa-onnx/bin/sherpa-onnx.exe"),
                      sourceRoot + QStringLiteral("/sherpa-onnx/sherpa-onnx-v1.12.33-win-x64-shared-MD-Release-no-tts/bin/sherpa-onnx.exe")
                  }, false, true),
        probeTool(QStringLiteral("sentencepiece"), QStringLiteral("wake"), {
                      sourceRoot + QStringLiteral("/sentencepiece/CMakeLists.txt")
                  }, false, true),
        probeTool(QStringLiteral("silero-vad-model"), QStringLiteral("vad"), {
                      root + QStringLiteral("/models/silero/silero_vad.onnx"),
                      sourceRoot + QStringLiteral("/models/silero_vad.onnx")
                  }, true, true),
        probeTool(QStringLiteral("rnnoise"), QStringLiteral("audio"), {
                      root + QStringLiteral("/rnnoise/rnnoise.dll"),
                      root + QStringLiteral("/rnnoise/bin/rnnoise.dll"),
                      sourceRoot + QStringLiteral("/rnnoise/rnnoise-main")
                  }, false, true),
        probeTool(QStringLiteral("piper"), QStringLiteral("tts"), {
                      root + QStringLiteral("/piper/piper.exe")
                  }, true, false),
        probeTool(QStringLiteral("qwen3-tts"), QStringLiteral("tts"), {
                      root + QStringLiteral("/qwen3-tts/qwen3-tts.exe")
                  }, false, false)
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
    beginDownload(descriptorForName(name));
}

void ToolManager::downloadModel(const QString &name)
{
    beginDownload(descriptorForName(name));
}

void ToolManager::installAll()
{
    for (const ToolInfo &tool : scan()) {
        if (!tool.installed && tool.downloadable) {
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
    info.downloadable = downloadable;
    info.path = resolveExistingPath(candidateFiles);
    info.installed = !info.path.isEmpty();
    if (info.installed && info.path.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
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
        return {};
    }
    return versionFromText(QString::fromUtf8(process.readAllStandardOutput()) + QString::fromUtf8(process.readAllStandardError()));
}

ToolManager::DownloadDescriptor ToolManager::descriptorForName(const QString &name) const
{
    if (name == QStringLiteral("silero-vad-model")) {
        return {
            .name = name,
            .category = QStringLiteral("vad"),
            .url = QStringLiteral("https://raw.githubusercontent.com/snakers4/silero-vad/master/files/silero_vad.onnx"),
            .relativeTargetPath = QStringLiteral("models/silero/silero_vad.onnx")
        };
    }
    if (name == QStringLiteral("onnxruntime")) {
        return {
            .name = name,
            .category = QStringLiteral("runtime"),
            .url = QStringLiteral("https://github.com/microsoft/onnxruntime/archive/refs/heads/main.zip"),
            .relativeTargetPath = QStringLiteral("archives/onnxruntime-main.zip"),
            .extractArchive = true
        };
    }
    if (name == QStringLiteral("rnnoise-source") || name == QStringLiteral("rnnoise")) {
        return {
            .name = name,
            .category = QStringLiteral("audio"),
            .url = QStringLiteral("https://github.com/xiph/rnnoise/archive/refs/heads/main.zip"),
            .relativeTargetPath = QStringLiteral("archives/rnnoise-main.zip"),
            .extractArchive = true
        };
    }
    if (name == QStringLiteral("sherpa-onnx-source") || name == QStringLiteral("sherpa-onnx")) {
        return {
            .name = name,
            .category = QStringLiteral("wake"),
            .url = QStringLiteral("https://github.com/k2-fsa/sherpa-onnx/archive/refs/heads/master.zip"),
            .relativeTargetPath = QStringLiteral("archives/sherpa-onnx-master.zip"),
            .extractArchive = true
        };
    }
    if (name == QStringLiteral("sentencepiece")) {
        return {
            .name = name,
            .category = QStringLiteral("wake"),
            .url = QStringLiteral("https://github.com/google/sentencepiece/archive/refs/tags/v0.2.1.zip"),
            .relativeTargetPath = QStringLiteral("archives/sentencepiece-v0.2.1.zip"),
            .extractArchive = true
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
        const QString destinationDir = QFileInfo(targetPath).absolutePath() + QStringLiteral("/") + QFileInfo(targetPath).completeBaseName();
        QDir().mkpath(destinationDir);
        QProcess extractor;
        extractor.start(QStringLiteral("powershell"), {
            QStringLiteral("-NoProfile"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-Command"),
            QStringLiteral("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                .arg(targetPath, destinationDir)
        });
        if (!extractor.waitForFinished(60000) || extractor.exitCode() != 0) {
            m_activeDownloadName.clear();
            m_activeDownloadPercent = -1;
            emit downloadFinished(toolName, false, QStringLiteral("Downloaded archive but extraction failed."));
            reply->deleteLater();
            return;
        }
    }

    m_activeDownloadName.clear();
    m_activeDownloadPercent = -1;
    rescan();
    emit downloadFinished(toolName, true, QStringLiteral("Download completed."));
    reply->deleteLater();
}
