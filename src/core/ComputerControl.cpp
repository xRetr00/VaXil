#include "core/ComputerControl.h"

#include <algorithm>

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStringConverter>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

#include "platform/ComputerControlBackend.h"
#include "platform/PlatformRuntime.h"

namespace ComputerControl {
namespace {

ActionResult failure(const QString &summary, const QString &detail)
{
    return {
        .success = false,
        .summary = summary,
        .detail = detail
    };
}

ActionResult success(const QString &summary,
                     const QString &detail,
                     const QStringList &lines = {},
                     const QString &resolvedPath = {})
{
    return {
        .success = true,
        .summary = summary,
        .detail = detail,
        .lines = lines,
        .resolvedPath = resolvedPath
    };
}

QString baseDirectoryFor(const QString &baseDir)
{
    const QString normalized = baseDir.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QStringLiteral("desktop")) {
        return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    }
    if (normalized == QStringLiteral("documents") || normalized == QStringLiteral("document")) {
        return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    if (normalized == QStringLiteral("downloads") || normalized == QStringLiteral("download")) {
        return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    if (normalized == QStringLiteral("pictures") || normalized == QStringLiteral("picture")) {
        return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }
    if (normalized == QStringLiteral("music")) {
        return QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    }
    if (normalized == QStringLiteral("videos") || normalized == QStringLiteral("video")) {
        return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    }
    if (normalized == QStringLiteral("home")) {
        return QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    if (normalized == QStringLiteral("appdata")) {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    if (normalized == QStringLiteral("cwd") || normalized == QStringLiteral("workspace")) {
        return QDir::currentPath();
    }
    return {};
}

QString resolveWritablePath(const QString &path, const QString &baseDir)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QFileInfo info(trimmed);
    if (info.isAbsolute()) {
        return QDir::cleanPath(info.absoluteFilePath());
    }

    const QString basePath = baseDirectoryFor(baseDir);
    const QString effectiveBase = basePath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) : basePath;
    if (effectiveBase.isEmpty()) {
        return {};
    }

    return QDir::cleanPath(QDir(effectiveBase).absoluteFilePath(trimmed));
}

QString normalizeUrl(const QString &input)
{
    QString value = input.trimmed();
    if (value.isEmpty()) {
        return {};
    }

    if (value.compare(QStringLiteral("youtube"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("https://www.youtube.com/");
    }

    if (!value.contains(QStringLiteral("://"))) {
        if (value.contains(QRegularExpression(QStringLiteral("^[\\w.-]+\\.[A-Za-z]{2,}(/.*)?$")))) {
            value.prepend(QStringLiteral("https://"));
        } else {
            return {};
        }
    }

    return value;
}

QStringList parseArguments(const QStringList &arguments)
{
    QStringList parsed;
    for (const QString &argument : arguments) {
        if (!argument.trimmed().isEmpty()) {
            parsed.push_back(argument.trimmed());
        }
    }
    return parsed;
}

ActionResult unsupported(const QString &featureName)
{
    return failure(QStringLiteral("%1 is unavailable").arg(featureName),
                   QStringLiteral("%1 is not supported on %2 in this release.")
                       .arg(featureName, PlatformRuntime::platformLabel()));
}

class GenericComputerControlBackend : public ComputerControlBackend
{
public:
    ActionResult writeTextFile(const QString &path,
                               const QString &content,
                               bool overwrite,
                               const QString &baseDir) const override
    {
        const QString resolvedPath = resolveWritablePath(path, baseDir);
        if (resolvedPath.isEmpty()) {
            return failure(QStringLiteral("File path is required"),
                           QStringLiteral("Provide an absolute path or a relative path with a valid base directory."));
        }

        const QFileInfo info(resolvedPath);
        if (info.fileName().isEmpty()) {
            return failure(QStringLiteral("Invalid file path"),
                           QStringLiteral("The target path must include a file name."));
        }
        if (info.exists() && !overwrite) {
            return failure(QStringLiteral("File already exists"),
                           QStringLiteral("Refusing to overwrite %1 without overwrite=true.").arg(resolvedPath));
        }

        QDir().mkpath(info.absolutePath());
        QFile file(resolvedPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            return failure(QStringLiteral("File write failed"),
                           QStringLiteral("Could not open %1 for writing.").arg(resolvedPath));
        }

        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << content;
        stream.flush();

        return success(QStringLiteral("Created %1").arg(info.fileName()),
                       QStringLiteral("Wrote %1 bytes to %2").arg(content.toUtf8().size()).arg(resolvedPath),
                       {},
                       resolvedPath);
    }

    ActionResult openUrl(const QString &url) const override
    {
        const QString normalized = normalizeUrl(url);
        if (normalized.isEmpty()) {
            return failure(QStringLiteral("Invalid URL"),
                           QStringLiteral("Provide a full URL or a hostname like youtube.com."));
        }

        const QUrl parsed = QUrl::fromUserInput(normalized);
        if (!parsed.isValid()) {
            return failure(QStringLiteral("Invalid URL"),
                           QStringLiteral("Could not parse %1").arg(normalized));
        }

        if (!QDesktopServices::openUrl(parsed)) {
            return failure(QStringLiteral("Browser launch failed"),
                           QStringLiteral("The operating system did not accept %1").arg(parsed.toString()));
        }

        return success(QStringLiteral("Opened %1").arg(parsed.toString()),
                       QStringLiteral("Requested the default handler for %1").arg(parsed.toString()));
    }

    ActionResult listApps(const QString &, int) const override
    {
        return unsupported(QStringLiteral("App listing"));
    }

    ActionResult launchApp(const QString &, const QStringList &) const override
    {
        return unsupported(QStringLiteral("App launch"));
    }

    ActionResult setTimer(int, const QString &, const QString &) const override
    {
        return unsupported(QStringLiteral("Timer notifications"));
    }
};

#ifdef Q_OS_WIN
QString quotePowerShell(QString value)
{
    value.replace(QStringLiteral("'"), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
}

bool waitForProcess(QProcess &process, int timeoutMs)
{
    if (process.waitForFinished(timeoutMs)) {
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }

    process.kill();
    process.waitForFinished(2000);
    return false;
}

QString runPowerShell(const QString &command, int timeoutMs, bool *ok = nullptr, QString *stderrText = nullptr)
{
    QProcess process;
    process.start(QStringLiteral("powershell"),
                  {QStringLiteral("-NoProfile"),
                   QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                   QStringLiteral("-Command"),
                   command});
    const bool successState = waitForProcess(process, timeoutMs);
    if (ok) {
        *ok = successState;
    }
    if (stderrText) {
        *stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    } else {
        process.readAllStandardError();
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

QStringList startMenuRoots()
{
    return {
        QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation),
        QDir::cleanPath(qEnvironmentVariable("ProgramData") + QStringLiteral("/Microsoft/Windows/Start Menu/Programs"))
    };
}

QStringList findStartMenuCandidates(const QString &target)
{
    QStringList candidates;
    const QString lowered = target.trimmed().toLower();
    if (lowered.isEmpty()) {
        return candidates;
    }

    for (const QString &root : startMenuRoots()) {
        if (root.isEmpty() || !QFileInfo::exists(root)) {
            continue;
        }

        QDirIterator it(root,
                        {QStringLiteral("*.lnk"), QStringLiteral("*.exe")},
                        QDir::Files | QDir::Readable,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString candidate = it.next();
            const QFileInfo info(candidate);
            const QString baseName = info.completeBaseName().toLower();
            if (baseName.contains(lowered)) {
                candidates.push_back(info.absoluteFilePath());
            }
            if (candidates.size() >= 12) {
                break;
            }
        }

        if (candidates.size() >= 12) {
            break;
        }
    }

    candidates.removeDuplicates();
    return candidates;
}

ActionResult launchStartAppByName(const QString &target)
{
    QString stderrText;
    bool ok = false;
    const QString command =
        QStringLiteral("$apps = Get-StartApps | Where-Object { $_.Name -like %1 } | Sort-Object Name | Select-Object -First 8 Name,AppID;"
                       "if ($apps.Count -eq 1) { explorer.exe (\"shell:AppsFolder\\\" + $apps[0].AppID) | Out-Null; "
                       "Write-Output (\"LAUNCHED|\" + $apps[0].Name + \"|\" + $apps[0].AppID) } "
                       "elseif ($apps.Count -gt 1) { $apps | ForEach-Object { Write-Output (\"MATCH|\" + $_.Name + \"|\" + $_.AppID) } } "
                       "else { Write-Output \"NO_MATCH\" }")
            .arg(quotePowerShell(QStringLiteral("*%1*").arg(target.trimmed())));
    const QString output = runPowerShell(command, 12000, &ok, &stderrText).trimmed();
    if (!ok) {
        return failure(QStringLiteral("App lookup failed"),
                       stderrText.isEmpty() ? QStringLiteral("Get-StartApps failed.") : stderrText);
    }

    if (output.startsWith(QStringLiteral("LAUNCHED|"))) {
        const QStringList parts = output.split(QChar::fromLatin1('|'));
        const QString name = parts.value(1);
        return success(QStringLiteral("Opened %1").arg(name.isEmpty() ? target : name),
                       QStringLiteral("Launched start app %1").arg(name.isEmpty() ? target : name));
    }

    if (output == QStringLiteral("NO_MATCH")) {
        return failure(QStringLiteral("App not found"),
                       QStringLiteral("No installed start app matched \"%1\".").arg(target));
    }

    QStringList matches;
    const QStringList lines = output.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("MATCH|"))) {
            const QStringList parts = line.split(QChar::fromLatin1('|'));
            matches.push_back(QStringLiteral("%1 | %2").arg(parts.value(1), parts.value(2)));
        }
    }

    if (!matches.isEmpty()) {
        return {
            .success = false,
            .summary = QStringLiteral("Multiple apps matched"),
            .detail = QStringLiteral("More than one installed app matched \"%1\".").arg(target),
            .lines = matches
        };
    }

    return failure(QStringLiteral("App not found"),
                   QStringLiteral("No installed start app matched \"%1\".").arg(target));
}

class WindowsComputerControlBackend final : public GenericComputerControlBackend
{
public:
    ActionResult listApps(const QString &query, int limit) const override
    {
        const int boundedLimit = std::clamp(limit, 1, 50);
        QString stderrText;
        bool ok = false;
        QString command = QStringLiteral("$apps = Get-StartApps | Sort-Object Name;");
        if (!query.trimmed().isEmpty()) {
            command += QStringLiteral("$apps = $apps | Where-Object { $_.Name -like %1 };")
                .arg(quotePowerShell(QStringLiteral("*%1*").arg(query.trimmed())));
        }
        command += QStringLiteral("$apps | Select-Object -First %1 Name,AppID | ForEach-Object { Write-Output ($_.Name + \" | \" + $_.AppID) }")
            .arg(boundedLimit);

        const QString output = runPowerShell(command, 12000, &ok, &stderrText).trimmed();
        if (!ok) {
            return failure(QStringLiteral("App listing failed"),
                           stderrText.isEmpty() ? QStringLiteral("Get-StartApps failed.") : stderrText);
        }

        const QStringList lines = output.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
        if (lines.isEmpty()) {
            return failure(QStringLiteral("No apps found"),
                           query.trimmed().isEmpty()
                               ? QStringLiteral("No installed start apps were returned.")
                               : QStringLiteral("No installed start apps matched \"%1\".").arg(query));
        }

        return success(QStringLiteral("Found %1 app entries").arg(lines.size()),
                       query.trimmed().isEmpty()
                           ? QStringLiteral("Listed installed start apps.")
                           : QStringLiteral("Listed apps matching \"%1\".").arg(query),
                       lines);
    }

    ActionResult launchApp(const QString &target, const QStringList &arguments) const override
    {
        const QString trimmedTarget = target.trimmed();
        if (trimmedTarget.isEmpty()) {
            return failure(QStringLiteral("App target is required"),
                           QStringLiteral("Provide an executable path, app name, or start menu app name."));
        }

        const QStringList cleanedArguments = parseArguments(arguments);
        QFileInfo targetInfo(trimmedTarget);
        const QString executableFromPath = targetInfo.isAbsolute()
            ? targetInfo.absoluteFilePath()
            : QStandardPaths::findExecutable(trimmedTarget);
        if (!executableFromPath.isEmpty() && QFileInfo::exists(executableFromPath)) {
            if (!QProcess::startDetached(executableFromPath, cleanedArguments)) {
                return failure(QStringLiteral("App launch failed"),
                               QStringLiteral("The process could not be started: %1").arg(executableFromPath));
            }
            return success(QStringLiteral("Opened %1").arg(QFileInfo(executableFromPath).fileName()),
                           QStringLiteral("Launched %1").arg(executableFromPath));
        }

        if (targetInfo.isAbsolute() && QFileInfo::exists(targetInfo.absoluteFilePath())) {
            if (!QProcess::startDetached(targetInfo.absoluteFilePath(), cleanedArguments)) {
                return failure(QStringLiteral("App launch failed"),
                               QStringLiteral("The process could not be started: %1").arg(targetInfo.absoluteFilePath()));
            }
            return success(QStringLiteral("Opened %1").arg(targetInfo.fileName()),
                           QStringLiteral("Launched %1").arg(targetInfo.absoluteFilePath()));
        }

        const ActionResult startAppResult = launchStartAppByName(trimmedTarget);
        if (startAppResult.success) {
            return startAppResult;
        }

        const QStringList shortcutCandidates = findStartMenuCandidates(trimmedTarget);
        if (shortcutCandidates.size() == 1) {
            if (!QProcess::startDetached(shortcutCandidates.first(), cleanedArguments)) {
                return failure(QStringLiteral("App launch failed"),
                               QStringLiteral("The shortcut could not be started: %1").arg(shortcutCandidates.first()));
            }
            return success(QStringLiteral("Opened %1").arg(QFileInfo(shortcutCandidates.first()).completeBaseName()),
                           QStringLiteral("Launched shortcut %1").arg(shortcutCandidates.first()));
        }

        if (shortcutCandidates.size() > 1) {
            QStringList lines;
            for (const QString &candidate : shortcutCandidates) {
                lines.push_back(QFileInfo(candidate).completeBaseName() + QStringLiteral(" | ") + candidate);
            }
            return {
                .success = false,
                .summary = QStringLiteral("Multiple apps matched"),
                .detail = QStringLiteral("More than one app or shortcut matched \"%1\".").arg(trimmedTarget),
                .lines = lines
            };
        }

        return failure(startAppResult.summary.isEmpty() ? QStringLiteral("App not found") : startAppResult.summary,
                       startAppResult.detail.isEmpty()
                           ? QStringLiteral("No installed app matched \"%1\".").arg(trimmedTarget)
                           : startAppResult.detail);
    }

    ActionResult setTimer(int durationSeconds, const QString &title, const QString &message) const override
    {
        if (durationSeconds <= 0 || durationSeconds > 86400) {
            return failure(QStringLiteral("Invalid timer duration"),
                           QStringLiteral("duration_seconds must be between 1 and 86400."));
        }

        const QString effectiveTitle = title.trimmed().isEmpty() ? QStringLiteral("VAXIL Timer") : title.trimmed();
        const QString effectiveMessage = message.trimmed().isEmpty()
            ? QStringLiteral("Timer finished after %1 seconds.").arg(durationSeconds)
            : message.trimmed();

        const QString command =
            QStringLiteral("Start-Process powershell -WindowStyle Hidden -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-Command',"
                           "\"Start-Sleep -Seconds %1; "
                           "Add-Type -AssemblyName PresentationFramework; "
                           "[void][System.Windows.MessageBox]::Show(%2,%3)\")")
                .arg(durationSeconds)
                .arg(quotePowerShell(effectiveMessage))
                .arg(quotePowerShell(effectiveTitle));

        QString stderrText;
        bool ok = false;
        runPowerShell(command, 8000, &ok, &stderrText);
        if (!ok) {
            return failure(QStringLiteral("Timer creation failed"),
                           stderrText.isEmpty() ? QStringLiteral("The timer process could not be started.") : stderrText);
        }

        return success(QStringLiteral("Timer set for %1 seconds").arg(durationSeconds),
                       QStringLiteral("A detached reminder window will appear when the timer completes."));
    }
};
#endif

const ComputerControlBackend &backend()
{
#ifdef Q_OS_WIN
    static const WindowsComputerControlBackend windowsBackend;
    return windowsBackend;
#else
    static const GenericComputerControlBackend genericBackend;
    return genericBackend;
#endif
}

}

ActionResult writeTextFile(const QString &path, const QString &content, bool overwrite, const QString &baseDir)
{
    return backend().writeTextFile(path, content, overwrite, baseDir);
}

ActionResult openUrl(const QString &url)
{
    return backend().openUrl(url);
}

ActionResult listApps(const QString &query, int limit)
{
    return backend().listApps(query, limit);
}

ActionResult launchApp(const QString &target, const QStringList &arguments)
{
    return backend().launchApp(target, arguments);
}

ActionResult setTimer(int durationSeconds, const QString &title, const QString &message)
{
    return backend().setTimer(durationSeconds, title, message);
}

}
