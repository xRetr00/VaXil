#include "core/agent/IntentDetector.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace {
QString normalizeInput(const QString &input)
{
    QString normalized = input.trimmed().toLower();
    normalized.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return normalized;
}

QString cleanCandidate(QString value)
{
    value = value.trimmed();
    value.remove(QRegularExpression(QStringLiteral("^[\"'`]+|[\"'`?.,!]+$")));
    return value.trimmed();
}

QString absoluteCandidatePath(const QString &candidate, const QString &workspaceRoot)
{
    const QString cleaned = cleanCandidate(candidate);
    if (cleaned.isEmpty()) {
        return {};
    }

    QFileInfo info(cleaned);
    if (info.isAbsolute()) {
        return QDir::cleanPath(info.absoluteFilePath());
    }

    return QDir::cleanPath(QDir(workspaceRoot).absoluteFilePath(cleaned));
}

QString extractPathCandidate(const QString &input)
{
    static const QRegularExpression explicitPathPattern(
        QStringLiteral(R"(([A-Za-z]:\\[^\s"']+|(?:[\w\-.]+[\\/])+[\w\-.]+|[\w\-.]+\.[A-Za-z0-9]{1,8}))"));
    const QRegularExpressionMatch explicitMatch = explicitPathPattern.match(input);
    if (explicitMatch.hasMatch()) {
        return explicitMatch.captured(1);
    }

    static const QRegularExpression namedFilePattern(
        QStringLiteral(R"((?:file|log)\s+(?:called|named)?\s*([A-Za-z0-9_\-\.]+))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch namedMatch = namedFilePattern.match(input);
    if (namedMatch.hasMatch()) {
        return namedMatch.captured(1);
    }

    return {};
}

AgentTask buildTask(const QString &type, const QJsonObject &args, int priority)
{
    AgentTask task;
    task.type = type;
    task.args = args;
    task.priority = priority;
    return task;
}

bool containsAny(const QString &normalized, const QStringList &phrases)
{
    for (const QString &phrase : phrases) {
        if (normalized.contains(phrase)) {
            return true;
        }
    }
    return false;
}

QString latestLogPath(const QString &workspaceRoot, const QString &subdir)
{
    const QString fullRoot = QDir(workspaceRoot).absoluteFilePath(subdir);
    const QFileInfoList files = QDir(fullRoot).entryInfoList({QStringLiteral("*.log")}, QDir::Files | QDir::Readable, QDir::Time);
    return files.isEmpty() ? QString{} : files.first().absoluteFilePath();
}
}

IntentDetector::IntentDetector(QObject *parent)
    : QObject(parent)
{
}

IntentResult IntentDetector::detect(const QString &input, const QString &workspaceRoot) const
{
    const QString normalized = normalizeInput(input);
    const QString cleanWorkspace = QDir::cleanPath(workspaceRoot);

    if (normalized.isEmpty()) {
        return {};
    }

    const bool explicitListRequest = containsAny(normalized, {
        QStringLiteral("list files"),
        QStringLiteral("list the files"),
        QStringLiteral("show files"),
        QStringLiteral("show me the files"),
        QStringLiteral("directory listing"),
        QStringLiteral("current directory"),
        QStringLiteral("current folder"),
        QStringLiteral("current dictionary"),
        QStringLiteral("where are you"),
        QStringLiteral("what directory are you in")
    });
    const bool ambiguousListRequest = containsAny(normalized, {
        QStringLiteral("workspace files"),
        QStringLiteral("current files"),
        QStringLiteral("files in your workspace")
    });
    if ((explicitListRequest || ambiguousListRequest) && !normalized.contains(QStringLiteral("profile"))) {
        return {
            .type = IntentType::LIST_FILES,
            .confidence = explicitListRequest ? 0.92f : 0.65f,
            .spokenMessage = QStringLiteral("All right, I'm listing the files now. The result will show up in the panel."),
            .tasks = {buildTask(QStringLiteral("dir_list"),
                                QJsonObject{{QStringLiteral("path"), cleanWorkspace}},
                                90)}
        };
    }

    const bool explicitReadRequest = containsAny(normalized, {
        QStringLiteral("read file"),
        QStringLiteral("open file"),
        QStringLiteral("show file"),
        QStringLiteral("read the log"),
        QStringLiteral("read this file"),
        QStringLiteral("read logs"),
        QStringLiteral("read your own logs"),
        QStringLiteral("check logs"),
        QStringLiteral("look in the logs"),
        QStringLiteral("startup log"),
        QStringLiteral("jarvis log")
    });
    const bool weakReadRequest = containsAny(normalized, {
        QStringLiteral("check this file"),
        QStringLiteral("look at this file"),
        QStringLiteral("can you read")
    });
    if (explicitReadRequest || weakReadRequest) {
        QString candidate = absoluteCandidatePath(extractPathCandidate(input), cleanWorkspace);
        if (candidate.isEmpty() && containsAny(normalized, {QStringLiteral("startup log")})) {
            candidate = QDir(cleanWorkspace).absoluteFilePath(QStringLiteral("bin/logs/startup.log"));
        }
        if (candidate.isEmpty() && containsAny(normalized, {QStringLiteral("jarvis log"), QStringLiteral("your own logs"), QStringLiteral("read logs"), QStringLiteral("check logs")})) {
            candidate = QDir(cleanWorkspace).absoluteFilePath(QStringLiteral("bin/logs/jarvis.log"));
        }
        if (candidate.isEmpty() && containsAny(normalized, {QStringLiteral("ai log"), QStringLiteral("latest ai log")})) {
            candidate = latestLogPath(cleanWorkspace, QStringLiteral("bin/logs/AI"));
        }
        if (!candidate.isEmpty()) {
            return {
                .type = IntentType::READ_FILE,
                .confidence = explicitReadRequest ? 0.93f : 0.62f,
                .spokenMessage = QStringLiteral("Okay, I'm reading that file now. You'll see the content in the panel."),
                .tasks = {buildTask(QStringLiteral("file_read"),
                                    QJsonObject{{QStringLiteral("path"), candidate}},
                                    95)}
            };
        }
    }

    static const QRegularExpression writePattern(
        QStringLiteral(R"((?:write|create|make)\s+(?:a\s+)?file\s+([^\s]+)(?:\s+(?:with|containing)\s+(.+))?)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch writeMatch = writePattern.match(input);
    if (writeMatch.hasMatch()) {
        const QString candidate = absoluteCandidatePath(writeMatch.captured(1), cleanWorkspace);
        const QString content = cleanCandidate(writeMatch.captured(2));
        if (!candidate.isEmpty() && !content.isEmpty()) {
            return {
                .type = IntentType::WRITE_FILE,
                .confidence = 0.94f,
                .spokenMessage = QStringLiteral("Understood. I'm writing that file in the background, and the result will appear in the panel."),
                .tasks = {buildTask(QStringLiteral("file_write"),
                                    QJsonObject{
                                        {QStringLiteral("path"), candidate},
                                        {QStringLiteral("content"), content}
                                    },
                                    95)}
            };
        }

        return {
            .type = IntentType::WRITE_FILE,
            .confidence = 0.58f,
            .spokenMessage = QStringLiteral("I can do that, but I need the exact file path and content."),
            .tasks = {}
        };
    }

    if (normalized.startsWith(QStringLiteral("remember that "))
        || normalized.startsWith(QStringLiteral("remember "))
        || normalized.startsWith(QStringLiteral("save this preference"))) {
        QString memoryText = input.trimmed();
        memoryText.remove(QRegularExpression(QStringLiteral("^(remember that|remember|save this preference)\\s+"),
                                             QRegularExpression::CaseInsensitiveOption));
        memoryText = memoryText.trimmed();
        if (!memoryText.isEmpty()) {
            return {
                .type = IntentType::MEMORY_WRITE,
                .confidence = 0.9f,
                .spokenMessage = QStringLiteral("Okay, I'll keep that in memory. You'll see the saved entry in the panel."),
                .tasks = {buildTask(QStringLiteral("memory_write"),
                                    QJsonObject{
                                        {QStringLiteral("kind"), QStringLiteral("preference")},
                                        {QStringLiteral("title"), QStringLiteral("general_preference")},
                                        {QStringLiteral("key"), QStringLiteral("general_preference")},
                                        {QStringLiteral("content"), memoryText},
                                        {QStringLiteral("value"), memoryText}
                                    },
                                    70)}
            };
        }
    }

    return {
        .type = IntentType::GENERAL_CHAT,
        .confidence = 0.0f,
        .spokenMessage = {},
        .tasks = {}
    };
}
