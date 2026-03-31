#include "skills/SkillStore.h"

#include "platform/PlatformRuntime.h"
#include "python/PythonRuntimeManager.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <nlohmann/json.hpp>

namespace {
QString quotePowerShell(QString value)
{
    value.replace(QStringLiteral("'"), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
}

QString normalizeSkillId(QString value)
{
    value = value.trimmed().toLower();
    for (QChar &ch : value) {
        if (!ch.isLetterOrNumber() && ch != QChar::fromLatin1('-') && ch != QChar::fromLatin1('_')) {
            ch = QChar::fromLatin1('-');
        }
    }
    while (value.contains(QStringLiteral("--"))) {
        value.replace(QStringLiteral("--"), QStringLiteral("-"));
    }
    return value.trimmed().remove(QRegularExpression(QStringLiteral("^-+|-+$")));
}

bool writeTextFile(const QString &path, const QString &content)
{
    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    QTextStream out(&file);
    out << content;
    return true;
}

QString resolveDownloadUrl(const QString &sourceUrl)
{
    const QString trimmed = sourceUrl.trimmed();
    const QRegularExpression githubRepoPattern(QStringLiteral("^https://github\\.com/([^/]+)/([^/]+)/?$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch repoMatch = githubRepoPattern.match(trimmed);
    if (repoMatch.hasMatch()) {
        return QStringLiteral("https://codeload.github.com/%1/%2/zip/refs/heads/main")
            .arg(repoMatch.captured(1), repoMatch.captured(2));
    }
    return trimmed;
}
}

SkillStore::SkillStore(QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(skillsRoot());
}

QString SkillStore::skillsRoot() const
{
    const QString workspaceRoot = QDir::currentPath() + QStringLiteral("/skills");
    QDir().mkpath(workspaceRoot);
    return workspaceRoot;
}

QList<SkillManifest> SkillStore::listSkills() const
{
    if (m_pythonRuntime == nullptr) {
        auto *self = const_cast<SkillStore *>(this);
        self->m_pythonRuntime = new PythonRuntimeManager({skillsRoot(), QDir::currentPath()}, nullptr, self);
    }

    QString runtimeError;
    const QJsonArray runtimeSkills = m_pythonRuntime->listSkills(&runtimeError);
    if (!runtimeSkills.isEmpty()) {
        QList<SkillManifest> skills;
        for (const QJsonValue &value : runtimeSkills) {
            const QJsonObject object = value.toObject();
            SkillManifest manifest;
            manifest.id = normalizeSkillId(object.value(QStringLiteral("id")).toString());
            manifest.name = object.value(QStringLiteral("name")).toString();
            manifest.version = object.value(QStringLiteral("version")).toString();
            manifest.description = object.value(QStringLiteral("description")).toString();
            manifest.promptTemplatePath = object.value(QStringLiteral("prompt_template")).toString();
            if (!manifest.id.isEmpty()) {
                skills.push_back(manifest);
            }
        }
        if (!skills.isEmpty()) {
            return skills;
        }
    }

    QList<SkillManifest> skills;
    QDirIterator it(skillsRoot(), {QStringLiteral("skill.json")}, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString manifestPath = it.next();
        const SkillManifest manifest = loadManifest(manifestPath);
        if (!manifest.id.isEmpty()) {
            skills.push_back(manifest);
        }
    }
    return skills;
}

bool SkillStore::createSkill(const QString &id, const QString &name, const QString &description, QString *error) const
{
    if (m_pythonRuntime == nullptr) {
        auto *self = const_cast<SkillStore *>(this);
        self->m_pythonRuntime = new PythonRuntimeManager({skillsRoot(), QDir::currentPath()}, nullptr, self);
    }

    QString runtimeError;
    const QJsonObject runtimeResult = m_pythonRuntime->createSkill(id, name, description, &runtimeError);
    if (!runtimeResult.isEmpty()) {
        const bool ok = runtimeResult.value(QStringLiteral("ok")).toBool();
        if (!ok && error) {
            *error = runtimeResult.value(QStringLiteral("detail")).toString(runtimeError);
        }
        if (ok) {
            return true;
        }
    }

    SkillManifest manifest{
        .id = normalizeSkillId(id),
        .name = name.trimmed(),
        .version = QStringLiteral("1.0.0"),
        .description = description.trimmed(),
        .promptTemplatePath = QStringLiteral("prompt.txt")
    };
    if (!validateManifest(manifest, error)) {
        return false;
    }

    const QString root = skillsRoot() + QStringLiteral("/") + manifest.id;
    if (QFileInfo::exists(root)) {
        if (error) {
            *error = QStringLiteral("Skill already exists.");
        }
        return false;
    }

    const nlohmann::json json = {
        {"id", manifest.id.toStdString()},
        {"name", manifest.name.toStdString()},
        {"version", manifest.version.toStdString()},
        {"description", manifest.description.toStdString()},
        {"prompt_template", manifest.promptTemplatePath.toStdString()}
    };

    return writeTextFile(root + QStringLiteral("/skill.json"), QString::fromStdString(json.dump(2)))
        && writeTextFile(root + QStringLiteral("/README.md"),
                         QStringLiteral("# %1\n\n%2\n").arg(manifest.name, manifest.description))
        && writeTextFile(root + QStringLiteral("/prompt.txt"),
                         QStringLiteral("Skill: %1\nPurpose: %2\nInstructions:\n- Replace this scaffold with concrete guidance.\n")
                             .arg(manifest.name, manifest.description));
}

bool SkillStore::installSkill(const QString &sourceUrl, QString *error) const
{
    if (!PlatformRuntime::isWindows()) {
        if (error) {
            *error = QStringLiteral("Skill download/install is currently only supported on Windows.");
        }
        return false;
    }

    if (m_pythonRuntime == nullptr) {
        auto *self = const_cast<SkillStore *>(this);
        self->m_pythonRuntime = new PythonRuntimeManager({skillsRoot(), QDir::currentPath()}, nullptr, self);
    }

    QString runtimeError;
    const QJsonObject runtimeResult = m_pythonRuntime->installSkill(sourceUrl, &runtimeError);
    if (!runtimeResult.isEmpty()) {
        const bool ok = runtimeResult.value(QStringLiteral("ok")).toBool();
        if (!ok && error) {
            *error = runtimeResult.value(QStringLiteral("detail")).toString(runtimeError);
        }
        if (ok) {
            return true;
        }
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (error) {
            *error = QStringLiteral("Failed to create a temp directory.");
        }
        return false;
    }

    const QString zipPath = tempDir.path() + QStringLiteral("/skill.zip");
    const QString extractPath = tempDir.path() + QStringLiteral("/extract");
    const QString downloadUrl = resolveDownloadUrl(sourceUrl);

    QProcess downloader;
    downloader.start(
        QStringLiteral("powershell"),
        {
            QStringLiteral("-NoProfile"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-Command"),
            QStringLiteral("$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -UseBasicParsing -Uri %1 -OutFile %2")
                .arg(quotePowerShell(downloadUrl), quotePowerShell(zipPath))
        });
    if (!downloader.waitForFinished(180000) || downloader.exitCode() != 0 || !QFileInfo::exists(zipPath)) {
        if (downloader.state() != QProcess::NotRunning) {
            downloader.kill();
            downloader.waitForFinished(2000);
        }
        if (error) {
            *error = QStringLiteral("Skill download failed.");
        }
        return false;
    }

    QProcess extractor;
    extractor.start(
        QStringLiteral("powershell"),
        {
            QStringLiteral("-NoProfile"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-Command"),
            QStringLiteral("Expand-Archive -Path %1 -DestinationPath %2 -Force")
                .arg(quotePowerShell(zipPath), quotePowerShell(extractPath))
        });
    if (!extractor.waitForFinished(120000) || extractor.exitCode() != 0) {
        if (extractor.state() != QProcess::NotRunning) {
            extractor.kill();
            extractor.waitForFinished(2000);
        }
        if (error) {
            *error = QStringLiteral("Skill archive extraction failed.");
        }
        return false;
    }

    QString manifestPath;
    QDirIterator it(extractPath, {QStringLiteral("skill.json")}, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        manifestPath = it.next();
    }
    if (manifestPath.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Downloaded skill is missing skill.json.");
        }
        return false;
    }

    const SkillManifest manifest = loadManifest(manifestPath);
    if (!validateManifest(manifest, error)) {
        return false;
    }

    const QString sourceRoot = QFileInfo(manifestPath).absolutePath();
    const QString destinationRoot = skillsRoot() + QStringLiteral("/") + manifest.id;
    if (QFileInfo::exists(destinationRoot)) {
        QDir(destinationRoot).removeRecursively();
    }
    return copyDirectory(sourceRoot, destinationRoot, error);
}

SkillManifest SkillStore::loadManifest(const QString &manifestPath) const
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const auto parsed = nlohmann::json::parse(file.readAll().constData(), nullptr, false);
    if (!parsed.is_object()) {
        return {};
    }

    return {
        .id = normalizeSkillId(QString::fromStdString(parsed.value("id", std::string{}))),
        .name = QString::fromStdString(parsed.value("name", std::string{})),
        .version = QString::fromStdString(parsed.value("version", std::string{})),
        .description = QString::fromStdString(parsed.value("description", std::string{})),
        .promptTemplatePath = QString::fromStdString(parsed.value("prompt_template", std::string{}))
    };
}

bool SkillStore::validateManifest(const SkillManifest &manifest, QString *error) const
{
    if (manifest.id.isEmpty() || manifest.name.trimmed().isEmpty() || manifest.promptTemplatePath.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Skill manifest requires id, name, and prompt_template.");
        }
        return false;
    }
    return true;
}

bool SkillStore::copyDirectory(const QString &sourcePath, const QString &destinationPath, QString *error) const
{
    QDir sourceDir(sourcePath);
    if (!sourceDir.exists()) {
        if (error) {
            *error = QStringLiteral("Skill source directory does not exist.");
        }
        return false;
    }

    QDir().mkpath(destinationPath);
    QDirIterator it(sourcePath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sourceItem = it.next();
        const QFileInfo sourceInfo(sourceItem);
        const QString relativePath = sourceDir.relativeFilePath(sourceItem);
        const QString destinationItem = destinationPath + QStringLiteral("/") + relativePath;

        if (sourceInfo.isDir()) {
            QDir().mkpath(destinationItem);
            continue;
        }

        if (relativePath.contains(QStringLiteral("..")) || sourceInfo.fileName().startsWith('.')) {
            continue;
        }

        QDir().mkpath(QFileInfo(destinationItem).absolutePath());
        QFile::remove(destinationItem);
        if (!QFile::copy(sourceItem, destinationItem)) {
            if (error) {
                *error = QStringLiteral("Failed to copy skill file %1.").arg(relativePath);
            }
            return false;
        }
    }

    return true;
}
