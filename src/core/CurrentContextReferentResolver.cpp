#include "core/CurrentContextReferentResolver.h"

#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace {
QString normalized(QString value)
{
    return value.simplified().toLower();
}

bool containsAny(const QString &text, const QStringList &needles)
{
    for (const QString &needle : needles) {
        if (text.contains(needle)) {
            return true;
        }
    }
    return false;
}

bool hasFreshContext(const CurrentContextReferentInput &input)
{
    return input.desktopContextAtMs > 0
        && input.nowMs >= input.desktopContextAtMs
        && (input.nowMs - input.desktopContextAtMs) <= 90000;
}

bool isCurrentPageRequest(const QString &text)
{
    return containsAny(text, {
        QStringLiteral("current page"),
        QStringLiteral("this page"),
        QStringLiteral("read the page"),
        QStringLiteral("page result"),
        QStringLiteral("result page"),
        QStringLiteral("search result page"),
        QStringLiteral("the result"),
        QStringLiteral("the results"),
        QStringLiteral("from the result"),
        QStringLiteral("in the result")
    });
}

bool isCurrentFileRequest(const QString &text)
{
    return containsAny(text, {
        QStringLiteral("current file"),
        QStringLiteral("opened file"),
        QStringLiteral("open file"),
        QStringLiteral("this file"),
        QStringLiteral("read the file"),
        QStringLiteral("summarize the file"),
        QStringLiteral("what's in this file"),
        QStringLiteral("what is in this file")
    });
}

QString firstStringValue(const QVariantMap &map, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QString value = map.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString directUrlFromContext(const QVariantMap &context)
{
    const QString rawUrl = firstStringValue(context, {
        QStringLiteral("url"),
        QStringLiteral("pageUrl"),
        QStringLiteral("browserUrl"),
        QStringLiteral("currentUrl"),
        QStringLiteral("activeUrl")
    });
    const QUrl parsed(rawUrl);
    if (parsed.isValid() && !parsed.scheme().isEmpty() && !parsed.host().isEmpty()) {
        return rawUrl;
    }
    return {};
}

AgentTask task(const QString &type, const QJsonObject &args, int priority = 95)
{
    AgentTask result;
    result.type = type;
    result.args = args;
    result.priority = priority;
    return result;
}

CurrentContextResolution taskResolution(const QString &reasonCode,
                                        IntentType intent,
                                        const QString &message,
                                        const QString &status,
                                        const AgentTask &agentTask)
{
    CurrentContextResolution result;
    result.kind = CurrentContextResolutionKind::Task;
    result.reasonCode = reasonCode;
    result.decision.kind = InputRouteKind::BackgroundTasks;
    result.decision.intent = intent;
    result.decision.message = message;
    result.decision.status = status;
    result.decision.speak = true;
    result.decision.tasks = {agentTask};
    return result;
}

CurrentContextResolution localResolution(CurrentContextResolutionKind kind,
                                         const QString &reasonCode,
                                         const QString &message,
                                         const QString &status)
{
    CurrentContextResolution result;
    result.kind = kind;
    result.reasonCode = reasonCode;
    result.message = message;
    result.status = status;
    result.decision.kind = InputRouteKind::LocalResponse;
    result.decision.message = message;
    result.decision.status = status;
    result.decision.speak = true;
    return result;
}

QStringList skipDirectoryNames()
{
    return {
        QStringLiteral(".git"),
        QStringLiteral(".cache"),
        QStringLiteral("build"),
        QStringLiteral("build-msvc"),
        QStringLiteral("build-release"),
        QStringLiteral("bin"),
        QStringLiteral("node_modules"),
        QStringLiteral("third_party"),
        QStringLiteral(".venv")
    };
}

bool isLikelyBinaryFile(const QFileInfo &info)
{
    static const QSet<QString> binaryExtensions{
        QStringLiteral("exe"),
        QStringLiteral("dll"),
        QStringLiteral("lib"),
        QStringLiteral("pdb"),
        QStringLiteral("obj"),
        QStringLiteral("bin"),
        QStringLiteral("png"),
        QStringLiteral("jpg"),
        QStringLiteral("jpeg"),
        QStringLiteral("gif"),
        QStringLiteral("webp"),
        QStringLiteral("wav"),
        QStringLiteral("mp3"),
        QStringLiteral("mp4"),
        QStringLiteral("onnx"),
        QStringLiteral("gguf"),
        QStringLiteral("zip")
    };
    return binaryExtensions.contains(info.suffix().toLower());
}

QStringList resolveCandidateFiles(const QString &workspaceRoot, const QString &documentContext)
{
    QString document = documentContext.trimmed();
    document.remove(QRegularExpression(QStringLiteral("^\\s*[\\-|:]+\\s*")));
    document.remove(QRegularExpression(QStringLiteral("\\s*\\([0-9]+\\)\\s*$")));
    document = document.simplified();
    if (document.isEmpty()) {
        return {};
    }

    const QFileInfo documentInfo(document);
    if (documentInfo.isAbsolute() && documentInfo.exists()) {
        return {QDir::cleanPath(documentInfo.absoluteFilePath())};
    }

    const QDir root(workspaceRoot);
    if (!root.exists()) {
        return {};
    }

    const QString fileName = QFileInfo(document).fileName();
    if (fileName.isEmpty()) {
        return {};
    }

    QStringList matches;
    QDirIterator it(root.absolutePath(),
                    QDir::Files | QDir::Readable | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    const QStringList skipped = skipDirectoryNames();
    int visited = 0;
    while (it.hasNext() && matches.size() < 6 && visited < 25000) {
        const QString path = it.next();
        ++visited;
        const QFileInfo info(path);
        const QString relative = root.relativeFilePath(info.absoluteFilePath());
        const QStringList segments = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        bool skippedDir = false;
        const qsizetype parentSegmentCount = segments.size() > 0 ? segments.size() - 1 : 0;
        for (qsizetype index = 0; index < parentSegmentCount; ++index) {
            const QString &segment = segments.at(index);
            if (skipped.contains(segment, Qt::CaseInsensitive)) {
                skippedDir = true;
                break;
            }
        }
        if (skippedDir) {
            continue;
        }
        if (info.fileName().compare(fileName, Qt::CaseInsensitive) == 0) {
            matches.push_back(QDir::cleanPath(info.absoluteFilePath()));
        }
    }
    std::sort(matches.begin(), matches.end(), [](const QString &left, const QString &right) {
        return left.size() < right.size();
    });
    return matches;
}
}

CurrentContextResolution CurrentContextReferentResolver::resolve(const CurrentContextReferentInput &input)
{
    const QString text = normalized(input.userInput);
    if (text.isEmpty() || !hasFreshContext(input)) {
        return {};
    }

    const QString taskId = input.desktopContext.value(QStringLiteral("taskId")).toString().trimmed();
    if (isCurrentPageRequest(text) && taskId == QStringLiteral("browser_tab")) {
        const QString url = directUrlFromContext(input.desktopContext);
        if (!url.isEmpty()) {
            return taskResolution(
                QStringLiteral("current_referent.browser_direct_page"),
                IntentType::GENERAL_CHAT,
                QStringLiteral("I’ll inspect the current page first."),
                QStringLiteral("Inspecting current page"),
                task(QStringLiteral("browser_fetch_text"), QJsonObject{
                    {QStringLiteral("url"), url},
                    {QStringLiteral("timeout_ms"), 45000},
                    {QStringLiteral("requestedBy"), QStringLiteral("current_page_referent")}
                }));
        }

        const QString document = input.desktopContext.value(QStringLiteral("documentContext")).toString().trimmed();
        const QString site = input.desktopContext.value(QStringLiteral("siteContext")).toString().trimmed();
        QStringList queryParts;
        if (!document.isEmpty()) {
            queryParts.push_back(document);
        }
        if (!site.isEmpty()) {
            queryParts.push_back(site);
        }
        const QString query = queryParts.join(QStringLiteral(" "));
        if (!query.trimmed().isEmpty()) {
            return taskResolution(
                QStringLiteral("current_referent.browser_search_fallback"),
                IntentType::GENERAL_CHAT,
                QStringLiteral("I can’t read a direct page URL from the active tab, so I’ll use search as weaker evidence."),
                QStringLiteral("Searching from page context"),
                task(QStringLiteral("web_search"), QJsonObject{
                    {QStringLiteral("query"), query.trimmed()},
                    {QStringLiteral("evidenceNote"), QStringLiteral("not_direct_page_evidence")}
                }, 80));
        }

        return localResolution(
            CurrentContextResolutionKind::Blocked,
            QStringLiteral("current_referent.browser_unavailable"),
            QStringLiteral("I can see a browser is active, but I do not have a readable page URL or page text. Share the link or paste the page text and I’ll inspect that."),
            QStringLiteral("Page context unavailable"));
    }

    if (isCurrentFileRequest(text) && taskId == QStringLiteral("editor_document")) {
        const QString document = input.desktopContext.value(QStringLiteral("documentContext")).toString().trimmed();
        const QStringList matches = resolveCandidateFiles(input.workspaceRoot, document);
        if (matches.isEmpty()) {
            return localResolution(
                CurrentContextResolutionKind::Blocked,
                QStringLiteral("current_referent.file_unavailable"),
                QStringLiteral("I can see an editor file name, but I cannot resolve it to a readable file path. Tell me the exact path or workspace."),
                QStringLiteral("File path unavailable"));
        }
        if (matches.size() > 1) {
            QStringList choices;
            const QDir root(input.workspaceRoot);
            for (const QString &match : matches.mid(0, 3)) {
                choices.push_back(root.relativeFilePath(match));
            }
            return localResolution(
                CurrentContextResolutionKind::Clarify,
                QStringLiteral("current_referent.file_ambiguous"),
                QStringLiteral("I found multiple files named %1: %2. Which one should I read?")
                    .arg(QFileInfo(document).fileName(), choices.join(QStringLiteral(", "))),
                QStringLiteral("Clarification needed"));
        }

        const QFileInfo fileInfo(matches.first());
        if (isLikelyBinaryFile(fileInfo)) {
            return localResolution(
                CurrentContextResolutionKind::Blocked,
                QStringLiteral("current_referent.file_binary"),
                QStringLiteral("The current file appears to be binary or not useful as UTF-8 text. Which text file should I inspect instead?"),
                QStringLiteral("Unreadable file type"));
        }

        return taskResolution(
            QStringLiteral("current_referent.editor_file"),
            IntentType::READ_FILE,
            QStringLiteral("I’ll read the current editor file first."),
            QStringLiteral("Reading current file"),
            task(QStringLiteral("file_read"), QJsonObject{
                {QStringLiteral("path"), QDir::cleanPath(fileInfo.absoluteFilePath())},
                {QStringLiteral("requestedBy"), QStringLiteral("current_file_referent")}
            }));
    }

    return {};
}
