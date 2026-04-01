#include "core/AssistantController.h"

#include <algorithm>
#include <optional>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QTime>
#include <QUrl>

#include <nlohmann/json.hpp>

#include "ai/AiBackendClient.h"
#include "ai/ModelCatalogService.h"
#include "ai/PromptAdapter.h"
#include "ai/RuntimeAiBackendClient.h"
#include "ai/SpokenReply.h"
#include "ai/ReasoningRouter.h"
#include "ai/StreamAssembler.h"
#include "agent/AgentToolbox.h"
#include "core/agent/IntentDetector.h"
#include "core/agent/IntentEngine.h"
#include "core/AiRequestCoordinator.h"
#include "core/InputRouter.h"
#include "core/IntentRouter.h"
#include "core/LocalResponseEngine.h"
#include "core/MemoryPolicyHandler.h"
#include "core/ResponseFinalizer.h"
#include "core/tasks/TaskDispatcher.h"
#include "core/tasks/ToolWorker.h"
#include "devices/DeviceManager.h"
#include "logging/LoggingService.h"
#include "memory/MemoryStore.h"
#include "settings/AppSettings.h"
#include "settings/IdentityProfileService.h"
#include "skills/SkillStore.h"
#include "stt/RuntimeSpeechRecognizer.h"
#include "tts/TtsEngine.h"
#include "tts/WorkerTtsEngine.h"
#include "vision/VisionIngestService.h"
#include "vision/GestureActionRouter.h"
#include "vision/GestureInterpreter.h"
#include "vision/GestureStateMachine.h"
#include "vision/VisionContextGate.h"
#include "vision/WorldStateCache.h"
#include "wakeword/WakeWordDetector.h"
#include "wakeword/SherpaWakeWordEngine.h"
#include "wakeword/WakeWordEngine.h"
#include "workers/VoicePipelineRuntime.h"

namespace {
QString stateToString(AssistantState state)
{
    switch (state) {
    case AssistantState::Idle:
        return QStringLiteral("IDLE");
    case AssistantState::Listening:
        return QStringLiteral("LISTENING");
    case AssistantState::Processing:
        return QStringLiteral("THINKING");
    case AssistantState::Speaking:
        return QStringLiteral("SPEAKING");
    }
    return QStringLiteral("IDLE");
}

QString compactSurfaceText(QString text, int maxLength = 72)
{
    text = text.simplified();
    if (text.size() > maxLength) {
        text = text.left(maxLength - 3).trimmed() + QStringLiteral("...");
    }
    return text;
}

QString formatDurationForSurface(int totalSeconds)
{
    if (totalSeconds <= 0) {
        return {};
    }

    if (totalSeconds >= 3600) {
        const int hours = totalSeconds / 3600;
        const int minutes = (totalSeconds % 3600) / 60;
        return minutes > 0
            ? QStringLiteral("%1 hr %2 min").arg(hours).arg(minutes)
            : QStringLiteral("%1 hr").arg(hours);
    }

    if (totalSeconds >= 60) {
        const int minutes = totalSeconds / 60;
        const int seconds = totalSeconds % 60;
        return seconds > 0
            ? QStringLiteral("%1 min %2 sec").arg(minutes).arg(seconds)
            : QStringLiteral("%1 min").arg(minutes);
    }

    return QStringLiteral("%1 sec").arg(totalSeconds);
}

QString firstNonEmptyArg(const QJsonObject &args, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QString value = args.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString compactPathForSurface(const QString &pathText)
{
    const QString trimmed = pathText.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QFileInfo info(trimmed);
    const QString fileName = info.fileName().trimmed();
    if (!fileName.isEmpty()) {
        return compactSurfaceText(fileName, 56);
    }

    return compactSurfaceText(trimmed, 56);
}

QString compactUrlForSurface(const QString &urlText)
{
    const QString trimmed = urlText.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QUrl url(trimmed);
    if (url.isValid() && !url.host().trimmed().isEmpty()) {
        return compactSurfaceText(url.host(), 48);
    }

    return compactSurfaceText(trimmed, 48);
}

QString normalizeForRouting(QString text)
{
    text = text.trimmed();
    while (!text.isEmpty() && QStringLiteral(",.!?:;").contains(text.front())) {
        text.remove(0, 1);
        text = text.trimmed();
    }
    return text;
}

bool isCurrentTimeQuery(const QString &input)
{
    const QString lowered = input.toLower();
    return lowered.contains(QStringLiteral("what time is it"))
        || lowered.contains(QStringLiteral("what's the time"))
        || lowered.contains(QStringLiteral("whats the time"))
        || lowered.contains(QStringLiteral("time now"))
        || lowered.contains(QStringLiteral("current time"));
}

bool isCurrentDateQuery(const QString &input)
{
    const QString lowered = input.toLower();
    return lowered.contains(QStringLiteral("what day is it"))
        || lowered.contains(QStringLiteral("what's the date"))
        || lowered.contains(QStringLiteral("whats the date"))
        || lowered.contains(QStringLiteral("today's date"))
        || lowered.contains(QStringLiteral("todays date"))
        || lowered.contains(QStringLiteral("current date"));
}

QString normalizeStageAnnotation(const QString &input)
{
    QString normalized = input.trimmed().toLower();
    normalized.remove(QRegularExpression(QStringLiteral("^[\\[(]+|[\\])]+$")));
    normalized.remove(QRegularExpression(QStringLiteral("[^a-z]")));
    return normalized;
}

bool isLikelyNonSpeechTranscript(const QString &input)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }

    const bool bracketed = (trimmed.startsWith(QChar::fromLatin1('[')) && trimmed.endsWith(QChar::fromLatin1(']')))
        || (trimmed.startsWith(QChar::fromLatin1('(')) && trimmed.endsWith(QChar::fromLatin1(')')));
    if (!bracketed) {
        return false;
    }

    const QString normalized = normalizeStageAnnotation(trimmed);
    return normalized == QStringLiteral("musicplaying")
        || normalized == QStringLiteral("applause")
        || normalized == QStringLiteral("laughter")
        || normalized == QStringLiteral("silence")
        || normalized == QStringLiteral("noise")
        || normalized == QStringLiteral("backgroundnoise")
        || normalized == QStringLiteral("inaudible");
}

bool isLikelySttArtifactTranscript(const QString &input)
{
    QString normalized = input.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    normalized = normalized.simplified();
    if (normalized.isEmpty()) {
        return true;
    }

    static const QStringList knownArtifacts = {
        QStringLiteral("transcribed by"),
        QStringLiteral("transcribe literally"),
        QStringLiteral("transcribed literally"),
        QStringLiteral("subtitle by"),
        QStringLiteral("subtitles by"),
        QStringLiteral("captions by"),
        QStringLiteral("thanks for watching"),
        QStringLiteral("learn english for free"),
        QStringLiteral("engvid"),
        QStringLiteral("subscribe for more"),
        QStringLiteral("follow for more")
    };

    for (const QString &phrase : knownArtifacts) {
        const QString escaped = QRegularExpression::escape(phrase);
        const QString pattern = QStringLiteral("(^|\\s)%1(\\s|$)").arg(escaped).replace(QStringLiteral("\\ "), QStringLiteral("\\s+"));
        if (QRegularExpression(pattern).match(normalized).hasMatch()) {
            return true;
        }
    }

    const QStringList words = normalized.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (QRegularExpression(QStringLiteral("\\b(?:www\\s+)?[a-z0-9]+\\s+(?:com|net|org|io|ai)\\b")).match(normalized).hasMatch()) {
        return true;
    }
    if (words.size() <= 3) {
        for (const QString &word : words) {
            if (word.startsWith(QStringLiteral("transcrib"))
                || word.startsWith(QStringLiteral("subtitle"))
                || word.startsWith(QStringLiteral("caption"))) {
                return true;
            }
        }
    }

    return false;
}

QStringList transcriptWords(const QString &input)
{
    return input.toLower().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

QString normalizePhrase(const QString &input)
{
    QString normalized = input.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    return normalized.simplified();
}

QString userFacingPromptForLogging(const QString &input)
{
    const QString trimmed = input.trimmed();
    if (!trimmed.startsWith(QStringLiteral("You previously asked me to search the web."), Qt::CaseInsensitive)) {
        return trimmed;
    }

    const QRegularExpression queryPattern(QStringLiteral("User query:\\s*(.+?)(?:\\n|$)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = queryPattern.match(trimmed);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }

    return QStringLiteral("[web search summary]");
}

bool containsPhrase(const QString &normalizedInput, const QString &phrase)
{
    const QString escaped = QRegularExpression::escape(phrase);
    const QString pattern = QStringLiteral("(^|\\s)%1(\\s|$)").arg(escaped).replace(QStringLiteral("\\ "), QStringLiteral("\\s+"));
    return QRegularExpression(pattern).match(normalizedInput).hasMatch();
}

bool isConversationStopPhrase(const QString &input)
{
    const QString normalized = normalizePhrase(input);
    if (normalized.isEmpty()) {
        return false;
    }

    static const QStringList phrases = {
        QStringLiteral("stop"),
        QStringLiteral("stop listening"),
        QStringLiteral("stop talking"),
        QStringLiteral("sleep"),
        QStringLiteral("go to sleep"),
        QStringLiteral("sleep now"),
        QStringLiteral("shutdown"),
        QStringLiteral("shut down"),
        QStringLiteral("bye"),
        QStringLiteral("goodbye"),
        QStringLiteral("good bye"),
        QStringLiteral("thank you"),
        QStringLiteral("thanks"),
        QStringLiteral("no thanks"),
        QStringLiteral("never mind"),
        QStringLiteral("cancel"),
        QStringLiteral("that is all"),
        QStringLiteral("thats all"),
        QStringLiteral("that s all"),
        QStringLiteral("stand by"),
        QStringLiteral("standby")
    };

    for (const QString &phrase : phrases) {
        if (containsPhrase(normalized, phrase)) {
            return true;
        }
    }

    return false;
}

bool startsWithAllowedFollowUpWord(const QStringList &words)
{
    if (words.isEmpty()) {
        return false;
    }

    static const QSet<QString> allowedStarts = {
        QStringLiteral("what"),
        QStringLiteral("where"),
        QStringLiteral("when"),
        QStringLiteral("why"),
        QStringLiteral("how"),
        QStringLiteral("who"),
        QStringLiteral("can"),
        QStringLiteral("could"),
        QStringLiteral("would"),
        QStringLiteral("will"),
        QStringLiteral("should"),
        QStringLiteral("do"),
        QStringLiteral("does"),
        QStringLiteral("did"),
        QStringLiteral("is"),
        QStringLiteral("are"),
        QStringLiteral("am"),
        QStringLiteral("tell"),
        QStringLiteral("show"),
        QStringLiteral("list"),
        QStringLiteral("read"),
        QStringLiteral("write"),
        QStringLiteral("create"),
        QStringLiteral("open"),
        QStringLiteral("close"),
        QStringLiteral("start"),
        QStringLiteral("stop"),
        QStringLiteral("set"),
        QStringLiteral("search"),
        QStringLiteral("find"),
        QStringLiteral("remember"),
        QStringLiteral("forget"),
        QStringLiteral("save"),
        QStringLiteral("delete"),
        QStringLiteral("play"),
        QStringLiteral("pause"),
        QStringLiteral("resume"),
        QStringLiteral("make"),
        QStringLiteral("give"),
        QStringLiteral("call"),
        QStringLiteral("name"),
        QStringLiteral("check"),
        QStringLiteral("run"),
        QStringLiteral("explain"),
        QStringLiteral("summarize"),
        QStringLiteral("use"),
        QStringLiteral("go"),
        QStringLiteral("okay"),
        QStringLiteral("ok"),
        QStringLiteral("please"),
        QStringLiteral("thanks"),
        QStringLiteral("thank")
    };

    return allowedStarts.contains(words.first());
}

QString firstExistingPath(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    return {};
}

QString extractJsonObjectPayload(const QString &payload)
{
    const QString trimmed = payload.trimmed();
    const int start = trimmed.indexOf(QChar::fromLatin1('{'));
    const int end = trimmed.lastIndexOf(QChar::fromLatin1('}'));
    if (start < 0 || end < start) {
        return trimmed;
    }
    return trimmed.mid(start, end - start + 1);
}

QList<AgentToolCall> parseAdapterToolCalls(const nlohmann::json &jsonObject)
{
    QList<AgentToolCall> toolCalls;
    if (jsonObject.contains("tool_calls") && jsonObject.at("tool_calls").is_array()) {
        for (const auto &callJson : jsonObject.at("tool_calls")) {
            if (!callJson.is_object()) {
                continue;
            }

            AgentToolCall call;
            call.name = QString::fromStdString(callJson.value("name", std::string{}));
            call.argumentsJson = QString::fromStdString(callJson.value("arguments_json", std::string{}));
            if (call.argumentsJson.trimmed().isEmpty() && callJson.contains("args") && callJson.at("args").is_object()) {
                call.argumentsJson = QString::fromStdString(callJson.at("args").dump());
            }
            if (!call.name.isEmpty()) {
                toolCalls.push_back(call);
            }
        }
        return toolCalls;
    }

    if (!jsonObject.contains("background_tasks") || !jsonObject.at("background_tasks").is_array()) {
        return toolCalls;
    }

    for (const auto &taskJson : jsonObject.at("background_tasks")) {
        if (!taskJson.is_object()) {
            continue;
        }

        AgentToolCall call;
        call.name = QString::fromStdString(taskJson.value("type", std::string{}));
        if (taskJson.contains("args") && taskJson.at("args").is_object()) {
            call.argumentsJson = QString::fromStdString(taskJson.at("args").dump());
        }
        if (!call.name.isEmpty()) {
            toolCalls.push_back(call);
        }
    }

    return toolCalls;
}

IntentType intentTypeFromString(const QString &value)
{
    const QString normalized = value.trimmed().toUpper();
    if (normalized == QStringLiteral("LIST_FILES")) {
        return IntentType::LIST_FILES;
    }
    if (normalized == QStringLiteral("READ_FILE")) {
        return IntentType::READ_FILE;
    }
    if (normalized == QStringLiteral("WRITE_FILE")) {
        return IntentType::WRITE_FILE;
    }
    if (normalized == QStringLiteral("MEMORY_WRITE")) {
        return IntentType::MEMORY_WRITE;
    }
    return IntentType::GENERAL_CHAT;
}

bool intentRequiresTool(IntentType intent)
{
    return intent == IntentType::LIST_FILES
        || intent == IntentType::READ_FILE
        || intent == IntentType::WRITE_FILE
        || intent == IntentType::MEMORY_WRITE;
}

bool containsAnyNormalized(const QString &input, const QStringList &phrases)
{
    const QString normalized = input.toLower();
    for (const QString &phrase : phrases) {
        if (normalized.contains(phrase)) {
            return true;
        }
    }
    return false;
}

bool isExplicitAgentWorldQuery(const QString &input)
{
    return containsAnyNormalized(input, {
        QStringLiteral("search the web"),
        QStringLiteral("reach the web"),
        QStringLiteral("browse the web"),
        QStringLiteral("search web"),
        QStringLiteral("web search"),
        QStringLiteral("latest news"),
        QStringLiteral("today"),
        QStringLiteral("read your own logs"),
        QStringLiteral("read logs"),
        QStringLiteral("startup log"),
        QStringLiteral("vaxil log"),
        QStringLiteral("jarvis log"),
        QStringLiteral("correct tools available"),
        QStringLiteral("what are the tools"),
        QStringLiteral("what tools"),
        QStringLiteral("reach the tools"),
        QStringLiteral("what are your tools"),
        QStringLiteral("tool list"),
        QStringLiteral("tools available"),
        QStringLiteral("what can you access"),
        QStringLiteral("latest model"),
        QStringLiteral("tools inside the workspace"),
        QStringLiteral("use the tools inside the workspace"),
        QStringLiteral("generate code"),
        QStringLiteral("write code"),
        QStringLiteral("create code"),
        QStringLiteral("code a"),
        QStringLiteral("build a"),
        QStringLiteral("make a script"),
        QStringLiteral("python game"),
        QStringLiteral("snake game")
    });
}

bool isExplicitToolInventoryQuery(const QString &input)
{
    return containsAnyNormalized(input, {
        QStringLiteral("what are the tools"),
        QStringLiteral("what tools"),
        QStringLiteral("what are your tools"),
        QStringLiteral("tool list"),
        QStringLiteral("tools available"),
        QStringLiteral("reach the tools"),
        QStringLiteral("what can you access"),
        QStringLiteral("what tools can you reach"),
        QStringLiteral("correct tools available"),
        QStringLiteral("tools inside the workspace")
    });
}

bool isVisionRelevantQuery(const QString &input)
{
    return containsAnyNormalized(input, {
        QStringLiteral("what do you see"),
        QStringLiteral("can you see"),
        QStringLiteral("do you see"),
        QStringLiteral("what am i holding"),
        QStringLiteral("am i holding"),
        QStringLiteral("holding"),
        QStringLiteral("look at"),
        QStringLiteral("look around"),
        QStringLiteral("around me"),
        QStringLiteral("in front of me"),
        QStringLiteral("on my desk"),
        QStringLiteral("on the desk"),
        QStringLiteral("environment"),
        QStringLiteral("room"),
        QStringLiteral("camera"),
        QStringLiteral("gesture"),
        QStringLiteral("hand"),
        QStringLiteral("finger"),
        QStringLiteral("finger count"),
        QStringLiteral("how many fingers"),
        QStringLiteral("number of fingers"),
        QStringLiteral("middle finger"),
        QStringLiteral("thumbs up"),
        QStringLiteral("thumbs down"),
        QStringLiteral("object"),
        QStringLiteral("what is this"),
        QStringLiteral("what is that")
    });
}

bool isDirectVisionAnswerQuery(const QString &input)
{
    return containsAnyNormalized(input, {
        QStringLiteral("what do you see"),
        QStringLiteral("can you see"),
        QStringLiteral("do you see"),
        QStringLiteral("what am i holding"),
        QStringLiteral("what i'm holding"),
        QStringLiteral("what is in my hand"),
        QStringLiteral("what's in my hand"),
        QStringLiteral("what is on my hand"),
        QStringLiteral("what is this"),
        QStringLiteral("what is that"),
        QStringLiteral("is my hand open"),
        QStringLiteral("is my hand closed"),
        QStringLiteral("open or closed"),
        QStringLiteral("closed or open"),
        QStringLiteral("closed hand"),
        QStringLiteral("fist"),
        QStringLiteral("how many fingers"),
        QStringLiteral("finger count"),
        QStringLiteral("number of fingers"),
        QStringLiteral("thumbs up"),
        QStringLiteral("thumbs down"),
        QStringLiteral("middle finger"),
        QStringLiteral("my hand")
    });
}

bool isVisionFollowUpQuery(const QString &input)
{
    return containsAnyNormalized(input, {
        QStringLiteral("what about now"),
        QStringLiteral("how about now"),
        QStringLiteral("and now"),
        QStringLiteral("right now"),
        QStringLiteral("now can you"),
        QStringLiteral("can you see now"),
        QStringLiteral("do you see now"),
        QStringLiteral("what do you see now")
    });
}

bool isPortableVisionObject(const QString &label)
{
    static const QSet<QString> portableObjects = {
        QStringLiteral("bottle"),
        QStringLiteral("book"),
        QStringLiteral("can"),
        QStringLiteral("cell phone"),
        QStringLiteral("cup"),
        QStringLiteral("fork"),
        QStringLiteral("keyboard"),
        QStringLiteral("knife"),
        QStringLiteral("laptop"),
        QStringLiteral("mouse"),
        QStringLiteral("remote"),
        QStringLiteral("scissors"),
        QStringLiteral("spoon"),
        QStringLiteral("sports ball"),
        QStringLiteral("toothbrush"),
        QStringLiteral("wine glass")
    };
    return portableObjects.contains(label.trimmed().toLower());
}

QString withArticle(const QString &noun)
{
    const QString trimmed = noun.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("something");
    }

    const QChar first = trimmed.front().toLower();
    if (first == QChar::fromLatin1('a')
        || first == QChar::fromLatin1('e')
        || first == QChar::fromLatin1('i')
        || first == QChar::fromLatin1('o')
        || first == QChar::fromLatin1('u')) {
        return QStringLiteral("an %1").arg(trimmed);
    }
    return QStringLiteral("a %1").arg(trimmed);
}

bool isExplicitComputerControlQuery(const QString &input)
{
    return containsAnyNormalized(input, {
        QStringLiteral("open browser"),
        QStringLiteral("open my browser"),
        QStringLiteral("open the browser"),
        QStringLiteral("launch browser"),
        QStringLiteral("start browser"),
        QStringLiteral("in the browser"),
        QStringLiteral("browser tab"),
        QStringLiteral("new tab"),
        QStringLiteral("private tab"),
        QStringLiteral("private window"),
        QStringLiteral("incognito"),
        QStringLiteral("open youtube"),
        QStringLiteral("launch youtube"),
        QStringLiteral("open app"),
        QStringLiteral("open the app"),
        QStringLiteral("launch app"),
        QStringLiteral("launch the app"),
        QStringLiteral("installed apps"),
        QStringLiteral("list apps"),
        QStringLiteral("set timer"),
        QStringLiteral("start timer"),
        QStringLiteral("timer for"),
        QStringLiteral("create file on desktop"),
        QStringLiteral("create a file on desktop"),
        QStringLiteral("write file on desktop"),
        QStringLiteral("desktop file"),
        QStringLiteral("create file in documents"),
        QStringLiteral("create file in downloads")
    });
}

QString sanitizeSimpleFileName(QString fileName)
{
    fileName = fileName.trimmed();
    fileName.remove(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")));
    fileName.remove(QRegularExpression(QStringLiteral("\\s+")));
    if (fileName.isEmpty()) {
        return QStringLiteral("vaxil_note.txt");
    }
    if (!fileName.contains(QChar::fromLatin1('.'))) {
        fileName += QStringLiteral(".txt");
    }
    return fileName;
}

int parseTimerDurationSeconds(const QString &input)
{
    const QRegularExpression pattern(
        QStringLiteral("(?:set|start)?\\s*(?:a\\s*)?timer(?:\\s*for)?\\s*(\\d+)\\s*(seconds?|secs?|minutes?|mins?|hours?|hrs?)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(input);
    if (!match.hasMatch()) {
        return 0;
    }

    int value = match.captured(1).toInt();
    const QString unit = match.captured(2).toLower();
    if (unit.startsWith(QStringLiteral("hour")) || unit.startsWith(QStringLiteral("hr"))) {
        value *= 3600;
    } else if (unit.startsWith(QStringLiteral("minute")) || unit.startsWith(QStringLiteral("min"))) {
        value *= 60;
    }
    return value;
}

bool buildDeterministicComputerTask(const QString &input, AgentTask *task, QString *spoken)
{
    if (task == nullptr || spoken == nullptr) {
        return false;
    }

    const QString lowered = input.toLower().trimmed();

    QRegularExpression ytSearchPattern(
        QStringLiteral("(?:search\\s+(?:on\\s+)?youtube\\s+for|youtube\\s+search\\s+for|find\\s+on\\s+youtube|open\\s+youtube\\s+and\\s+search\\s+for)\\s+(.+)"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch ytMatch = ytSearchPattern.match(input);
    if (ytMatch.hasMatch()) {
        const QString query = ytMatch.captured(1).trimmed();
        if (!query.isEmpty()) {
            const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(query)).replace(QStringLiteral("%20"), QStringLiteral("+"));
            task->type = QStringLiteral("browser_open");
            task->args = QJsonObject{{QStringLiteral("url"), QStringLiteral("https://www.youtube.com/results?search_query=%1").arg(encoded)}};
            task->priority = 90;
            *spoken = QStringLiteral("Opening YouTube search results with Playwright.");
            return true;
        }
    }

    if (lowered.contains(QStringLiteral("open youtube")) || lowered == QStringLiteral("youtube")) {
        task->type = QStringLiteral("browser_open");
        task->args = QJsonObject{{QStringLiteral("url"), QStringLiteral("https://www.youtube.com")}};
        task->priority = 85;
        *spoken = QStringLiteral("Opening YouTube with Playwright.");
        return true;
    }

    const int timerSeconds = parseTimerDurationSeconds(input);
    if (timerSeconds > 0) {
        task->type = QStringLiteral("computer_set_timer");
        task->args = QJsonObject{
            {QStringLiteral("duration_seconds"), timerSeconds},
            {QStringLiteral("title"), QStringLiteral("VAXIL Timer")},
            {QStringLiteral("message"), QStringLiteral("Time is up.")}
        };
        task->priority = 88;
        *spoken = QStringLiteral("Timer set.");
        return true;
    }

    QRegularExpression filePattern(
        QStringLiteral("(?:create|write|make)\\s+(?:a\\s+)?file\\s+(?:on|in)\\s+(desktop|documents|downloads)(?:\\s+(?:called|named))?(?:\\s+([^\\s]+))?(?:.*?(?:with\\s+content|saying)\\s+(.+))?"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch fileMatch = filePattern.match(input);
    if (fileMatch.hasMatch()) {
        const QString baseDir = fileMatch.captured(1).toLower();
        const QString fileName = sanitizeSimpleFileName(fileMatch.captured(2));
        QString content = fileMatch.captured(3).trimmed();
        if (content.isEmpty()) {
            content = QStringLiteral("Created by Vaxil.");
        }

        task->type = QStringLiteral("computer_write_file");
        task->args = QJsonObject{
            {QStringLiteral("path"), fileName},
            {QStringLiteral("content"), content},
            {QStringLiteral("overwrite"), false},
            {QStringLiteral("base_dir"), baseDir}
        };
        task->priority = 87;
        *spoken = QStringLiteral("Creating the file now.");
        return true;
    }

    const bool wantsBrowser = lowered.contains(QStringLiteral("open browser"))
        || lowered.contains(QStringLiteral("launch browser"))
        || lowered.contains(QStringLiteral("start browser"));
    if (wantsBrowser) {
        QString query;
        const int searchIndex = lowered.indexOf(QStringLiteral("search"));
        if (searchIndex >= 0) {
            query = input.mid(searchIndex + QStringLiteral("search").size()).trimmed();
            query.remove(QRegularExpression(QStringLiteral("^(?:google|the\\s+web|web|the\\s+internet|internet)\\b\\s*"),
                                            QRegularExpression::CaseInsensitiveOption));
            query.remove(QRegularExpression(QStringLiteral("^(?:for)\\b\\s*"),
                                            QRegularExpression::CaseInsensitiveOption));
        }

        task->type = QStringLiteral("browser_open");
        if (!query.isEmpty()) {
            const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(query)).replace(QStringLiteral("%20"), QStringLiteral("+"));
            task->args = QJsonObject{{QStringLiteral("url"), QStringLiteral("https://www.google.com/search?q=%1").arg(encoded)}};
            *spoken = QStringLiteral("Opening browser search results with Playwright.");
        } else {
            task->args = QJsonObject{{QStringLiteral("url"), QStringLiteral("https://www.google.com")}};
            *spoken = QStringLiteral("Opening the browser with Playwright.");
        }
        task->priority = 84;
        return true;
    }

    QRegularExpression openAppPattern(
        QStringLiteral("^(?:open|launch|start)\\s+(?:the\\s+)?(.+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch appMatch = openAppPattern.match(input.trimmed());
    if (appMatch.hasMatch()) {
        const QString target = appMatch.captured(1).trimmed();
        const QString targetLower = target.toLower();
        if (!target.isEmpty()
            && !targetLower.contains(QStringLiteral("youtube"))
            && !targetLower.contains(QStringLiteral("website"))
            && !targetLower.contains(QStringLiteral("url"))
            && !targetLower.contains(QStringLiteral("timer"))
            && !targetLower.contains(QStringLiteral("file"))
            && !targetLower.contains(QStringLiteral("browser"))
            && !targetLower.contains(QStringLiteral("search"))
            && !targetLower.contains(QStringLiteral("google"))
            && !targetLower.contains(QStringLiteral("web"))
            && !targetLower.contains(QStringLiteral("internet"))) {
            task->type = QStringLiteral("computer_open_app");
            task->args = QJsonObject{{QStringLiteral("target"), target}};
            task->priority = 86;
            *spoken = QStringLiteral("Opening %1.").arg(target);
            return true;
        }
    }

    return false;
}

bool isExplicitWebSearchQuery(const QString &input)
{
    return containsAnyNormalized(input, {
        QStringLiteral("search"),
        QStringLiteral("search internet"),
        QStringLiteral("search the internet"),
        QStringLiteral("search the web"),
        QStringLiteral("search web"),
        QStringLiteral("search for"),
        QStringLiteral("search anything"),
        QStringLiteral("try to search"),
        QStringLiteral("internet search"),
        QStringLiteral("browse internet"),
        QStringLiteral("browse the internet"),
        QStringLiteral("browse the web"),
        QStringLiteral("web search"),
        QStringLiteral("latest news"),
        QStringLiteral("latest model"),
        QStringLiteral("reach the web")
    });
}

bool isWebSearchVerificationQuery(const QString &input)
{
    const QString normalized = input.toLower();
    return (normalized.contains(QStringLiteral("search"))
            || normalized.contains(QStringLiteral("web"))
            || normalized.contains(QStringLiteral("internet")))
        && (normalized.contains(QStringLiteral("working"))
            || normalized.contains(QStringLiteral("work or not"))
            || normalized.contains(QStringLiteral("test"))
            || normalized.contains(QStringLiteral("try to search"))
            || normalized.contains(QStringLiteral("search for anything")));
}

QString defaultWebSearchProbeQuery()
{
    return QStringLiteral("latest AI news");
}

bool isLikelyKnowledgeLookupQuery(const QString &input)
{
    const QString normalized = input.trimmed().toLower();
    if (normalized.isEmpty()) {
        return false;
    }

    if (isVisionRelevantQuery(normalized)) {
        return false;
    }

    static const QRegularExpression startsWithQuestionWord(
        QStringLiteral("^(what|who|when|where|which|how many|how much|in which year|tell me)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (!startsWithQuestionWord.match(normalized).hasMatch()) {
        return false;
    }

    if (containsAnyNormalized(normalized, {
            QStringLiteral("open "),
            QStringLiteral("launch "),
            QStringLiteral("start "),
            QStringLiteral("create file"),
            QStringLiteral("read file"),
            QStringLiteral("show logs"),
            QStringLiteral("set timer")
        })) {
        return false;
    }

    return true;
}

bool isFreshnessSensitiveQuery(const QString &input)
{
    return containsAnyNormalized(input, {
        QStringLiteral("latest"),
        QStringLiteral("newest"),
        QStringLiteral("recent"),
        QStringLiteral("just released"),
        QStringLiteral("release"),
        QStringLiteral("today"),
        QStringLiteral("this week"),
        QStringLiteral("this month"),
        QStringLiteral("breaking"),
        QStringLiteral("news"),
        QStringLiteral("as of")
    });
}

QString freshnessCodeForQuery(const QString &input)
{
    const QString normalized = input.toLower();
    if (normalized.contains(QStringLiteral("today"))
        || normalized.contains(QStringLiteral("last 24"))
        || normalized.contains(QStringLiteral("past 24"))) {
        return QStringLiteral("pd");
    }
    if (normalized.contains(QStringLiteral("this week"))
        || normalized.contains(QStringLiteral("last week"))
        || normalized.contains(QStringLiteral("past week"))) {
        return QStringLiteral("pw");
    }
    if (normalized.contains(QStringLiteral("this month"))
        || normalized.contains(QStringLiteral("last month"))
        || normalized.contains(QStringLiteral("past month"))) {
        return QStringLiteral("pm");
    }
    if (normalized.contains(QStringLiteral("this year"))
        || normalized.contains(QStringLiteral("last year"))
        || normalized.contains(QStringLiteral("past year"))) {
        return QStringLiteral("py");
    }
    return QStringLiteral("pw");
}

bool asksForDetailedAnswer(const QString &input)
{
    return containsAnyNormalized(input, {
        QStringLiteral("details"),
        QStringLiteral("detailed"),
        QStringLiteral("explain"),
        QStringLiteral("why"),
        QStringLiteral("how"),
        QStringLiteral("breakdown"),
        QStringLiteral("compare"),
        QStringLiteral("sources"),
        QStringLiteral("list")
    });
}

QString extractWebSearchQuery(QString input)
{
    input = input.trimmed();
    input.remove(QRegularExpression(QStringLiteral("^(yeah|yes|okay|ok|please|vaxil|jarvis)\\s*,?\\s*"),
                                    QRegularExpression::CaseInsensitiveOption));
    input.remove(QRegularExpression(QStringLiteral("^(can you|could you|would you|please)\\s+"),
                                    QRegularExpression::CaseInsensitiveOption));
    input.remove(QRegularExpression(QStringLiteral("^(search|browse)\\s+(the\\s+)?(web|internet)\\s+(for|about|on)\\s+"),
                                    QRegularExpression::CaseInsensitiveOption));
    input.remove(QRegularExpression(QStringLiteral("^(search|browse)\\s+(the\\s+)?(web|internet)\\s*"),
                                    QRegularExpression::CaseInsensitiveOption));
    input.remove(QRegularExpression(QStringLiteral("^(search|find|look up)\\s+(for\\s+)?"),
                                    QRegularExpression::CaseInsensitiveOption));
    input.remove(QRegularExpression(QStringLiteral("^(what('?s| is)\\s+the\\s+latest\\s+model)"),
                                    QRegularExpression::CaseInsensitiveOption));
    input.remove(QRegularExpression(QStringLiteral("^(latest\\s+news\\s+(in|about)\\s+)"),
                                    QRegularExpression::CaseInsensitiveOption));
    input = input.trimmed();
    input.remove(QRegularExpression(QStringLiteral("^[\\s,.:;!?-]+|[\\s,.:;!?-]+$")));
    return input.trimmed();
}

QString mcpPackageManifestPath(const QString &mcpRootPath, const QString &packageName)
{
    if (mcpRootPath.isEmpty() || packageName.trimmed().isEmpty()) {
        return {};
    }

    const QStringList parts = packageName.split(QStringLiteral("/"), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return {};
    }

    if (packageName.startsWith(QStringLiteral("@")) && parts.size() >= 2) {
        return mcpRootPath + QStringLiteral("/node_modules/") + parts[0] + QStringLiteral("/") + parts[1] + QStringLiteral("/package.json");
    }

    return mcpRootPath + QStringLiteral("/node_modules/") + parts[0] + QStringLiteral("/package.json");
}

QString mcpPackageHealthLabel(const QString &mcpRootPath, const QString &packageName)
{
    QFile file(mcpPackageManifestPath(mcpRootPath, packageName));
    if (!file.exists()) {
        return QStringLiteral("Not installed");
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("Installed (unreadable)");
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return QStringLiteral("Installed (invalid manifest)");
    }

    const QJsonObject obj = doc.object();
    const QString version = obj.value(QStringLiteral("version")).toString();
    const QJsonValue binValue = obj.value(QStringLiteral("bin"));
    const bool runnable = (binValue.isString() && !binValue.toString().trimmed().isEmpty())
        || (binValue.isObject() && !binValue.toObject().isEmpty());

    if (runnable) {
        return version.isEmpty() ? QStringLiteral("Working") : QStringLiteral("Working (%1)").arg(version);
    }

    return version.isEmpty()
        ? QStringLiteral("Installed (entrypoint unknown)")
        : QStringLiteral("Installed (entrypoint unknown, %1)").arg(version);
}

QString runtimeToolStatusSummary(const AppSettings *settings)
{
    const bool npmAvailable = !QStandardPaths::findExecutable(QStringLiteral("npm")).isEmpty()
        || !QStandardPaths::findExecutable(QStringLiteral("npm.cmd")).isEmpty();

    const bool mcpEnabled = settings != nullptr && settings->mcpEnabled();
    const QString mcpServer = settings != nullptr ? settings->mcpServerUrl().trimmed() : QString();
    const QString mcpCatalog = settings != nullptr ? settings->mcpCatalogUrl().trimmed() : QString();

    QStringList lines;
    lines.push_back(QStringLiteral("mcp_runtime=disabled"));
    lines.push_back(QStringLiteral("mcp_configured=%1").arg(mcpEnabled ? QStringLiteral("true") : QStringLiteral("false")));
    lines.push_back(QStringLiteral("npm_available=%1").arg(npmAvailable ? QStringLiteral("true") : QStringLiteral("false")));
    lines.push_back(QStringLiteral("mcp_server=%1").arg(mcpServer.isEmpty() ? QStringLiteral("unset") : mcpServer));
    lines.push_back(QStringLiteral("mcp_catalog=%1").arg(mcpCatalog.isEmpty() ? QStringLiteral("unset") : mcpCatalog));
    return lines.join(QStringLiteral("; "));
}

MemoryRecord runtimeToolStatusMemory(const AppSettings *settings)
{
    MemoryRecord record;
    record.type = QStringLiteral("runtime");
    record.key = QStringLiteral("tool_status");
    record.value = runtimeToolStatusSummary(settings);
    record.confidence = 1.0f;
    record.source = QStringLiteral("local_runtime");
    record.updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return record;
}

QString configuredAgentProviderMode(const AppSettings *settings)
{
    const QString configured = settings != nullptr
        ? settings->agentProviderMode().trimmed().toLower()
        : QStringLiteral("auto");
    if (configured == QStringLiteral("responses") || configured == QStringLiteral("chat_adapter")) {
        return configured;
    }
    return QStringLiteral("auto");
}

QString effectiveAgentProviderModeText(const AppSettings *settings,
                                       const AgentCapabilitySet &capabilities,
                                       const QString &modelId,
                                       const AiRequestCoordinator *coordinator)
{
    const QString configured = configuredAgentProviderMode(settings);
    if (configured == QStringLiteral("responses") || configured == QStringLiteral("chat_adapter")) {
        return configured;
    }
    if (coordinator != nullptr
        && coordinator->resolveAgentTransport(capabilities, modelId) == AgentTransportMode::Responses) {
        return QStringLiteral("responses");
    }
    return QStringLiteral("chat_adapter");
}

QString agentCapabilityStatusText(const AppSettings *settings,
                                  const AgentCapabilitySet &capabilities,
                                  const QString &modelId,
                                  const AiRequestCoordinator *coordinator)
{
    if (settings != nullptr && !settings->agentEnabled()) {
        return QStringLiteral("Agent disabled");
    }
    if (coordinator == nullptr) {
        return capabilities.status;
    }

    switch (coordinator->resolveAgentTransport(capabilities, modelId)) {
    case AgentTransportMode::Responses:
        return QStringLiteral("Responses tool-calling ready");
    case AgentTransportMode::ChatAdapter:
        return QStringLiteral("Chat adapter fallback ready");
    case AgentTransportMode::CapabilityError:
        return coordinator->capabilityErrorText(capabilities, modelId);
    }

    return capabilities.status;
}

QString groundedToolInventoryText(const QList<AgentToolSpec> &tools, const AppSettings *settings)
{
    QStringList names;
    for (const auto &tool : tools) {
        if (!tool.name.isEmpty()) {
            names.push_back(tool.name);
        }
    }
    names.removeDuplicates();
    return QStringLiteral("I can use these tools right now: %1. File reads can access readable paths on this PC. File writes stay sandboxed to the app roots. Runtime capability status: %2")
        .arg(names.join(QStringLiteral(", ")), runtimeToolStatusSummary(settings));
}

int effectiveRequestTimeoutMs(const AppSettings *settings)
{
    return std::max(30000, settings != nullptr ? settings->requestTimeoutMs() : 30000);
}

IntentType expectedAgentIntentForQuery(const QString &input)
{
    if (containsAnyNormalized(input, {
            QStringLiteral("read your own logs"),
            QStringLiteral("read logs"),
            QStringLiteral("startup log"),
            QStringLiteral("vaxil log"),
            QStringLiteral("jarvis log")
        })) {
        return IntentType::READ_FILE;
    }

    return IntentType::GENERAL_CHAT;
}

}

AssistantController::AssistantController(
    AppSettings *settings,
    IdentityProfileService *identityProfileService,
    LoggingService *loggingService,
    QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_identityProfileService(identityProfileService)
    , m_loggingService(loggingService)
{
    m_voicePipelineRuntime = new VoicePipelineRuntime(m_settings, m_loggingService, this);
    m_aiBackendClient = new RuntimeAiBackendClient(m_voicePipelineRuntime, this);
    m_modelCatalogService = new ModelCatalogService(m_settings, m_aiBackendClient, this);
    m_reasoningRouter = new ReasoningRouter(this);
    m_promptAdapter = new PromptAdapter(this);
    m_streamAssembler = new StreamAssembler(this);
    m_memoryStore = new MemoryStore(this);
    m_inputRouter = std::make_unique<InputRouter>();
    m_aiRequestCoordinator = std::make_unique<AiRequestCoordinator>(m_settings, m_reasoningRouter);
    m_memoryPolicyHandler = std::make_unique<MemoryPolicyHandler>(m_identityProfileService, m_memoryStore);
    m_skillStore = new SkillStore(this);
    m_agentToolbox = new AgentToolbox(m_settings, m_memoryStore, m_skillStore, m_loggingService, this);
    m_deviceManager = new DeviceManager(this);
    m_intentEngine = new IntentEngine(m_settings, m_loggingService, this);
    m_backgroundIntentDetector = new IntentDetector(this);
    m_intentRouter = new IntentRouter(this);
    m_localResponseEngine = new LocalResponseEngine(this);
    m_taskDispatcher = new TaskDispatcher(m_loggingService, this);
    m_toolWorker = new ToolWorker(backgroundAllowedRoots(), m_loggingService, m_settings);
    m_whisperSttEngine = new RuntimeSpeechRecognizer(m_voicePipelineRuntime, this);
    m_ttsEngine = new WorkerTtsEngine(m_voicePipelineRuntime, this);
    m_responseFinalizer = std::make_unique<ResponseFinalizer>(m_memoryStore, m_ttsEngine, m_loggingService);
    m_worldStateCache = new WorldStateCache(15000, 2000, this);
    m_visionIngestService = new VisionIngestService(m_settings, m_loggingService, this);
    m_gestureInterpreter = new GestureInterpreter(this);
    m_gestureStateMachine = new GestureStateMachine(m_loggingService);
    m_gestureActionRouter = new GestureActionRouter(m_loggingService);
    m_toolWorkerThread.setObjectName(QStringLiteral("BackgroundToolWorkerThread"));
    m_gestureActionRouterThread.setObjectName(QStringLiteral("GestureActionRouterThread"));
    m_toolWorker->moveToThread(&m_toolWorkerThread);
    m_gestureStateMachine->moveToThread(&m_gestureActionRouterThread);
    m_gestureActionRouter->moveToThread(&m_gestureActionRouterThread);
    connect(&m_toolWorkerThread, &QThread::finished, m_toolWorker, &QObject::deleteLater);
    connect(&m_gestureActionRouterThread, &QThread::finished, m_gestureStateMachine, &QObject::deleteLater);
    connect(&m_gestureActionRouterThread, &QThread::finished, m_gestureActionRouter, &QObject::deleteLater);
    connect(m_taskDispatcher, &TaskDispatcher::taskReady, m_toolWorker, &ToolWorker::processTask, Qt::QueuedConnection);
    connect(m_taskDispatcher, &TaskDispatcher::taskCanceled, m_toolWorker, &ToolWorker::cancelTask, Qt::QueuedConnection);
    connect(m_taskDispatcher, &TaskDispatcher::taskCanceled, this, [this](int taskId) {
        m_knownBackgroundTasks.remove(taskId);

        for (auto it = m_activeBackgroundTaskIds.begin(); it != m_activeBackgroundTaskIds.end();) {
            if (it.value() == taskId) {
                it = m_activeBackgroundTaskIds.erase(it);
            } else {
                ++it;
            }
        }

        refreshBackgroundTaskSurface();
    });
    connect(m_taskDispatcher, &TaskDispatcher::activeTaskChanged, this, [this](const QString &type, int taskId) {
        m_activeBackgroundTaskIds.insert(type, taskId);
        if (m_knownBackgroundTasks.contains(taskId)) {
            AgentTask task = m_knownBackgroundTasks.value(taskId);
            task.state = TaskState::Running;
            m_knownBackgroundTasks.insert(taskId, task);
        }
        refreshBackgroundTaskSurface();
    });
    connect(m_toolWorker, &ToolWorker::taskStarted, m_taskDispatcher, &TaskDispatcher::handleTaskStarted, Qt::QueuedConnection);
    connect(m_toolWorker, &ToolWorker::taskFinished, m_taskDispatcher, &TaskDispatcher::handleTaskFinished, Qt::QueuedConnection);
    connect(m_taskDispatcher, &TaskDispatcher::taskResultReady, this, [this](const QJsonObject &resultObject) {
        recordTaskResult(resultObject);
    }, Qt::QueuedConnection);
    connect(m_visionIngestService, &VisionIngestService::visionSnapshotReceived, this, &AssistantController::handleVisionSnapshot, Qt::QueuedConnection);
    connect(m_visionIngestService, &VisionIngestService::statusChanged, this, [this](const QString &status, bool connected) {
        if (!m_settings->visionEnabled() || status.compare(QStringLiteral("Vision ingest disabled"), Qt::CaseInsensitive) == 0) {
            clearSurfaceError(QStringLiteral("vision"));
            return;
        }

        if (connected) {
            clearSurfaceError(QStringLiteral("vision"));
            return;
        }

        setSurfaceError(QStringLiteral("vision"), compactSurfaceText(status));
    }, Qt::QueuedConnection);
    connect(m_gestureInterpreter, &GestureInterpreter::observationsInterpreted, m_gestureStateMachine, &GestureStateMachine::ingestObservations, Qt::QueuedConnection);
    connect(m_gestureStateMachine, &GestureStateMachine::gestureEventReady, m_gestureActionRouter, &GestureActionRouter::routeGestureEvent, Qt::QueuedConnection);
    connect(m_gestureActionRouter, &GestureActionRouter::gestureTriggered, this, [this](const QString &gestureName, qint64 timestampMs) {
        m_lastVisionGestureTriggerMs = timestampMs;
        m_lastVisionGestureAction = gestureName;
    }, Qt::QueuedConnection);
    connect(m_gestureActionRouter, &GestureActionRouter::stopSpeakingRequested, this, &AssistantController::stopSpeaking, Qt::QueuedConnection);
    connect(m_gestureActionRouter, &GestureActionRouter::cancelCurrentRequestRequested, this, &AssistantController::cancelCurrentRequest, Qt::QueuedConnection);
    connect(m_gestureActionRouter, &GestureActionRouter::farewellRequested, this, &AssistantController::handleGestureFarewell, Qt::QueuedConnection);
    connect(m_gestureActionRouter, &GestureActionRouter::confirmRequested, this, &AssistantController::handleGestureConfirm, Qt::QueuedConnection);
    connect(m_gestureActionRouter, &GestureActionRouter::rejectRequested, this, &AssistantController::handleGestureReject, Qt::QueuedConnection);
    connect(m_settings, &AppSettings::settingsChanged, this, &AssistantController::reconfigureGestureActionRouter);
    createWakeWordEngine();
}

AssistantController::~AssistantController()
{
    if (m_gestureActionRouterThread.isRunning()) {
        m_gestureActionRouterThread.quit();
        m_gestureActionRouterThread.wait();
    } else if (m_gestureStateMachine != nullptr || m_gestureActionRouter != nullptr) {
        if (m_gestureStateMachine != nullptr) {
            m_gestureStateMachine->deleteLater();
        }
        if (m_gestureActionRouter != nullptr) {
            m_gestureActionRouter->deleteLater();
        }
    }
    if (m_toolWorkerThread.isRunning()) {
        m_toolWorkerThread.quit();
        m_toolWorkerThread.wait();
    }
}

void AssistantController::initialize()
{
    m_statusText = QStringLiteral("Loading services...");
    if (!m_toolWorkerThread.isRunning()) {
        m_toolWorkerThread.start();
    }
    if (!m_gestureActionRouterThread.isRunning()) {
        m_gestureActionRouterThread.start();
    }
    reconfigureGestureActionRouter();
    if (m_visionIngestService) {
        m_visionIngestService->start();
    }
    m_voicePipelineRuntime->start();
    m_aiBackendClient->setProviderConfig(m_settings->chatBackendKind(), m_settings->chatBackendApiKey());
    m_aiBackendClient->setEndpoint(m_settings->chatBackendEndpoint());
    m_deviceManager->registerDefaults();
    m_localResponseEngine->initialize();
    setupStateMachine();
    refreshModels();

    connect(m_modelCatalogService, &ModelCatalogService::modelsChanged, this, &AssistantController::modelsChanged);
    connect(m_modelCatalogService, &ModelCatalogService::modelsChanged, this, [this]() {
        m_modelCatalogResolved = true;
        const QString modelId = selectedModel().isEmpty() && !availableModelIds().isEmpty() ? availableModelIds().first() : selectedModel();
        const QString lowered = modelId.toLower();
        m_agentCapabilities.selectedModelToolCapable = lowered.contains(QStringLiteral("qwen"))
            || lowered.contains(QStringLiteral("granite"))
            || lowered.contains(QStringLiteral("llama"))
            || lowered.contains(QStringLiteral("gpt-oss"))
            || lowered.contains(QStringLiteral("tool"));
        m_agentCapabilities.agentEnabled = m_settings->agentEnabled();
        m_agentCapabilities.providerMode = effectiveAgentProviderModeText(m_settings,
                                                                          m_agentCapabilities,
                                                                          modelId,
                                                                          m_aiRequestCoordinator.get());
        m_agentCapabilities.status = agentCapabilityStatusText(m_settings,
                                                               m_agentCapabilities,
                                                               modelId,
                                                               m_aiRequestCoordinator.get());
        emit agentStateChanged();
        updateStartupState();
    });
    connect(m_modelCatalogService, &ModelCatalogService::availabilityChanged, this, [this]() {
        if (!m_modelCatalogService->availability().online) {
            m_modelCatalogResolved = true;
        }
        setStatus(m_modelCatalogService->availability().status);
        updateStartupState();
    });

    m_voicePipelineRuntime->configureAudioProcessing({
        .aecEnabled = m_settings->aecEnabled(),
        .noiseSuppressionEnabled = true,
        .agcEnabled = true,
        .rnnoiseEnabled = m_settings->rnnoiseEnabled(),
        .vadSensitivity = static_cast<float>(m_settings->vadSensitivity())
    });

    connect(m_voicePipelineRuntime, &VoicePipelineRuntime::inputAudioLevelChanged, this, [this](quint64 generationId, const AudioLevel &level) {
        if (generationId != m_activeInputCaptureId) {
            return;
        }
        m_audioLevel = level.rms;
        emit audioLevelChanged();
    });
    connect(m_voicePipelineRuntime, &VoicePipelineRuntime::speechActivityChanged, this, [this](quint64 generationId, bool active) {
        if (generationId != m_activeInputCaptureId || !active || isMicrophoneBlocked()) {
            return;
        }

        if (m_loggingService) {
            const QString mode = m_audioCaptureMode == AudioCaptureMode::Direct
                    ? QStringLiteral("direct")
                    : (m_audioCaptureMode == AudioCaptureMode::WakeMonitor
                        ? QStringLiteral("wake")
                        : QStringLiteral("none"));
            m_loggingService->info(QStringLiteral("Audio speech detected. mode=%1").arg(mode));
        }
    });
    connect(m_voicePipelineRuntime, &VoicePipelineRuntime::speechFrame, this, [this](quint64 generationId, const AudioFrame &frame) {
        if (generationId != m_activeInputCaptureId || m_audioCaptureMode != AudioCaptureMode::WakeMonitor) {
            return;
        }

        if (m_wakeWordEngine && m_wakeWordEngine->isActive() && m_wakeWordEngine->usesExternalAudioInput()) {
            m_wakeWordEngine->processAudioFrame(frame);
        }
    });
    connect(m_voicePipelineRuntime, &VoicePipelineRuntime::inputCaptureFinished, this, [this](quint64 generationId, const QByteArray &pcmData, bool hadSpeech) {
        if (generationId != m_activeInputCaptureId) {
            return;
        }

        const AudioCaptureMode completedMode = m_audioCaptureMode;
        m_lastCompletedCaptureMode = completedMode;
        m_audioCaptureMode = AudioCaptureMode::None;

        if (m_loggingService) {
            const QString mode = completedMode == AudioCaptureMode::Direct
                    ? QStringLiteral("direct")
                    : (completedMode == AudioCaptureMode::WakeMonitor
                        ? QStringLiteral("wake")
                        : QStringLiteral("none"));
            m_loggingService->info(QStringLiteral("Audio capture finished. mode=%1 bytes=%2 hadSpeech=%3")
                .arg(mode)
                .arg(pcmData.size())
                .arg(hadSpeech ? QStringLiteral("true") : QStringLiteral("false")));
        }

        if (isMicrophoneBlocked()) {
            clearActiveSpeechCapture();
            return;
        }

        if (!hadSpeech || pcmData.isEmpty()) {
            handleConversationSessionMiss(QStringLiteral("No speech detected"));
            return;
        }

        m_consecutiveSessionMisses = 0;
        refreshConversationSession();
        emit processingRequested();
        m_activeSttRequestId = m_whisperSttEngine->transcribePcm(pcmData, buildSttPrompt(), true);
    });
    connect(m_voicePipelineRuntime, &VoicePipelineRuntime::inputCaptureFailed, this, [this](quint64 generationId, const QString &errorText) {
        if (generationId != m_activeInputCaptureId) {
            return;
        }

        const AudioCaptureMode failedMode = m_audioCaptureMode;
        m_audioCaptureMode = AudioCaptureMode::None;
        if (m_loggingService) {
            m_loggingService->error(QStringLiteral("Input capture failed: %1").arg(errorText));
        }
        if (failedMode == AudioCaptureMode::WakeMonitor && m_wakeWordEngine->isActive()) {
            m_wakeWordEngine->stop();
        }
        if (m_conversationSessionActive && failedMode == AudioCaptureMode::Direct) {
            handleConversationSessionMiss(errorText);
            return;
        }
        setSurfaceError(QStringLiteral("assistant"), compactSurfaceText(errorText));
        setStatus(errorText);
        resumeWakeMonitor(shortWakeResumeDelayMs());
        emit idleRequested();
    });

    bindWakeWordEngineSignals();

    connect(m_whisperSttEngine, &SpeechRecognizer::transcriptionReady, this, [this](quint64 requestId, const TranscriptionResult &result) {
        if (requestId != m_activeSttRequestId || isMicrophoneBlocked()) {
            return;
        }
        const QString transcript = result.text.trimmed();
        m_transcript = transcript;
        emit transcriptChanged();
        if (transcript.isEmpty() || isLikelyNonSpeechTranscript(transcript)) {
            if (m_loggingService && isLikelyNonSpeechTranscript(transcript)) {
                m_loggingService->info(QStringLiteral("Ignoring non-speech transcription token. text=\"%1\"").arg(transcript.left(120)));
            }
            handleConversationSessionMiss(QStringLiteral("No speech detected"));
            return;
        }
        if (isLikelySttArtifactTranscript(transcript)) {
            if (m_loggingService) {
                m_loggingService->infoFor(QStringLiteral("stt"), QStringLiteral("Ignoring STT artifact transcription. text=\"%1\"").arg(transcript.left(120)));
            }
            handleConversationSessionMiss(QStringLiteral("No speech detected"));
            return;
        }
        if (shouldIgnoreAmbiguousTranscript(transcript)) {
            if (m_loggingService) {
                m_loggingService->info(QStringLiteral("Ignoring ambiguous transcription. text=\"%1\"").arg(transcript.left(120)));
            }
            if (m_conversationSessionActive) {
                ++m_consecutiveSessionMisses;
                refreshConversationSession();
            }
            deliverLocalResponse(QStringLiteral("I didn't catch that."), QStringLiteral("Please repeat"), true);
            return;
        }

        m_consecutiveSessionMisses = 0;
        refreshConversationSession();
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("Transcription ready. mode=direct text=\"%1\"")
                .arg(transcript.left(240)));
        }
        submitText(transcript);
    });
    connect(m_whisperSttEngine, &SpeechRecognizer::transcriptionFailed, this, [this](quint64 requestId, const QString &errorText) {
        if (requestId != m_activeSttRequestId) {
            return;
        }
        if (m_loggingService) {
            m_loggingService->error(QStringLiteral("Speech transcription failed: %1").arg(errorText));
        }
        if (m_conversationSessionActive) {
            handleConversationSessionMiss(errorText);
            return;
        }
        setSurfaceError(QStringLiteral("assistant"), compactSurfaceText(errorText));
        setStatus(errorText);
        resumeWakeMonitor(shortWakeResumeDelayMs());
        emit idleRequested();
    });

    connect(m_streamAssembler, &StreamAssembler::partialTextUpdated, this, [this](const QString &text) {
        m_responseText = sanitizeDisplayText(text);
        emit responseTextChanged();
    });

    connect(m_ttsEngine, &TtsEngine::playbackStarted, this, [this]() {
        if (m_loggingService) {
            m_loggingService->infoFor(QStringLiteral("tts"), QStringLiteral("TTS playback started."));
        }
        clearSurfaceError(QStringLiteral("assistant"));
        beginTtsExclusiveMode();
        emit speakingRequested();
    });
    connect(m_ttsEngine, &TtsEngine::playbackFinished, this, [this]() {
        if (m_loggingService) {
            m_loggingService->infoFor(QStringLiteral("tts"), QStringLiteral("TTS playback finished."));
        }
        enterPostSpeechCooldown();
        if (m_followUpListeningAfterWakeAck || conversationSessionShouldContinue()) {
            refreshConversationSession();
            const int restartDelayMs = m_followUpListeningAfterWakeAck
                ? followUpListeningDelayMs()
                : conversationSessionRestartDelayMs();
            if (!scheduleConversationSessionListening(restartDelayMs)) {
                endConversationSession();
                enterPostSpeechCooldown();
                resumeWakeMonitor(shortWakeResumeDelayMs());
                emit idleRequested();
            }
            return;
        }
        endConversationSession();
        resumeWakeMonitor(postSpeechWakeEngineStartDelayMs());
        emit idleRequested();
    });
    connect(m_ttsEngine, &TtsEngine::playbackFailed, this, [this](const QString &errorText) {
        enterPostSpeechCooldown();
        setSurfaceError(QStringLiteral("assistant"), compactSurfaceText(errorText));
        setStatus(errorText);
        endConversationSession();
        resumeWakeMonitor(shortWakeResumeDelayMs());
        emit idleRequested();
    });

    connect(m_aiBackendClient, &AiBackendClient::requestStarted, this, [this](quint64 requestId) {
        m_activeRequestId = requestId;
        clearSurfaceError(QStringLiteral("assistant"));
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("Local AI backend request started. requestId=%1 kind=%2")
                .arg(requestId)
                .arg(m_activeRequestKind == RequestKind::CommandExtraction
                         ? QStringLiteral("command")
                         : (m_activeRequestKind == RequestKind::AgentConversation
                                ? QStringLiteral("agent")
                                : QStringLiteral("conversation"))));
        }
        setDuplexState(DuplexState::Processing);
        emit processingRequested();
    });
    connect(m_aiBackendClient, &AiBackendClient::requestDelta, this, [this](quint64 requestId, const QString &delta) {
        if (requestId == m_activeRequestId && m_activeRequestKind == RequestKind::Conversation) {
            m_streamAssembler->appendChunk(delta);
        }
    });
    connect(m_aiBackendClient, &AiBackendClient::capabilitiesChanged, this, [this](const AgentCapabilitySet &capabilities) {
        m_agentCapabilities = capabilities;
        m_agentCapabilities.agentEnabled = m_settings->agentEnabled();
        m_agentCapabilities.providerMode = effectiveAgentProviderModeText(m_settings,
                                                                          m_agentCapabilities,
                                                                          selectedModel(),
                                                                          m_aiRequestCoordinator.get());
        m_agentCapabilities.status = agentCapabilityStatusText(m_settings,
                                                               m_agentCapabilities,
                                                               selectedModel(),
                                                               m_aiRequestCoordinator.get());
        emit agentStateChanged();
    });
    connect(m_aiBackendClient, &AiBackendClient::requestFinished, this, [this](quint64 requestId, const QString &fullText) {
        if (requestId != m_activeRequestId) {
            return;
        }

        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("Local AI backend request finished. requestId=%1 chars=%2")
                .arg(requestId)
                .arg(fullText.size()));
        }

        if (m_activeRequestKind == RequestKind::CommandExtraction) {
            handleCommandFinished(fullText);
        } else if (m_activeRequestKind == RequestKind::AgentConversation) {
            handleHybridAgentFinished(fullText);
        } else {
            handleConversationFinished(fullText);
        }
    });
    connect(m_aiBackendClient, &AiBackendClient::agentResponseReady, this, [this](quint64 requestId, const AgentResponse &response) {
        if (requestId != m_activeRequestId) {
            return;
        }
        handleAgentResponse(response);
    });
    connect(m_aiBackendClient, &AiBackendClient::requestFailed, this, [this](quint64 requestId, const QString &errorText) {
        if (requestId == m_activeRequestId) {
            if (m_loggingService) {
                m_loggingService->error(QStringLiteral("Local AI backend request failed. requestId=%1 error=\"%2\"")
                    .arg(QString::number(requestId), errorText));
            }
            const QString errorGroup = m_aiRequestCoordinator->errorGroupFor(errorText);
            refreshConversationSession();
            setSurfaceError(QStringLiteral("assistant"), compactSurfaceText(errorText));
            deliverLocalResponse(
                m_localResponseEngine->respondToError(errorGroup, buildLocalResponseContext()),
                errorText,
                true);
        }
    });

    if (m_settings->initialSetupCompleted()) {
        startWakeMonitor();
    }
    updateStartupState();
}

QString AssistantController::stateName() const { return stateToString(m_currentState); }
QString AssistantController::transcript() const { return m_transcript; }
QString AssistantController::responseText() const { return m_responseText; }
QString AssistantController::statusText() const { return m_statusText; }
float AssistantController::audioLevel() const { return m_audioLevel; }
int AssistantController::wakeTriggerToken() const { return m_wakeTriggerToken; }
AssistantSurfaceState AssistantController::assistantSurfaceState() const
{
    if (!m_surfaceErrorPrimary.trimmed().isEmpty()) {
        return AssistantSurfaceState::Error;
    }
    if (m_surfaceBackgroundTaskId >= 0 && !m_surfaceBackgroundPrimary.trimmed().isEmpty()) {
        return AssistantSurfaceState::ToolRunning;
    }

    switch (m_currentState) {
    case AssistantState::Listening:
        return AssistantSurfaceState::Listening;
    case AssistantState::Processing:
        return AssistantSurfaceState::Thinking;
    case AssistantState::Speaking:
        return AssistantSurfaceState::Speaking;
    case AssistantState::Idle:
    default:
        return AssistantSurfaceState::Ready;
    }
}

QString AssistantController::assistantSurfaceActivityPrimary() const
{
    if (!m_surfaceErrorPrimary.trimmed().isEmpty()) {
        return m_surfaceErrorPrimary;
    }
    return m_surfaceBackgroundPrimary;
}

QString AssistantController::assistantSurfaceActivitySecondary() const
{
    if (!m_surfaceErrorPrimary.trimmed().isEmpty()) {
        return m_surfaceErrorSecondary;
    }
    return m_surfaceBackgroundSecondary;
}
bool AssistantController::startupReady() const { return m_startupReady; }
bool AssistantController::startupBlocked() const { return m_startupBlocked; }
QString AssistantController::startupBlockingIssue() const { return m_startupBlockingIssue; }
QList<ModelInfo> AssistantController::availableModels() const { return m_modelCatalogService->models(); }
QStringList AssistantController::availableModelIds() const
{
    QStringList ids;
    for (const auto &model : availableModels()) {
        ids.push_back(model.id);
    }
    return ids;
}
QString AssistantController::selectedModel() const { return m_settings->chatBackendModel(); }
QList<SkillManifest> AssistantController::installedSkills() const { return m_skillStore->listSkills(); }
QList<AgentToolSpec> AssistantController::availableAgentTools() const { return m_agentToolbox->builtInTools(); }
AgentCapabilitySet AssistantController::agentCapabilities() const { return m_agentCapabilities; }
QList<AgentTraceEntry> AssistantController::agentTrace() const { return m_agentTrace; }
SamplingProfile AssistantController::samplingProfile() const
{
    return {
        .conversationTemperature = m_settings->conversationTemperature(),
        .conversationTopP = m_settings->conversationTopP(),
        .toolUseTemperature = m_settings->toolUseTemperature(),
        .providerTopK = m_settings->providerTopK(),
        .maxOutputTokens = m_settings->maxOutputTokens()
    };
}
QList<BackgroundTaskResult> AssistantController::backgroundTaskResults() const { return m_backgroundTaskResults; }
bool AssistantController::backgroundPanelVisible() const { return m_backgroundPanelVisible; }
QString AssistantController::latestTaskToast() const { return m_latestTaskToast; }
QString AssistantController::latestTaskToastTone() const { return m_latestTaskToastTone; }
int AssistantController::latestTaskToastTaskId() const { return m_latestTaskToastTaskId; }
QString AssistantController::latestTaskToastType() const { return m_latestTaskToastType; }
bool AssistantController::installSkill(const QString &url, QString *error)
{
    const bool ok = m_skillStore->installSkill(url, error);
    appendAgentTrace(QStringLiteral("skill"), QStringLiteral("Install skill"), url, ok);
    return ok;
}

bool AssistantController::createSkill(const QString &id, const QString &name, const QString &description, QString *error)
{
    const bool ok = m_skillStore->createSkill(id, name, description, error);
    appendAgentTrace(QStringLiteral("skill"), QStringLiteral("Create skill"), id, ok);
    return ok;
}

void AssistantController::refreshModels()
{
    m_modelCatalogResolved = false;
    updateStartupState();
    m_aiBackendClient->setProviderConfig(m_settings->chatBackendKind(), m_settings->chatBackendApiKey());
    m_aiBackendClient->setEndpoint(m_settings->chatBackendEndpoint());
    m_modelCatalogService->refresh();
}

void AssistantController::submitText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    clearSurfaceError(QStringLiteral("assistant"));
    m_lastPromptForAiLog = trimmed;
    invalidateWakeMonitorResume();

    const bool wakeDetected = WakeWordDetector::isWakeWordDetected(trimmed);
    QString routedInput = wakeDetected
        ? normalizeForRouting(WakeWordDetector::stripWakeWordPrefix(trimmed))
        : trimmed;
    const QString effectiveInput = routedInput.isEmpty() ? trimmed : routedInput;

    if (wakeDetected) {
        noteWakeTrigger();
        activateConversationSession();
    } else if (m_conversationSessionActive) {
        refreshConversationSession();
    }

    m_transcript = trimmed;
    m_responseText.clear();
    m_streamAssembler->reset();
    m_ttsEngine->clear();
    invalidateActiveTranscription();
    emit transcriptChanged();
    emit responseTextChanged();
    setDuplexState(DuplexState::Processing);
    setStatus(QStringLiteral("Processing request"));
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("[VAXIL] Processing... raw=\"%1\" wakeDetected=%2 routed=\"%3\"")
            .arg(trimmed.left(240))
            .arg(wakeDetected ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(routedInput.left(240)));
    }
    m_memoryPolicyHandler->applyUserInput(effectiveInput);
    m_memoryStore->appendConversation(QStringLiteral("user"), trimmed);

    if (wakeDetected && routedInput.isEmpty()) {
        m_followUpListeningAfterWakeAck = true;
        deliverLocalResponse(
            m_localResponseEngine->wakeWordReady(buildLocalResponseContext()),
            QStringLiteral("Listening"),
            false);
        return;
    }

    if (shouldEndConversationSession(effectiveInput)) {
        endConversationSession();
        deliverLocalResponse(QStringLiteral("Standing by."), QStringLiteral("Conversation ended"), true);
        return;
    }

    if (isCurrentTimeQuery(routedInput)) {
        deliverLocalResponse(
            m_localResponseEngine->currentTimeResponse(buildLocalResponseContext()),
            QStringLiteral("Local time response"),
            true);
        return;
    }

    if (isCurrentDateQuery(routedInput)) {
        deliverLocalResponse(
            m_localResponseEngine->currentDateResponse(buildLocalResponseContext()),
            QStringLiteral("Local date response"),
            true);
        return;
    }

    AgentTask deterministicTask;
    QString deterministicSpoken;
    if (buildDeterministicComputerTask(routedInput, &deterministicTask, &deterministicSpoken)) {
        dispatchBackgroundTasks({deterministicTask});
        deliverLocalResponse(
            deterministicSpoken,
            QStringLiteral("Background task queued"),
            true);
        return;
    }

    const IntentResult mlIntent = m_intentEngine->classify(routedInput);
    const IntentResult detectedIntent = m_backgroundIntentDetector->detect(routedInput, QDir::currentPath());
    IntentResult effectiveIntent = m_intentEngine->isReady() ? mlIntent : detectedIntent;
    if (detectedIntent.confidence > effectiveIntent.confidence) {
        effectiveIntent = detectedIntent;
    }

    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Intent routing. mlType=%1 mlConfidence=%2 extractedType=%3 extractedConfidence=%4 onnxReady=%5")
            .arg(static_cast<int>(mlIntent.type))
            .arg(mlIntent.confidence, 0, 'f', 2)
            .arg(static_cast<int>(detectedIntent.type))
            .arg(detectedIntent.confidence, 0, 'f', 2)
            .arg(m_intentEngine->isReady() ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (m_settings->agentEnabled() && isExplicitComputerControlQuery(routedInput)) {
        startAgentConversationRequest(routedInput, IntentType::GENERAL_CHAT);
        return;
    }

    if (detectedIntent.confidence > 0.8f && !detectedIntent.tasks.isEmpty()) {
        dispatchBackgroundTasks(detectedIntent.tasks);
        deliverLocalResponse(
            detectedIntent.spokenMessage,
            QStringLiteral("Background task queued"),
            true);
        return;
    }

    const LocalIntent intent = m_intentRouter->classify(routedInput);
    const AiAvailability availability = m_modelCatalogService->availability();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool recentVisionQuery = (nowMs - m_lastVisionQueryMs) <= 12000;
    const bool visionRelevantQuery = isVisionRelevantQuery(routedInput)
        || (recentVisionQuery && isVisionFollowUpQuery(routedInput));

    if (intent == LocalIntent::Greeting || intent == LocalIntent::SmallTalk) {
        deliverLocalResponse(
            m_localResponseEngine->respondToIntent(intent, buildLocalResponseContext()),
            QStringLiteral("Local response"),
            true);
        return;
    }

    if (visionRelevantQuery && !isExplicitComputerControlQuery(routedInput)) {
        m_lastVisionQueryMs = nowMs;
        const QString directVisionResponse = buildDirectVisionResponse(routedInput);
        if (!directVisionResponse.isEmpty()) {
            deliverLocalResponse(directVisionResponse, QStringLiteral("Vision response"), true);
            return;
        }
    }

    if (!availability.online || !availability.modelAvailable) {
        deliverLocalResponse(
            m_localResponseEngine->respondToError(QStringLiteral("ai_offline"), buildLocalResponseContext()),
            QStringLiteral("AI unavailable"),
            true);
        return;
    }

    if (isExplicitToolInventoryQuery(routedInput)) {
        deliverLocalResponse(
            groundedToolInventoryText(m_agentToolbox->builtInTools(), m_settings),
            QStringLiteral("Tool inventory"),
            true);
        return;
    }

    if (!visionRelevantQuery && isExplicitWebSearchQuery(routedInput)) {
        QString extractedQuery = extractWebSearchQuery(routedInput);
        if (isWebSearchVerificationQuery(routedInput)) {
            extractedQuery = defaultWebSearchProbeQuery();
        }
        if (extractedQuery.isEmpty()) {
            extractedQuery = routedInput;
        }

        AgentTask task;
        task.type = QStringLiteral("web_search");
        task.args = QJsonObject{{QStringLiteral("query"), extractedQuery}};
        task.priority = 85;
        dispatchBackgroundTasks({task});
        deliverLocalResponse(
            QStringLiteral("All right, I'm searching the web now. The result will show up in the panel."),
            QStringLiteral("Background task queued"),
            true);
        return;
    }

    if (!visionRelevantQuery && m_settings->agentEnabled() && isLikelyKnowledgeLookupQuery(routedInput)) {
        AgentTask task;
        task.type = QStringLiteral("web_search");
        task.args = QJsonObject{{QStringLiteral("query"), routedInput}};
        task.priority = 83;
        dispatchBackgroundTasks({task});
        deliverLocalResponse(
            QStringLiteral("I'll verify that on the web and summarize what I find."),
            QStringLiteral("Background task queued"),
            true);
        return;
    }

    if (!visionRelevantQuery && m_settings->agentEnabled() && isFreshnessSensitiveQuery(routedInput)) {
        AgentTask task;
        task.type = QStringLiteral("web_search");
        task.args = QJsonObject{
            {QStringLiteral("query"), routedInput},
            {QStringLiteral("freshness"), freshnessCodeForQuery(routedInput)},
            {QStringLiteral("prefer_fresh"), true}
        };
        task.priority = 84;
        dispatchBackgroundTasks({task});
        deliverLocalResponse(
            QStringLiteral("All right, I'll check the web for the latest information and summarize it for you next."),
            QStringLiteral("Background task queued"),
            true);
        return;
    }

    if (m_settings->agentEnabled() && isExplicitAgentWorldQuery(routedInput)) {
        startAgentConversationRequest(routedInput, expectedAgentIntentForQuery(routedInput));
        return;
    }

    if (visionRelevantQuery && !isExplicitComputerControlQuery(routedInput)) {
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("Routing vision-relevant query through conversation. input=\"%1\"")
                                       .arg(routedInput.left(240)));
        }
        startConversationRequest(routedInput);
    } else if (intent == LocalIntent::Command || m_reasoningRouter->isLikelyCommand(routedInput)) {
        startCommandRequest(routedInput);
    } else if (effectiveIntent.type != IntentType::GENERAL_CHAT
               && effectiveIntent.confidence > 0.4f
               && effectiveIntent.confidence <= 0.8f
               && m_settings->agentEnabled()) {
        startAgentConversationRequest(routedInput, effectiveIntent.type);
    } else {
        startConversationRequest(routedInput);
    }
}

void AssistantController::startListening()
{
    if (isMicrophoneBlocked()) {
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("Listening request ignored while microphone gate is closed."));
        }
        return;
    }
    clearSurfaceError(QStringLiteral("assistant"));
    pauseWakeMonitor();
    startAudioCapture(AudioCaptureMode::Direct, true);
}

void AssistantController::interruptSpeechAndListen()
{
    clearSurfaceError(QStringLiteral("assistant"));
    invalidateWakeMonitorResume();

    // Interrupt both pending generation and speech without ending the conversation session.
    m_aiBackendClient->cancelActiveRequest();
    invalidateActiveTranscription();
    m_streamAssembler->reset();

    if (m_audioCaptureMode == AudioCaptureMode::Direct && !isMicrophoneBlocked()) {
        refreshConversationSession();
        setStatus(QStringLiteral("Listening"));
        emit listeningRequested();
        return;
    }

    if (m_ttsEngine->isSpeaking()) {
        m_ttsEngine->clear();
    }

    if (!m_conversationSessionActive) {
        activateConversationSession();
    } else {
        refreshConversationSession();
    }

    if (m_duplexState == DuplexState::TtsExclusive || m_duplexState == DuplexState::Cooldown) {
        setDuplexState(DuplexState::Open);
    }

    pauseWakeMonitor();
    if (!startAudioCapture(AudioCaptureMode::Direct, true)) {
        setSurfaceError(QStringLiteral("assistant"), QStringLiteral("Unable to start listening"));
        setStatus(QStringLiteral("Unable to start listening"));
        resumeWakeMonitor(shortWakeResumeDelayMs());
        emit idleRequested();
    }
}

void AssistantController::startWakeMonitor()
{
    if (!m_settings->wakeWordEnabled()) {
        m_wakeMonitorEnabled = false;
        m_wakeStartRequested = false;
        m_wakeEngineReady = true;
        m_lastWakeError.clear();
        updateStartupState();
        return;
    }

    m_wakeMonitorEnabled = true;
    m_wakeStartRequested = true;
    m_wakeEngineReady = false;
    m_lastWakeError.clear();
    updateStartupState();
    if (m_wakeWordEngine->isActive()) {
        if (m_wakeWordEngine->isPaused() && canStartWakeMonitor()) {
            if (m_wakeWordEngine->usesExternalAudioInput()) {
                m_activeInputCaptureId = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
                m_audioCaptureMode = AudioCaptureMode::WakeMonitor;
                m_voicePipelineRuntime->startWakeCapture(m_activeInputCaptureId, m_settings->selectedAudioInputDeviceId());
            }
            m_wakeWordEngine->resume();
        }
        return;
    }

    if (!canStartWakeMonitor()) {
        return;
    }

    if (!m_wakeWordEngine->start(
            resolveWakeEngineRuntimePath(),
            resolveWakeEngineModelPath(),
            static_cast<float>(m_settings->wakeTriggerThreshold()),
            m_settings->wakeTriggerCooldownMs(),
            m_settings->selectedAudioInputDeviceId())) {
        m_wakeEngineReady = false;
        if (m_loggingService) {
            m_loggingService->warnFor(QStringLiteral("wake_engine"), QStringLiteral("Wake monitor could not start."));
        }
        updateStartupState();
        return;
    }

    if (m_wakeWordEngine->usesExternalAudioInput()) {
        m_activeInputCaptureId = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
        m_audioCaptureMode = AudioCaptureMode::WakeMonitor;
        m_voicePipelineRuntime->startWakeCapture(m_activeInputCaptureId, m_settings->selectedAudioInputDeviceId());
    }
    updateStartupState();
}

void AssistantController::stopWakeMonitor()
{
    m_wakeMonitorEnabled = false;
    m_wakeStartRequested = false;
    m_wakeEngineReady = false;
    m_lastWakeError.clear();
    if (m_audioCaptureMode == AudioCaptureMode::WakeMonitor) {
        m_voicePipelineRuntime->stopWakeCapture();
        m_audioCaptureMode = AudioCaptureMode::None;
    }
    if (m_wakeWordEngine->isActive()) {
        m_wakeWordEngine->stop();
    }
    if (m_loggingService) {
        m_loggingService->infoFor(QStringLiteral("wake_engine"), QStringLiteral("Wake monitor stopped."));
    }
    updateStartupState();
}

void AssistantController::stopSpeaking()
{
    if (!m_ttsEngine->isSpeaking()) {
        return;
    }

    invalidateWakeMonitorResume();
    m_ttsEngine->clear();
    if (m_duplexState == DuplexState::TtsExclusive || m_duplexState == DuplexState::Cooldown) {
        setDuplexState(DuplexState::Open);
    }
    setStatus(QStringLiteral("Speech interrupted"));
    endConversationSession();
    resumeWakeMonitor(shortWakeResumeDelayMs());
    emit idleRequested();
}

void AssistantController::stopListening()
{
    if (isMicrophoneBlocked()) {
        clearActiveSpeechCapture();
        endConversationSession();
        return;
    }
    invalidateWakeMonitorResume();
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Audio capture stop requested. mode=direct"));
    }
    endConversationSession();
    m_voicePipelineRuntime->stopInputCapture(true);
}

void AssistantController::cancelActiveRequest()
{
    invalidateWakeMonitorResume();
    invalidateActiveTranscription();
    m_aiBackendClient->cancelActiveRequest();
    m_ttsEngine->clear();
    setStatus(QStringLiteral("Request cancelled"));
    endConversationSession();
    resumeWakeMonitor(shortWakeResumeDelayMs());
    emit idleRequested();
}

void AssistantController::cancelCurrentRequest()
{
    if (m_currentState != AssistantState::Processing
        && m_currentState != AssistantState::Speaking) {
        return;
    }
    cancelActiveRequest();
}

void AssistantController::setSelectedModel(const QString &modelId)
{
    m_settings->setChatBackendModel(modelId);
    m_settings->save();
    m_agentCapabilities.selectedModelToolCapable = modelId.toLower().contains(QStringLiteral("qwen"))
        || modelId.toLower().contains(QStringLiteral("granite"))
        || modelId.toLower().contains(QStringLiteral("llama"))
        || modelId.toLower().contains(QStringLiteral("gpt-oss"))
        || modelId.toLower().contains(QStringLiteral("tool"));
    m_agentCapabilities.providerMode = effectiveAgentProviderModeText(m_settings,
                                                                      m_agentCapabilities,
                                                                      modelId,
                                                                      m_aiRequestCoordinator.get());
    m_agentCapabilities.status = agentCapabilityStatusText(m_settings,
                                                           m_agentCapabilities,
                                                           modelId,
                                                           m_aiRequestCoordinator.get());
    emit modelsChanged();
    emit agentStateChanged();
    refreshModels();
}

void AssistantController::setAgentEnabled(bool enabled)
{
    m_settings->setAgentEnabled(enabled);
    m_settings->save();
    emit agentStateChanged();
}

void AssistantController::setBackgroundPanelVisible(bool visible)
{
    if (m_backgroundPanelVisible == visible) {
        return;
    }

    m_backgroundPanelVisible = visible;
    if (visible) {
        emit backgroundTaskResultsChanged();
    }
    emit backgroundPanelVisibleChanged();
}

void AssistantController::noteTaskToastShown(int taskId)
{
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("[UI] toast shown for task %1").arg(taskId));
    }
}

void AssistantController::noteTaskPanelRendered()
{
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("[UI] panel rendered"));
    }
}

void AssistantController::saveAgentSettings(bool enabled,
                                            const QString &providerMode,
                                            double conversationTemperature,
                                            double conversationTopP,
                                            double toolUseTemperature,
                                            int providerTopK,
                                            int maxOutputTokens,
                                            bool memoryAutoWrite,
                                            const QString &webSearchProvider,
                                            const QString &braveSearchApiKey,
                                            bool tracePanelEnabled)
{
    m_settings->setAgentEnabled(enabled);
    m_settings->setAgentProviderMode(providerMode);
    m_settings->setConversationTemperature(conversationTemperature);
    m_settings->setConversationTopP(conversationTopP <= 0.0 ? std::optional<double>{} : std::optional<double>{conversationTopP});
    m_settings->setToolUseTemperature(toolUseTemperature);
    m_settings->setProviderTopK(providerTopK <= 0 ? std::optional<int>{} : std::optional<int>{providerTopK});
    m_settings->setMaxOutputTokens(maxOutputTokens);
    m_settings->setMemoryAutoWrite(memoryAutoWrite);
    m_settings->setWebSearchProvider(webSearchProvider);
    m_settings->setBraveSearchApiKey(braveSearchApiKey);
    m_settings->setTracePanelEnabled(tracePanelEnabled);
    m_settings->save();
    emit agentStateChanged();
}

void AssistantController::saveSettings(
    const QString &providerKind,
    const QString &apiKey,
    const QString &endpoint,
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
    const QString &ffmpegPath,
    double voiceSpeed,
    double voicePitch,
    double micSensitivity,
    const QString &audioInputDeviceId,
    const QString &audioOutputDeviceId,
    bool clickThrough)
{
    const QString previousWakeEngineKind = m_settings->wakeEngineKind();
    m_settings->setChatBackendKind(providerKind);
    m_settings->setChatBackendApiKey(apiKey);
    m_settings->setChatBackendEndpoint(endpoint);
    m_settings->setChatBackendModel(modelId);
    m_settings->setDefaultReasoningMode(static_cast<ReasoningMode>(defaultMode));
    m_settings->setAutoRoutingEnabled(autoRouting);
    m_settings->setStreamingEnabled(streaming);
    m_settings->setRequestTimeoutMs(timeoutMs);
    m_settings->setAecEnabled(aecEnabled);
    m_settings->setRnnoiseEnabled(rnnoiseEnabled);
    m_settings->setVadSensitivity(vadSensitivity);
    m_settings->setWakeEngineKind(wakeEngineKind);
    m_settings->setWhisperExecutable(whisperPath);
    m_settings->setWhisperModelPath(whisperModelPath);
    m_settings->setWakeTriggerThreshold(wakeThreshold);
    m_settings->setWakeTriggerCooldownMs(wakeCooldownMs);
    m_settings->setTtsEngineKind(ttsEngineKind);
    m_settings->setPiperExecutable(piperPath);
    m_settings->setPiperVoiceModel(voicePath);
    m_settings->setFfmpegExecutable(ffmpegPath);
    m_settings->setVoiceSpeed(voiceSpeed);
    m_settings->setVoicePitch(voicePitch);
    m_settings->setMicSensitivity(micSensitivity);
    m_settings->setSelectedAudioInputDeviceId(audioInputDeviceId);
    m_settings->setSelectedAudioOutputDeviceId(audioOutputDeviceId);
    m_settings->setClickThroughEnabled(clickThrough);
    m_settings->save();
    refreshModels();
    setStatus(QStringLiteral("Settings saved"));
    if (previousWakeEngineKind != m_settings->wakeEngineKind()) {
        createWakeWordEngine();
        bindWakeWordEngineSignals();
    }
    if (m_wakeWordEngine->isActive()) {
        stopWakeMonitor();
    }
    m_wakeMonitorEnabled = m_settings->initialSetupCompleted();
    if (m_wakeMonitorEnabled) {
        startWakeMonitor();
    }
    updateStartupState();
}

void AssistantController::setupStateMachine()
{
    connect(this, &AssistantController::idleRequested, this, [this]() {
        transitionToState(AssistantState::Idle);
    });
    connect(this, &AssistantController::listeningRequested, this, [this]() {
        transitionToState(AssistantState::Listening);
    });
    connect(this, &AssistantController::processingRequested, this, [this]() {
        transitionToState(AssistantState::Processing);
    });
    connect(this, &AssistantController::speakingRequested, this, [this]() {
        transitionToState(AssistantState::Speaking);
    });

    transitionToState(AssistantState::Idle);
}

void AssistantController::createWakeWordEngine()
{
    m_wakeEngineReady = false;
    m_wakeStartRequested = false;
    m_lastWakeError.clear();
    if (m_wakeWordEngine) {
        m_wakeWordEngine->stop();
        delete m_wakeWordEngine;
        m_wakeWordEngine = nullptr;
    }

    m_wakeWordEngine = new SherpaWakeWordEngine(m_settings, m_loggingService, this);
}

void AssistantController::bindWakeWordEngineSignals()
{
    connect(m_wakeWordEngine, &WakeWordEngine::engineReady, this, [this]() {
        m_wakeEngineReady = true;
        m_lastWakeError.clear();
        clearSurfaceError(QStringLiteral("assistant"));
        updateStartupState();
    });
    connect(m_wakeWordEngine, &WakeWordEngine::wakeWordDetected, this, [this]() {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (isMicrophoneBlocked() || nowMs < m_ignoreWakeUntilMs) {
            if (m_loggingService) {
                m_loggingService->info(QStringLiteral("Wake trigger ignored while microphone gate is closed."));
            }
            return;
        }

        if (m_ttsEngine->isSpeaking()) {
            m_ttsEngine->clear();
        }

        pauseWakeMonitor();
        invalidateWakeMonitorResume();
        m_aiBackendClient->cancelActiveRequest();
        invalidateActiveTranscription();
        m_ttsEngine->clear();
        m_streamAssembler->reset();
        if (!m_responseText.isEmpty()) {
            m_responseText.clear();
            emit responseTextChanged();
        }
        noteWakeTrigger();
        activateConversationSession();
        m_followUpListeningAfterWakeAck = true;
        m_lastPromptForAiLog = m_settings->wakeWordPhrase();
        if (m_loggingService) {
            m_loggingService->infoFor(QStringLiteral("wake_engine"), QStringLiteral("[VAXIL] Wake word detected"));
            m_loggingService->infoFor(QStringLiteral("wake_engine"), QStringLiteral("[VAXIL] Listening..."));
        }
        deliverLocalResponse(
            m_localResponseEngine->wakeWordReady(buildLocalResponseContext()),
            QStringLiteral("Listening"),
            false);
    });
    connect(m_wakeWordEngine, &WakeWordEngine::errorOccurred, this, [this](const QString &message) {
        m_wakeEngineReady = false;
        m_lastWakeError = message;
        updateStartupState();
        if (m_loggingService) {
            m_loggingService->errorFor(QStringLiteral("wake_engine"), QStringLiteral("%1 wake engine error: %2").arg(wakeEngineDisplayName(), message));
        }
        setSurfaceError(QStringLiteral("assistant"), compactSurfaceText(message));
        setStatus(message);
    });
}

void AssistantController::transitionToState(AssistantState state)
{
    if (m_currentState == state) {
        return;
    }

    m_currentState = state;
    emit stateChanged();
    emit assistantSurfaceChanged();
}

void AssistantController::updateStartupState()
{
    bool blocked = false;
    const QString issue = resolveStartupBlockingIssue(&blocked);
    const bool ready = issue.isEmpty();

    if (m_startupReady == ready && m_startupBlocked == blocked && m_startupBlockingIssue == issue) {
        return;
    }

    m_startupReady = ready;
    m_startupBlocked = blocked;
    m_startupBlockingIssue = issue;
    emit startupStateChanged();
}

QString AssistantController::resolveStartupBlockingIssue(bool *blocked) const
{
    auto setBlocked = [blocked](bool value) {
        if (blocked) {
            *blocked = value;
        }
    };

#if defined(JARVIS_HAS_SHERPA_ONNX) && JARVIS_HAS_SHERPA_ONNX
    const bool wakeEngineCompiled = true;
#else
    const bool wakeEngineCompiled = false;
#endif
    const bool wakeEngineRequired = wakeEngineCompiled && m_settings->wakeWordEnabled();

    if (!m_settings->initialSetupCompleted()) {
        setBlocked(true);
        return QStringLiteral("Initial setup is incomplete.");
    }
    if (m_settings->chatBackendEndpoint().trimmed().isEmpty()) {
        setBlocked(true);
        return QStringLiteral("Local AI backend endpoint is missing.");
    }
    if (m_settings->whisperExecutable().trimmed().isEmpty() || !QFileInfo::exists(m_settings->whisperExecutable())) {
        setBlocked(true);
        return QStringLiteral("Whisper executable is missing.");
    }
    if (m_settings->whisperModelPath().trimmed().isEmpty() || !QFileInfo::exists(m_settings->whisperModelPath())) {
        setBlocked(true);
        return QStringLiteral("Whisper model is missing.");
    }
    if (m_settings->piperExecutable().trimmed().isEmpty() || !QFileInfo::exists(m_settings->piperExecutable())) {
        setBlocked(true);
        return QStringLiteral("Piper executable is missing.");
    }
    if (m_settings->piperVoiceModel().trimmed().isEmpty() || !QFileInfo::exists(m_settings->piperVoiceModel())) {
        setBlocked(true);
        return QStringLiteral("Piper voice model is missing.");
    }
    if (m_settings->ffmpegExecutable().trimmed().isEmpty() || !QFileInfo::exists(m_settings->ffmpegExecutable())) {
        setBlocked(true);
        return QStringLiteral("FFmpeg executable is missing.");
    }

    if (wakeEngineRequired) {
        const QString wakeRuntime = resolveWakeEngineRuntimePath();
        if (wakeRuntime.isEmpty()) {
            setBlocked(true);
            return QStringLiteral("Wake runtime is missing.");
        }
        const QString wakeModel = resolveWakeEngineModelPath();
        if (wakeModel.isEmpty()) {
            setBlocked(true);
            return QStringLiteral("Wake model is missing.");
        }
    }

    if (!m_modelCatalogResolved) {
        setBlocked(false);
        return QStringLiteral("Loading local AI backend...");
    }

    const AiAvailability availability = m_modelCatalogService->availability();
    if (!availability.online) {
        setBlocked(true);
        return availability.status.trimmed().isEmpty()
            ? QStringLiteral("Local AI backend is offline.")
            : availability.status;
    }
    if (!availability.modelAvailable) {
        setBlocked(true);
        return availability.status.trimmed().isEmpty()
            ? QStringLiteral("Selected model is unavailable.")
            : availability.status;
    }

    if (wakeEngineRequired) {
        if (!m_wakeStartRequested) {
            setBlocked(false);
            return QStringLiteral("Starting wake engine...");
        }
        if (!m_lastWakeError.trimmed().isEmpty()) {
            setBlocked(true);
            return m_lastWakeError;
        }
        if (!m_wakeEngineReady) {
            setBlocked(false);
            return QStringLiteral("Starting wake engine...");
        }
    }

    setBlocked(false);
    return {};
}

void AssistantController::setStatus(const QString &status)
{
    if (m_statusText == status) {
        return;
    }
    m_statusText = status;
    if (m_loggingService) {
        m_loggingService->info(status);
    }
    emit statusTextChanged();
}

void AssistantController::setDuplexState(DuplexState state)
{
    m_duplexState = state;
}

void AssistantController::invalidateWakeMonitorResume()
{
    ++m_wakeResumeSequence;
}

void AssistantController::invalidateActiveTranscription()
{
    m_activeSttRequestId = 0;
}

void AssistantController::clearActiveSpeechCapture()
{
    invalidateActiveTranscription();
    if (m_audioCaptureMode == AudioCaptureMode::WakeMonitor) {
        m_voicePipelineRuntime->stopWakeCapture();
    } else if (m_audioCaptureMode == AudioCaptureMode::Direct) {
        m_voicePipelineRuntime->clearInputCapture();
    }
    m_audioCaptureMode = AudioCaptureMode::None;
    m_lastCompletedCaptureMode = AudioCaptureMode::None;
}

void AssistantController::beginTtsExclusiveMode()
{
    ignoreWakeTriggersFor(postSpeechWakeResumeDelayMs());
    clearActiveSpeechCapture();
    pauseWakeMonitor();
    setDuplexState(DuplexState::TtsExclusive);
}

void AssistantController::enterPostSpeechCooldown()
{
    ignoreWakeTriggersFor(postSpeechWakeResumeDelayMs());
    setDuplexState(DuplexState::Cooldown);
}

bool AssistantController::isMicrophoneBlocked() const
{
    return m_duplexState == DuplexState::TtsExclusive || m_duplexState == DuplexState::Cooldown;
}

void AssistantController::noteWakeTrigger()
{
    ++m_wakeTriggerToken;
    emit wakeTriggerTokenChanged();
}

void AssistantController::activateConversationSession()
{
    m_conversationSessionActive = true;
    m_consecutiveSessionMisses = 0;
    refreshConversationSession();
}

void AssistantController::refreshConversationSession()
{
    if (!m_conversationSessionActive) {
        return;
    }

    m_conversationSessionExpiresAtMs = QDateTime::currentMSecsSinceEpoch() + conversationSessionTimeoutMs();
}

void AssistantController::endConversationSession()
{
    m_conversationSessionActive = false;
    m_consecutiveSessionMisses = 0;
    m_conversationSessionExpiresAtMs = 0;
    m_followUpListeningAfterWakeAck = false;
}

bool AssistantController::conversationSessionShouldContinue() const
{
    if (!m_conversationSessionActive) {
        return false;
    }

    return QDateTime::currentMSecsSinceEpoch() < m_conversationSessionExpiresAtMs;
}

bool AssistantController::scheduleConversationSessionListening(int delayMs)
{
    if (!m_followUpListeningAfterWakeAck && !conversationSessionShouldContinue()) {
        return false;
    }

    const quint64 resumeSequence = ++m_wakeResumeSequence;
    QTimer::singleShot(delayMs, this, [this, resumeSequence]() {
        if (resumeSequence != m_wakeResumeSequence) {
            return;
        }
        if (m_ttsEngine->isSpeaking()) {
            return;
        }
        if (!m_followUpListeningAfterWakeAck && !conversationSessionShouldContinue()) {
            endConversationSession();
            resumeWakeMonitor(shortWakeResumeDelayMs());
            emit idleRequested();
            return;
        }

        if (m_duplexState == DuplexState::Cooldown) {
            setDuplexState(DuplexState::Open);
        }

        m_followUpListeningAfterWakeAck = false;
        if (!startAudioCapture(AudioCaptureMode::Direct, true)) {
            endConversationSession();
            enterPostSpeechCooldown();
            resumeWakeMonitor(shortWakeResumeDelayMs());
            emit idleRequested();
        }
    });
    return true;
}

void AssistantController::pauseWakeMonitor()
{
    invalidateWakeMonitorResume();
    if (!m_wakeMonitorEnabled || !m_wakeWordEngine->isActive()) {
        return;
    }

    if (m_wakeWordEngine->usesExternalAudioInput()) {
        m_voicePipelineRuntime->stopWakeCapture();
        if (m_audioCaptureMode == AudioCaptureMode::WakeMonitor) {
            m_audioCaptureMode = AudioCaptureMode::None;
        }
    }
    m_wakeWordEngine->pause();
}

void AssistantController::resumeWakeMonitor(int delayMs)
{
    if (!m_wakeMonitorEnabled) {
        return;
    }

    const quint64 resumeSequence = ++m_wakeResumeSequence;
    QTimer::singleShot(delayMs, this, [this, resumeSequence]() {
        if (resumeSequence != m_wakeResumeSequence) {
            return;
        }
        if (!m_wakeMonitorEnabled) {
            return;
        }

        // The wake resume timer is the point where post-TTS cooldown ends.
        // Lift the mic gate before evaluating whether wake monitoring can start.
        if (m_duplexState == DuplexState::Cooldown) {
            setDuplexState(DuplexState::Open);
        }
        if (!canStartWakeMonitor()) {
            return;
        }

        setDuplexState(DuplexState::WakeOnly);
        if (m_wakeWordEngine->isActive()) {
            if (m_wakeWordEngine->isPaused()) {
                m_wakeWordEngine->resume();
                if (m_wakeWordEngine->usesExternalAudioInput()) {
                    m_activeInputCaptureId = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
                    m_audioCaptureMode = AudioCaptureMode::WakeMonitor;
                    m_voicePipelineRuntime->startWakeCapture(m_activeInputCaptureId, m_settings->selectedAudioInputDeviceId());
                }
            }
        } else {
            startWakeMonitor();
        }
    });
}

void AssistantController::ignoreWakeTriggersFor(int delayMs)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_ignoreWakeUntilMs = std::max(m_ignoreWakeUntilMs, nowMs + static_cast<qint64>(delayMs));
}

int AssistantController::shortWakeResumeDelayMs() const
{
    return std::max(250, m_settings->wakeTriggerCooldownMs() / 2);
}

int AssistantController::postSpeechWakeResumeDelayMs() const
{
    return std::max(450, m_settings->wakeTriggerCooldownMs());
}

int AssistantController::postSpeechWakeEngineStartDelayMs() const
{
    return std::min(120, postSpeechWakeResumeDelayMs());
}

int AssistantController::followUpListeningDelayMs() const
{
    return 60;
}

int AssistantController::conversationSessionTimeoutMs() const
{
    return 45000;
}

int AssistantController::conversationSessionRestartDelayMs() const
{
    return 180;
}

int AssistantController::maxConversationSessionMisses() const
{
    return 3;
}

QString AssistantController::buildSttPrompt() const
{
    const QString wakeWord = m_settings->wakeWordPhrase().trimmed().isEmpty()
        ? QStringLiteral("Hey Vaxil")
        : m_settings->wakeWordPhrase().trimmed();
    return QStringLiteral(
        "%1. Everyday English speech. Common topics include time, date, settings, files, logs, web search, memory, timers, and general conversation. Output only what the speaker says.")
        .arg(wakeWord);
}

bool AssistantController::shouldIgnoreAmbiguousTranscript(const QString &transcript) const
{
    const QStringList words = transcriptWords(transcript);
    if (words.isEmpty()) {
        return true;
    }

    const QString joined = words.join(QStringLiteral(" "));
    static const QStringList ambiguousPhrases = {
        QStringLiteral("you"),
        QStringLiteral("yeah"),
        QStringLiteral("yep"),
        QStringLiteral("uh"),
        QStringLiteral("um"),
        QStringLiteral("hmm"),
        QStringLiteral("hm"),
        QStringLiteral("ah"),
        QStringLiteral("oh"),
        QStringLiteral("i am now"),
        QStringLiteral("its a time"),
        QStringLiteral("it's a time")
    };
    if (ambiguousPhrases.contains(joined)) {
        return true;
    }

    if (WakeWordDetector::isWakeWordDetected(transcript) || isConversationStopPhrase(transcript)) {
        return false;
    }

    if (words.size() == 1) {
        const QString token = words.first();
        static const QStringList allowSingleWordCommands = {
            QStringLiteral("stop"),
            QStringLiteral("mute"),
            QStringLiteral("unmute"),
            QStringLiteral("open"),
            QStringLiteral("close"),
            QStringLiteral("start")
        };
        if (!allowSingleWordCommands.contains(token) && token.size() <= 3) {
            return true;
        }
    }

    if (m_conversationSessionActive && !startsWithAllowedFollowUpWord(words)) {
        if (words.size() <= 2) {
            return true;
        }

        static const QSet<QString> lowSignalStarts = {
            QStringLiteral("you"),
            QStringLiteral("your"),
            QStringLiteral("it"),
            QStringLiteral("its"),
            QStringLiteral("this"),
            QStringLiteral("that"),
            QStringLiteral("tip")
        };
        if (words.size() <= 4 && lowSignalStarts.contains(words.first())) {
            return true;
        }
    }

    return false;
}

bool AssistantController::shouldEndConversationSession(const QString &input) const
{
    return m_conversationSessionActive && isConversationStopPhrase(input);
}

void AssistantController::handleConversationSessionMiss(const QString &statusText)
{
    if (!m_conversationSessionActive) {
        setStatus(statusText);
        resumeWakeMonitor(shortWakeResumeDelayMs());
        emit idleRequested();
        return;
    }

    ++m_consecutiveSessionMisses;
    const bool noSpeechMiss = statusText.compare(QStringLiteral("No speech detected"), Qt::CaseInsensitive) == 0;
    const int missLimit = noSpeechMiss ? 2 : maxConversationSessionMisses();
    if (m_consecutiveSessionMisses >= missLimit) {
        endConversationSession();
        setStatus(QStringLiteral("Standing by"));
        resumeWakeMonitor(shortWakeResumeDelayMs());
        emit idleRequested();
        return;
    }

    refreshConversationSession();
    setStatus(QStringLiteral("Listening"));
    setDuplexState(DuplexState::Open);
    const int restartDelayMs = noSpeechMiss
        ? std::max(500, conversationSessionRestartDelayMs() * 3)
        : conversationSessionRestartDelayMs();
    if (!scheduleConversationSessionListening(restartDelayMs)) {
        endConversationSession();
        resumeWakeMonitor(shortWakeResumeDelayMs());
        emit idleRequested();
    }
}

void AssistantController::scheduleWakeMonitorRestart(int delayMs)
{
    if (!m_wakeMonitorEnabled && m_settings->initialSetupCompleted()) {
        m_wakeMonitorEnabled = true;
    }

    if (!m_wakeMonitorEnabled) {
        return;
    }

    resumeWakeMonitor(delayMs);
}

bool AssistantController::canStartWakeMonitor() const
{
#if !defined(JARVIS_HAS_SHERPA_ONNX) || !JARVIS_HAS_SHERPA_ONNX
    return false;
#else
    return m_wakeMonitorEnabled
        && m_settings->wakeWordEnabled()
        && m_currentState != AssistantState::Listening
        && !isMicrophoneBlocked()
        && m_audioCaptureMode == AudioCaptureMode::None
        && !m_ttsEngine->isSpeaking()
        && !resolveWakeEngineRuntimePath().isEmpty()
        && !resolveWakeEngineModelPath().isEmpty();
#endif
}

void AssistantController::reconfigureGestureActionRouter()
{
    if (!m_gestureActionRouterThread.isRunning()
        || m_gestureStateMachine == nullptr
        || m_gestureActionRouter == nullptr
        || m_settings == nullptr) {
        return;
    }

    QMetaObject::invokeMethod(
        m_gestureStateMachine,
        "configure",
        Qt::QueuedConnection,
        Q_ARG(bool, m_settings->gestureEnabled()),
        Q_ARG(double, m_settings->visionGesturesMinConfidence()),
        Q_ARG(int, m_settings->gestureStabilityMs()),
        Q_ARG(int, m_settings->gestureCooldownMs()));

    QMetaObject::invokeMethod(
        m_gestureActionRouter,
        "configure",
        Qt::QueuedConnection,
        Q_ARG(bool, m_settings->gestureEnabled()));
}

QString AssistantController::resolveWakeEngineRuntimePath() const
{
    return firstExistingPath({
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/third_party/sherpa-onnx/sherpa-onnx-v1.12.33-win-x64-shared-MD-Release-no-tts"),
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/third_party/sherpa-onnx/sherpa-onnx-v1.12.33-linux-x64-shared"),
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/third_party/sherpa-onnx"),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/third_party/sherpa-onnx/sherpa-onnx-v1.12.33-win-x64-shared-MD-Release-no-tts"),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/third_party/sherpa-onnx/sherpa-onnx-v1.12.33-linux-x64-shared"),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/third_party/sherpa-onnx")
    });
}

QString AssistantController::resolveWakeEngineModelPath() const
{
    return firstExistingPath({
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/third_party/sherpa-kws-model/sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01"),
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/third_party/sherpa-kws-model"),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/third_party/sherpa-kws-model/sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01"),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/third_party/sherpa-kws-model"),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/third_party/models/sherpa-kws/sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01")
    });
}

QString AssistantController::wakeEngineDisplayName() const
{
    return QStringLiteral("sherpa-onnx");
}

bool AssistantController::startAudioCapture(AudioCaptureMode mode, bool announceListening)
{
    invalidateWakeMonitorResume();
    if (isMicrophoneBlocked() || m_ttsEngine->isSpeaking()) {
        return false;
    }
    if (m_audioCaptureMode != AudioCaptureMode::None || mode != AudioCaptureMode::Direct) {
        return false;
    }

    m_activeInputCaptureId = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
    m_audioCaptureMode = mode;
    m_voicePipelineRuntime->startInputCapture(
        m_activeInputCaptureId,
        m_settings->micSensitivity(),
        m_settings->selectedAudioInputDeviceId());
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Audio capture started. mode=direct device=\"%1\" sensitivity=%2")
            .arg(m_settings->selectedAudioInputDeviceId())
            .arg(m_settings->micSensitivity(), 0, 'f', 3));
        m_loggingService->infoFor(QStringLiteral("wake_engine"), QStringLiteral("[VAXIL] Listening..."));
    }
    if (announceListening) {
        setDuplexState(DuplexState::Listening);
        setStatus(QStringLiteral("Listening"));
        emit listeningRequested();
    } else {
        setDuplexState(DuplexState::Open);
    }
    return true;
}

void AssistantController::updateUserProfileFromInput(const QString &input)
{
    const QString lowered = input.toLower();
    if (lowered.startsWith(QStringLiteral("my name is "))) {
        m_identityProfileService->setUserName(input.mid(11).trimmed());
        return;
    }

    if (lowered.startsWith(QStringLiteral("i prefer "))) {
        m_identityProfileService->setPreference(QStringLiteral("general"), input.mid(9).trimmed());
    }
}

LocalResponseContext AssistantController::buildLocalResponseContext() const
{
    const QDateTime now = QDateTime::currentDateTime();
    const int hour = now.time().hour();
    QString timeOfDay = QStringLiteral("afternoon");
    if (hour < 12) {
        timeOfDay = QStringLiteral("morning");
    } else if (hour >= 18) {
        timeOfDay = QStringLiteral("evening");
    }

    const UserProfile profile = m_identityProfileService->userProfile();
    const QString userName = profile.userName;

    return {
        .assistantName = m_identityProfileService->identity().assistantName,
        .userName = userName.isEmpty() ? m_memoryStore->userName() : userName,
        .timeOfDay = timeOfDay,
        .systemState = stateName(),
        .tone = m_identityProfileService->identity().tone,
        .addressingStyle = m_identityProfileService->identity().addressingStyle,
        .currentTime = QLocale::system().toString(now.time(), QLocale::ShortFormat),
        .currentDate = QLocale::system().toString(now.date(), QLocale::LongFormat),
        .wakeWord = m_settings->wakeWordPhrase()
    };
}

void AssistantController::deliverLocalResponse(const QString &text, const QString &status, bool speak)
{
    SpokenReply reply;
    reply.displayText = text;
    reply.spokenText = speak ? text : QString{};
    reply.shouldSpeak = speak;
    const bool spoke = m_responseFinalizer->finalizeResponse(
        QStringLiteral("local"),
        reply,
        &m_responseText,
        [this]() { emit responseTextChanged(); },
        [this]() { refreshConversationSession(); },
        [this](const QString &response, const QString &source, const QString &logStatus) { logPromptResponsePair(response, source, logStatus); },
        status,
        [this](const QString &newStatus) { setStatus(newStatus); });
    if (!spoke) {
        setDuplexState(DuplexState::Open);
        if (conversationSessionShouldContinue()) {
            const int restartDelayMs = m_followUpListeningAfterWakeAck
                ? followUpListeningDelayMs()
                : conversationSessionRestartDelayMs();
            if (!scheduleConversationSessionListening(restartDelayMs)) {
                endConversationSession();
                scheduleWakeMonitorRestart();
            }
        } else {
            endConversationSession();
            scheduleWakeMonitorRestart();
        }
        emit idleRequested();
    }
}

void AssistantController::startConversationRequest(const QString &input)
{
    const ReasoningMode mode = m_aiRequestCoordinator->chooseReasoningMode(input);
    m_activeReasoningMode = mode;
    const QString modelId = m_aiRequestCoordinator->resolveModelId(availableModelIds());
    if (modelId.isEmpty()) {
        setStatus(QStringLiteral("No local AI backend model selected"));
        emit idleRequested();
        return;
    }

    m_activeRequestKind = RequestKind::Conversation;
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Starting conversation request. model=\"%1\" input=\"%2\"")
            .arg(modelId, userFacingPromptForLogging(input).left(240)));
    }

    QList<MemoryRecord> requestMemory = m_memoryPolicyHandler->requestMemory(input, runtimeToolStatusMemory(m_settings));

    const auto messages = m_promptAdapter->buildConversationMessages(
        input,
        m_memoryStore->recentMessages(8),
        requestMemory,
        m_identityProfileService->identity(),
        m_identityProfileService->userProfile(),
        mode,
        buildVisionPromptContext(input, IntentType::GENERAL_CHAT));

    m_activeRequestId = m_aiBackendClient->sendChatRequest(messages, modelId, {
        .mode = mode,
        .kind = RequestKind::Conversation,
        .stream = m_settings->streamingEnabled(),
        .temperature = m_settings->conversationTemperature(),
        .topP = m_settings->conversationTopP(),
        .providerTopK = m_settings->providerTopK(),
        .maxTokens = m_settings->maxOutputTokens(),
        .timeout = std::chrono::milliseconds(effectiveRequestTimeoutMs(m_settings))
    });
}

void AssistantController::startAgentConversationRequest(const QString &input, IntentType expectedIntent)
{
    const ReasoningMode mode = m_aiRequestCoordinator->chooseReasoningMode(input);
    m_activeReasoningMode = mode;
    const QString modelId = m_aiRequestCoordinator->resolveModelId(availableModelIds());
    if (modelId.isEmpty()) {
        setStatus(QStringLiteral("No local AI backend model selected"));
        emit idleRequested();
        return;
    }

    m_activeRequestKind = RequestKind::AgentConversation;
    m_lastAgentInput = input;
    m_lastAgentIntent = expectedIntent;
    m_activeAgentIteration = 0;
    m_agentTrace.clear();
    emit agentTraceChanged();
    const QList<AgentToolSpec> relevantTools = m_promptAdapter->getRelevantTools(
        input,
        expectedIntent,
        m_agentToolbox->builtInTools());
    const AgentTransportMode transportMode = m_aiRequestCoordinator->resolveAgentTransport(m_agentCapabilities, modelId);
    const bool directToolCalling = transportMode == AgentTransportMode::Responses;
    appendAgentTrace(QStringLiteral("session"),
                     QStringLiteral("Agent request"),
                     directToolCalling
                         ? QStringLiteral("Starting direct tool-calling agent conversation")
                         : QStringLiteral("Starting hybrid agent conversation"),
                     true);

    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Starting agent request. model=\"%1\" input=\"%2\"")
            .arg(modelId, input.left(240)));
    }

    QList<MemoryRecord> requestMemory = m_memoryPolicyHandler->requestMemory(input, runtimeToolStatusMemory(m_settings));

    if (transportMode == AgentTransportMode::CapabilityError) {
        deliverLocalResponse(
            m_localResponseEngine->respondToError(QStringLiteral("error_capability"), buildLocalResponseContext()),
            m_aiRequestCoordinator->capabilityErrorText(m_agentCapabilities, modelId),
            true);
        return;
    }

    if (directToolCalling) {
        const QString visionContext = buildVisionPromptContext(input, expectedIntent);
        const AgentRequest request{
            .model = modelId,
            .instructions = m_promptAdapter->buildAgentInstructions(
                requestMemory,
                m_skillStore->listSkills(),
                relevantTools,
                m_identityProfileService->identity(),
                m_identityProfileService->userProfile(),
                QDir::currentPath(),
                expectedIntent,
                m_settings->memoryAutoWrite(),
                visionContext),
            .inputText = input,
            .previousResponseId = {},
            .tools = relevantTools,
            .toolResults = {},
            .sampling = samplingProfile(),
            .mode = mode,
            .timeout = std::chrono::milliseconds(effectiveRequestTimeoutMs(m_settings))
        };
        m_activeRequestId = m_aiBackendClient->sendAgentRequest(request);
        return;
    }

    const auto messages = m_promptAdapter->buildHybridAgentMessages(
        input,
        requestMemory,
        m_identityProfileService->identity(),
        m_identityProfileService->userProfile(),
        QDir::currentPath(),
        expectedIntent,
        relevantTools,
        mode,
        buildVisionPromptContext(input, expectedIntent));

    m_activeRequestId = m_aiBackendClient->sendChatRequest(messages, modelId, {
        .mode = mode,
        .kind = RequestKind::AgentConversation,
        .stream = false,
        .temperature = m_settings->conversationTemperature(),
        .topP = m_settings->conversationTopP(),
        .providerTopK = m_settings->providerTopK(),
        .maxTokens = m_settings->maxOutputTokens(),
        .timeout = std::chrono::milliseconds(effectiveRequestTimeoutMs(m_settings))
    });
}

void AssistantController::continueAgentConversation(const QList<AgentToolResult> &results)
{
    if (m_activeAgentIteration >= 6) {
        handleConversationFinished(QStringLiteral("I’ve hit the tool-call limit for this request. Please narrow it down and try again."));
        return;
    }

    ++m_activeAgentIteration;
    QList<AgentToolSpec> relevantTools = m_promptAdapter->getRelevantTools(
        m_lastAgentInput,
        m_lastAgentIntent,
        m_agentToolbox->builtInTools());
    if (relevantTools.isEmpty()) {
        relevantTools = m_agentToolbox->builtInTools();
    }

    QList<MemoryRecord> requestMemory = m_memoryPolicyHandler->requestMemory(m_lastAgentInput, runtimeToolStatusMemory(m_settings));

    const AgentRequest request{
        .model = selectedModel(),
        .instructions = m_promptAdapter->buildAgentInstructions(
            requestMemory,
            m_skillStore->listSkills(),
            relevantTools,
            m_identityProfileService->identity(),
            m_identityProfileService->userProfile(),
            QDir::currentPath(),
            m_lastAgentIntent,
            m_settings->memoryAutoWrite(),
            buildVisionPromptContext(m_lastAgentInput, m_lastAgentIntent)),
        .inputText = {},
        .previousResponseId = m_previousAgentResponseId,
        .tools = relevantTools,
        .toolResults = results,
        .sampling = samplingProfile(),
        .mode = m_activeReasoningMode,
        .timeout = std::chrono::milliseconds(effectiveRequestTimeoutMs(m_settings))
    };
    m_activeRequestId = m_aiBackendClient->sendAgentRequest(request);
}

void AssistantController::handleVisionSnapshot(const VisionSnapshot &snapshot)
{
    if (!m_worldStateCache) {
        return;
    }

    m_worldStateCache->setHistoryWindowMs(std::max(10000, m_settings->visionStaleThresholdMs() * 6));
    m_worldStateCache->setMaxSnapshotAgeMs(m_settings->visionStaleThresholdMs());
    if (!m_worldStateCache->ingestSnapshot(snapshot)) {
        if (m_loggingService) {
            m_loggingService->logVisionDrop(QStringLiteral("stale_rejected"),
                                            QStringLiteral("trace=\"%1\" node=\"%2\"")
                                                .arg(snapshot.traceId, snapshot.nodeId),
                                            QStringLiteral("vision_cache_stale_%1").arg(snapshot.nodeId),
                                            1200);
        }
        return;
    }
    if (m_loggingService) {
        m_loggingService->logVisionSnapshot(snapshot);
    }
    applyVisionGestureTriggers(snapshot);
}

QString AssistantController::buildVisionPromptContext(const QString &input, IntentType intent) const
{
    if (!m_settings->visionEnabled() || !m_worldStateCache) {
        return {};
    }
    if (!shouldUseVisionContext(input, intent)) {
        return {};
    }

    const auto snapshot = m_worldStateCache->latestFreshSnapshot(m_settings->visionStaleThresholdMs());
    if (!snapshot.has_value()) {
        return {};
    }

    QString context = snapshot->summary.trimmed();
    if (context.isEmpty()) {
        context = m_worldStateCache->filteredSummary(m_settings->visionStaleThresholdMs());
    }
    if (context.isEmpty()) {
        return {};
    }

    if (!VisionContextGate::needsRawVisionDetails(input)) {
        return context;
    }

    QStringList objectNames;
    for (const auto &object : snapshot->objects) {
        objectNames.push_back(QStringLiteral("%1(%2)")
                                  .arg(object.className)
                                  .arg(object.confidence, 0, 'f', 2));
    }

    QStringList gestureNames;
    for (const auto &gesture : snapshot->gestures) {
        gestureNames.push_back(QStringLiteral("%1(%2)")
                                   .arg(gesture.name)
                                   .arg(gesture.confidence, 0, 'f', 2));
    }

    if (!objectNames.isEmpty()) {
        context += QStringLiteral(" Objects: %1.").arg(objectNames.join(QStringLiteral(", ")));
    }
    if (!gestureNames.isEmpty()) {
        context += QStringLiteral(" Gestures: %1.").arg(gestureNames.join(QStringLiteral(", ")));
    }
    return context;
}

QString AssistantController::buildDirectVisionResponse(const QString &input) const
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool recentVisionQuery = (nowMs - m_lastVisionQueryMs) <= 12000;
    if (!m_settings->visionEnabled()
        || !m_worldStateCache
        || !(isDirectVisionAnswerQuery(input) || (recentVisionQuery && isVisionFollowUpQuery(input)))) {
        return {};
    }

    const auto snapshot = m_worldStateCache->latestFreshSnapshot(m_settings->visionStaleThresholdMs());
    if (!snapshot.has_value()) {
        return {};
    }

    const QString normalized = input.trimmed().toLower();
    const auto bestPortableObject = [&snapshot]() -> std::optional<VisionObjectDetection> {
        for (const auto &object : snapshot->objects) {
            if (isPortableVisionObject(object.className)) {
                return object;
            }
        }
        return std::nullopt;
    }();

    const auto hasGesture = [&snapshot](const QString &name) {
        for (const auto &gesture : snapshot->gestures) {
            if (gesture.name.compare(name, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    };

    const QString summary = snapshot->summary.trimmed().isEmpty()
        ? m_worldStateCache->filteredSummary(m_settings->visionStaleThresholdMs()).trimmed()
        : snapshot->summary.trimmed();

    if (normalized.contains(QStringLiteral("holding")) || normalized.contains(QStringLiteral("in my hand"))) {
        if (bestPortableObject.has_value()) {
            return QStringLiteral("It looks like you're holding %1.").arg(withArticle(bestPortableObject->className));
        }
        if (hasGesture(QStringLiteral("pinch"))) {
            return QStringLiteral("I can see a pinch gesture, but I can't confidently identify the object in your hand.");
        }
        return QStringLiteral("I can't confidently tell what you're holding right now.");
    }

    if (normalized.contains(QStringLiteral("middle finger"))) {
        if (hasGesture(QStringLiteral("middle_finger"))) {
            return QStringLiteral("Yes, your middle finger is extended right now.");
        }
        return QStringLiteral("I can't confidently see a middle finger gesture right now.");
    }

    if (normalized.contains(QStringLiteral("thumbs up"))) {
        if (hasGesture(QStringLiteral("thumbs_up"))) {
            return QStringLiteral("Yes, that looks like a thumbs up.");
        }
        return QStringLiteral("I can't confidently see a thumbs up right now.");
    }

    if (normalized.contains(QStringLiteral("thumbs down"))) {
        if (hasGesture(QStringLiteral("thumbs_down"))) {
            return QStringLiteral("Yes, that looks like a thumbs down.");
        }
        return QStringLiteral("I can't confidently see a thumbs down right now.");
    }

    if (normalized.contains(QStringLiteral("how many fingers"))
        || normalized.contains(QStringLiteral("finger count"))
        || normalized.contains(QStringLiteral("number of fingers"))) {
        if (snapshot->fingerCount >= 0) {
            return QStringLiteral("I can see %1 finger%2 extended.")
                .arg(snapshot->fingerCount)
                .arg(snapshot->fingerCount == 1 ? QString() : QStringLiteral("s"));
        }
        return QStringLiteral("I can't confidently count your fingers right now.");
    }

    if (normalized.contains(QStringLiteral("open or closed"))
        || normalized.contains(QStringLiteral("closed or open"))
        || normalized.contains(QStringLiteral("is my hand open"))
        || normalized.contains(QStringLiteral("is my hand closed"))
        || normalized.contains(QStringLiteral("closed hand"))
        || normalized.contains(QStringLiteral("fist"))
        || normalized.contains(QStringLiteral("my hand"))) {
        if (hasGesture(QStringLiteral("open_hand"))) {
            return QStringLiteral("Your hand looks open right now.");
        }
        if (hasGesture(QStringLiteral("closed_hand"))) {
            return QStringLiteral("Your hand looks closed right now.");
        }
        if (hasGesture(QStringLiteral("middle_finger"))) {
            return QStringLiteral("Your middle finger is extended right now.");
        }
        if (hasGesture(QStringLiteral("thumbs_up"))) {
            return QStringLiteral("Your hand looks like a thumbs up right now.");
        }
        if (hasGesture(QStringLiteral("thumbs_down"))) {
            return QStringLiteral("Your hand looks like a thumbs down right now.");
        }
        if (hasGesture(QStringLiteral("pinch"))) {
            return bestPortableObject.has_value()
                ? QStringLiteral("Your hand looks closed around %1.").arg(withArticle(bestPortableObject->className))
                : QStringLiteral("Your hand looks pinched, like you're holding something.");
        }
        if (hasGesture(QStringLiteral("two_fingers"))) {
            return QStringLiteral("Your hand looks like two fingers are extended.");
        }
        return QStringLiteral("I can see your hand, but I can't confidently tell whether it is open or closed right now.");
    }

    if (normalized.contains(QStringLiteral("what do you see"))
        || normalized.contains(QStringLiteral("can you see"))
        || normalized.contains(QStringLiteral("do you see"))
        || normalized.contains(QStringLiteral("what is this"))
        || normalized.contains(QStringLiteral("what is that"))) {
        if (!summary.isEmpty()) {
            return summary.endsWith(QChar::fromLatin1('.')) ? summary : summary + QChar::fromLatin1('.');
        }
    }

    return {};
}

bool AssistantController::shouldUseVisionContext(const QString &input, IntentType intent) const
{
    return VisionContextGate::shouldInject(
        input,
        intent,
        m_worldStateCache != nullptr && m_worldStateCache->isFresh(m_settings->visionStaleThresholdMs()),
        m_settings->visionContextAlwaysOn(),
        (QDateTime::currentMSecsSinceEpoch() - m_lastVisionGestureTriggerMs) <= 3000);
}

void AssistantController::applyVisionGestureTriggers(const VisionSnapshot &snapshot)
{
    if (!m_gestureInterpreter) {
        return;
    }
    m_gestureInterpreter->ingestSnapshot(snapshot);
}

void AssistantController::handleGestureFarewell()
{
    if (m_currentState == AssistantState::Listening) {
        stopListening();
    } else if (m_currentState == AssistantState::Processing || m_currentState == AssistantState::Speaking) {
        cancelActiveRequest();
    } else {
        endConversationSession();
    }

    deliverLocalResponse(QStringLiteral("Bye sir."), QStringLiteral("Gesture farewell"), true);
    endConversationSession();
}

void AssistantController::handleGestureConfirm()
{
    if (m_currentState == AssistantState::Listening) {
        stopListening();
    }

    deliverLocalResponse(QStringLiteral("Confirmed."), QStringLiteral("Gesture confirm"), true);
}

void AssistantController::handleGestureReject()
{
    if (m_currentState == AssistantState::Listening) {
        stopListening();
    } else if (m_currentState == AssistantState::Processing || m_currentState == AssistantState::Speaking) {
        cancelActiveRequest();
    }

    deliverLocalResponse(QStringLiteral("Rejected."), QStringLiteral("Gesture reject"), true);
}

void AssistantController::startCommandRequest(const QString &input)
{
    m_activeReasoningMode = ReasoningMode::Fast;
    const QString modelId = m_aiRequestCoordinator->resolveModelId(availableModelIds());
    if (modelId.isEmpty()) {
        setStatus(QStringLiteral("No local AI backend model selected"));
        emit idleRequested();
        return;
    }

    m_activeRequestKind = RequestKind::CommandExtraction;
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Starting command extraction request. model=\"%1\" input=\"%2\"")
            .arg(modelId, input.left(240)));
    }
    m_activeRequestId = m_aiBackendClient->sendChatRequest(
        m_promptAdapter->buildCommandMessages(
            input,
            m_identityProfileService->identity(),
            m_identityProfileService->userProfile(),
            ReasoningMode::Fast),
        modelId,
        {.mode = ReasoningMode::Fast,
         .kind = RequestKind::CommandExtraction,
         .stream = false,
         .temperature = m_settings->toolUseTemperature(),
         .topP = m_settings->conversationTopP(),
         .providerTopK = m_settings->providerTopK(),
         .maxTokens = m_settings->maxOutputTokens(),
         .timeout = std::chrono::milliseconds(effectiveRequestTimeoutMs(m_settings))});
}

void AssistantController::handleConversationFinished(const QString &text)
{
    const SpokenReply reply = parseSpokenReply(text);
    m_streamAssembler->drainRemainingText();
    const bool spoke = m_responseFinalizer->finalizeResponse(
        QStringLiteral("conversation"),
        reply,
        &m_responseText,
        [this]() { emit responseTextChanged(); },
        [this]() { refreshConversationSession(); },
        [this](const QString &response, const QString &source, const QString &status) { logPromptResponsePair(response, source, status); },
        QStringLiteral("Response ready"),
        [this](const QString &status) { setStatus(status); });
    if (!spoke && !m_ttsEngine->isSpeaking()) {
        setDuplexState(DuplexState::Open);
        if (conversationSessionShouldContinue()) {
            if (!scheduleConversationSessionListening(conversationSessionRestartDelayMs())) {
                endConversationSession();
                scheduleWakeMonitorRestart();
            }
        } else {
            endConversationSession();
            scheduleWakeMonitorRestart();
        }
        emit idleRequested();
    }
}

void AssistantController::handleHybridAgentFinished(const QString &payload)
{
    appendAgentTrace(QStringLiteral("model"), QStringLiteral("Hybrid agent response"), QStringLiteral("Received hybrid payload"), true);

    const QString jsonPayload = extractJsonObjectPayload(payload);
    const auto json = nlohmann::json::parse(jsonPayload.toStdString(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        appendAgentTrace(QStringLiteral("validation"), QStringLiteral("Hybrid payload rejected"), QStringLiteral("The model returned invalid JSON."), false);
        deliverLocalResponse(
            m_localResponseEngine->respondToError(QStringLiteral("error_invalid"), buildLocalResponseContext()),
            QStringLiteral("The chat adapter returned invalid JSON."),
            true);
        return;
    }

    const IntentType returnedIntent = intentTypeFromString(QString::fromStdString(json.value("intent", std::string{})));
    const QString message = QString::fromStdString(json.value("message", std::string{})).trimmed();
    const QList<AgentToolSpec> relevantTools = m_promptAdapter->getRelevantTools(
        m_lastAgentInput,
        m_lastAgentIntent,
        m_agentToolbox->builtInTools());
    const QStringList allowedTaskTypes = [&relevantTools]() {
        QStringList names;
        for (const auto &tool : relevantTools) {
            names.push_back(tool.name);
        }
        return names;
    }();

    QList<AgentTask> tasks;
    for (const AgentToolCall &call : parseAdapterToolCalls(json)) {
        const QJsonDocument argsDocument = QJsonDocument::fromJson(call.argumentsJson.toUtf8());
        if (!call.argumentsJson.trimmed().isEmpty() && !argsDocument.isObject()) {
            appendAgentTrace(QStringLiteral("validation"),
                             QStringLiteral("Rejected tool call"),
                             QStringLiteral("Tool call %1 returned invalid arguments JSON.").arg(call.name),
                             false);
            continue;
        }
        QJsonObject args = argsDocument.object();
        AgentTask task;
        task.type = call.name;
        task.args = args;
        task.priority = 50;
        if (!allowedTaskTypes.isEmpty() && !allowedTaskTypes.contains(task.type)) {
            appendAgentTrace(QStringLiteral("validation"),
                             QStringLiteral("Rejected background task"),
                             QStringLiteral("Task type %1 is not allowed for this intent.").arg(task.type),
                             false);
            continue;
        }
        tasks.push_back(task);
    }

    if (intentRequiresTool(returnedIntent) && tasks.isEmpty()) {
        appendAgentTrace(QStringLiteral("validation"),
                         QStringLiteral("Hybrid payload rejected"),
                         QStringLiteral("A tool-backed intent was returned without a valid task."),
                         false);
        handleConversationFinished(message.isEmpty()
                                       ? QStringLiteral("I need to use a tool for that, but I didn't get a valid task back. Please try again with the path or target.")
                                       : message);
        return;
    }

    dispatchBackgroundTasks(tasks);

    const QString effectiveMessage = message.isEmpty()
        ? QStringLiteral("Done. Any background results will appear in the panel.")
        : message;

    const SpokenReply reply = parseSpokenReply(effectiveMessage);
    const bool spoke = m_responseFinalizer->finalizeResponse(
        QStringLiteral("agent"),
        reply,
        &m_responseText,
        [this]() { emit responseTextChanged(); },
        [this]() { refreshConversationSession(); },
        [this](const QString &response, const QString &source, const QString &status) { logPromptResponsePair(response, source, status); },
        QStringLiteral("Response ready"),
        [this](const QString &status) { setStatus(status); });

    if (!spoke && !m_ttsEngine->isSpeaking()) {
        setDuplexState(DuplexState::Open);
        if (conversationSessionShouldContinue()) {
            if (!scheduleConversationSessionListening(conversationSessionRestartDelayMs())) {
                endConversationSession();
                scheduleWakeMonitorRestart();
            }
        } else {
            endConversationSession();
            scheduleWakeMonitorRestart();
        }
        emit idleRequested();
    }

    if (m_loggingService) {
        m_loggingService->logAgentExchange(m_lastPromptForAiLog,
                                           reply.displayText,
                                           QStringLiteral("agent"),
                                           m_agentCapabilities,
                                           samplingProfile(),
                                           m_agentTrace,
                                           QStringLiteral("Response ready"));
    }
    m_lastPromptForAiLog.clear();
}

void AssistantController::handleAgentResponse(const AgentResponse &response)
{
    m_previousAgentResponseId = response.responseId;
    appendAgentTrace(QStringLiteral("model"), QStringLiteral("Agent response"),
                     response.toolCalls.isEmpty()
                        ? QStringLiteral("Received final answer")
                        : QStringLiteral("Received %1 tool calls").arg(response.toolCalls.size()),
                     true);

    if (!response.toolCalls.isEmpty()) {
        QList<AgentToolResult> results;
        for (const auto &toolCall : response.toolCalls) {
            appendAgentTrace(QStringLiteral("tool_call"), toolCall.name, toolCall.argumentsJson.left(500), true);
            const AgentToolResult result = m_agentToolbox->execute(toolCall);
            appendAgentTrace(QStringLiteral("tool_result"), result.toolName, result.output.left(800), result.success);
            results.push_back(result);
        }
        continueAgentConversation(results);
        return;
    }

    if (m_settings->memoryAutoWrite()) {
        m_memoryPolicyHandler->captureExplicitMemoryFromInput(m_lastAgentInput);
    }

    const SpokenReply reply = parseSpokenReply(response.outputText);
    const bool spoke = m_responseFinalizer->finalizeResponse(
        QStringLiteral("agent"),
        reply,
        &m_responseText,
        [this]() { emit responseTextChanged(); },
        [this]() { refreshConversationSession(); },
        [this](const QString &responseText, const QString &source, const QString &status) { logPromptResponsePair(responseText, source, status); },
        QStringLiteral("Response ready"),
        [this](const QString &status) { setStatus(status); });
    if (!spoke && !m_ttsEngine->isSpeaking()) {
        setDuplexState(DuplexState::Open);
        if (conversationSessionShouldContinue()) {
            if (!scheduleConversationSessionListening(conversationSessionRestartDelayMs())) {
                endConversationSession();
                scheduleWakeMonitorRestart();
            }
        } else {
            endConversationSession();
            scheduleWakeMonitorRestart();
        }
        emit idleRequested();
    }

    if (m_loggingService) {
        m_loggingService->logAgentExchange(m_lastPromptForAiLog,
                                           reply.displayText,
                                           QStringLiteral("agent"),
                                           m_agentCapabilities,
                                           samplingProfile(),
                                           m_agentTrace,
                                           QStringLiteral("Response ready"));
    }
    m_lastPromptForAiLog.clear();
}

void AssistantController::handleCommandFinished(const QString &text)
{
    const CommandEnvelope command = parseCommand(text);
    if (!command.valid || command.confidence < 0.6f) {
        startConversationRequest(m_transcript);
        return;
    }

    if (!m_deviceManager->canExecuteTarget(command.target)) {
        if (m_settings->agentEnabled()
            && (isExplicitComputerControlQuery(m_transcript)
                || command.target.compare(QStringLiteral("browser"), Qt::CaseInsensitive) == 0
                || command.target.compare(QStringLiteral("computer"), Qt::CaseInsensitive) == 0
                || command.target.compare(QStringLiteral("youtube"), Qt::CaseInsensitive) == 0)) {
            startAgentConversationRequest(m_transcript, IntentType::GENERAL_CHAT);
            return;
        }

        startConversationRequest(m_transcript);
        return;
    }

    const QString result = m_deviceManager->execute(command);
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Command executed. intent=\"%1\" target=\"%2\" action=\"%3\" confidence=%4")
            .arg(command.intent, command.target, command.action)
            .arg(command.confidence, 0, 'f', 2));
    }
    const QString message = m_localResponseEngine->acknowledgement(command.target, buildLocalResponseContext())
        + QStringLiteral(" ")
        + result;
    SpokenReply reply;
    reply.displayText = message;
    reply.spokenText = message;
    reply.shouldSpeak = true;
    m_responseFinalizer->finalizeResponse(
        QStringLiteral("command"),
        reply,
        &m_responseText,
        [this]() { emit responseTextChanged(); },
        [this]() { refreshConversationSession(); },
        [this](const QString &responseText, const QString &source, const QString &status) { logPromptResponsePair(responseText, source, status); },
        QStringLiteral("Command executed"),
        [this](const QString &status) { setStatus(status); });
}

void AssistantController::dispatchBackgroundTasks(const QList<AgentTask> &tasks)
{
    QList<AgentTask> sortedTasks = tasks;
    std::sort(sortedTasks.begin(), sortedTasks.end(), [](const AgentTask &left, const AgentTask &right) {
        return left.priority > right.priority;
    });

    for (AgentTask task : sortedTasks) {
        task.id = m_nextTaskId++;
        task.createdAtMs = QDateTime::currentMSecsSinceEpoch();
        task.state = TaskState::Pending;
        m_knownBackgroundTasks.insert(task.id, task);
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("[TaskDispatcher] created %1 #%2").arg(task.type).arg(task.id));
        }
        m_taskDispatcher->enqueue(task);
    }

    refreshBackgroundTaskSurface();
}

void AssistantController::recordTaskResult(const QJsonObject &resultObject)
{
    BackgroundTaskResult result;
    result.taskId = resultObject.value(QStringLiteral("taskId")).toInt();
    result.type = resultObject.value(QStringLiteral("type")).toString();
    result.success = resultObject.value(QStringLiteral("success")).toBool();
    result.state = static_cast<TaskState>(resultObject.value(QStringLiteral("state")).toInt(static_cast<int>(TaskState::Finished)));
    result.errorKind = static_cast<ToolErrorKind>(resultObject.value(QStringLiteral("errorKind")).toInt(static_cast<int>(ToolErrorKind::Unknown)));
    result.title = resultObject.value(QStringLiteral("title")).toString();
    result.summary = resultObject.value(QStringLiteral("summary")).toString();
    result.detail = resultObject.value(QStringLiteral("detail")).toString();
    result.payload = resultObject.value(QStringLiteral("payload")).toObject();
    result.finishedAt = resultObject.value(QStringLiteral("finishedAt")).toString();
    result.taskKey = resultObject.value(QStringLiteral("taskKey")).toString();

    const int activeTaskId = m_activeBackgroundTaskIds.value(result.type, -1);
    if (activeTaskId != result.taskId) {
        m_knownBackgroundTasks.remove(result.taskId);
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("[UI] ignored stale task id=%1 type=%2 active=%3")
                .arg(result.taskId)
                .arg(result.type)
                .arg(activeTaskId));
        }
        return;
    }

    if (result.state == TaskState::Canceled) {
        m_knownBackgroundTasks.remove(result.taskId);
        if (m_activeBackgroundTaskIds.value(result.type, -1) == result.taskId) {
            m_activeBackgroundTaskIds.remove(result.type);
        }
        refreshBackgroundTaskSurface();
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("[UI] ignored canceled task id=%1 type=%2")
                .arg(result.taskId)
                .arg(result.type));
        }
        return;
    }

    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("[TaskDispatcher] finished %1 #%2")
            .arg(result.type)
            .arg(result.taskId));
    }

    m_knownBackgroundTasks.remove(result.taskId);
    if (m_activeBackgroundTaskIds.value(result.type, -1) == result.taskId) {
        m_activeBackgroundTaskIds.remove(result.type);
    }
    refreshBackgroundTaskSurface();

    m_backgroundTaskResults.prepend(result);
    while (m_backgroundTaskResults.size() > 40) {
        m_backgroundTaskResults.removeLast();
    }
    if (m_backgroundPanelVisible) {
        emit backgroundTaskResultsChanged();
    }

    m_latestTaskToastTaskId = result.taskId;
    m_latestTaskToast = result.summary.isEmpty() ? result.title : result.summary;
    m_latestTaskToastTone = result.success ? QStringLiteral("response") : QStringLiteral("error");
    m_latestTaskToastType = result.type;
    emit latestTaskToastChanged();

    appendAgentTrace(QStringLiteral("tool_result"),
                     result.type,
                     result.detail.left(600),
                     result.success);

    if (result.type == QStringLiteral("web_search") && result.success) {
        startWebSearchSummaryRequest(result);
    }
}

void AssistantController::refreshBackgroundTaskSurface()
{
    int nextTaskId = -1;
    QString nextPrimary;
    QString nextSecondary;
    qint64 nextCreatedAt = -1;

    for (auto it = m_activeBackgroundTaskIds.cbegin(); it != m_activeBackgroundTaskIds.cend(); ++it) {
        const int taskId = it.value();
        const AgentTask task = m_knownBackgroundTasks.value(taskId);
        if (task.id <= 0 || task.type.isEmpty()) {
            continue;
        }

        const auto copy = backgroundTaskSurfaceCopy(task);
        if (copy.first.isEmpty()) {
            continue;
        }

        if (task.createdAtMs >= nextCreatedAt) {
            nextCreatedAt = task.createdAtMs;
            nextTaskId = taskId;
            nextPrimary = copy.first;
            nextSecondary = copy.second;
        }
    }

    if (m_surfaceBackgroundTaskId == nextTaskId
        && m_surfaceBackgroundPrimary == nextPrimary
        && m_surfaceBackgroundSecondary == nextSecondary) {
        return;
    }

    m_surfaceBackgroundTaskId = nextTaskId;
    m_surfaceBackgroundPrimary = nextPrimary;
    m_surfaceBackgroundSecondary = nextSecondary;
    emit assistantSurfaceChanged();
}

QPair<QString, QString> AssistantController::backgroundTaskSurfaceCopy(const AgentTask &task) const
{
    const QJsonObject &args = task.args;
    const QString type = task.type.trimmed().toLower();

    if (type == QStringLiteral("web_search")) {
        return {
            QStringLiteral("Searching the web..."),
            compactSurfaceText(firstNonEmptyArg(args, {QStringLiteral("query"), QStringLiteral("q")}), 64)
        };
    }

    if (type == QStringLiteral("dir_list")) {
        return {
            QStringLiteral("Listing files..."),
            compactPathForSurface(firstNonEmptyArg(args, {QStringLiteral("path"), QStringLiteral("directory"), QStringLiteral("dir")}))
        };
    }

    if (type == QStringLiteral("file_read")) {
        return {
            QStringLiteral("Opening file..."),
            compactPathForSurface(firstNonEmptyArg(args, {QStringLiteral("path"), QStringLiteral("file"), QStringLiteral("filename")}))
        };
    }

    if (type == QStringLiteral("file_write") || type == QStringLiteral("computer_write_file")) {
        return {
            QStringLiteral("Writing file..."),
            compactPathForSurface(firstNonEmptyArg(args, {QStringLiteral("path"), QStringLiteral("file"), QStringLiteral("filename")}))
        };
    }

    if (type == QStringLiteral("memory_write")) {
        return {
            QStringLiteral("Saving memory..."),
            compactSurfaceText(firstNonEmptyArg(args, {QStringLiteral("summary"), QStringLiteral("title"), QStringLiteral("memory"), QStringLiteral("content")}), 60)
        };
    }

    if (type == QStringLiteral("computer_open_app")) {
        return {
            QStringLiteral("Opening app..."),
            compactSurfaceText(firstNonEmptyArg(args, {QStringLiteral("app"), QStringLiteral("name"), QStringLiteral("application")}), 48)
        };
    }

    if (type == QStringLiteral("computer_open_url") || type == QStringLiteral("browser_open")) {
        return {
            QStringLiteral("Opening link..."),
            compactUrlForSurface(firstNonEmptyArg(args, {QStringLiteral("url"), QStringLiteral("link")}))
        };
    }

    if (type == QStringLiteral("computer_set_timer")) {
        QString secondary;
        const int seconds = args.value(QStringLiteral("seconds")).toInt();
        if (seconds > 0) {
            secondary = formatDurationForSurface(seconds);
        } else {
            secondary = compactSurfaceText(firstNonEmptyArg(args, {QStringLiteral("duration"), QStringLiteral("label")}), 48);
        }
        return {QStringLiteral("Setting timer..."), secondary};
    }

    return {
        QStringLiteral("Tool running..."),
        compactSurfaceText(firstNonEmptyArg(args, {QStringLiteral("query"), QStringLiteral("path"), QStringLiteral("file"), QStringLiteral("url"), QStringLiteral("name")}), 56)
    };
}

void AssistantController::setSurfaceError(const QString &source, const QString &primary, const QString &secondary)
{
    const QString normalizedPrimary = compactSurfaceText(primary);
    const QString normalizedSecondary = compactSurfaceText(secondary, 56);
    if (normalizedPrimary.isEmpty()) {
        clearSurfaceError(source);
        return;
    }

    if (m_surfaceErrorSource == source
        && m_surfaceErrorPrimary == normalizedPrimary
        && m_surfaceErrorSecondary == normalizedSecondary) {
        return;
    }

    m_surfaceErrorSource = source;
    m_surfaceErrorPrimary = normalizedPrimary;
    m_surfaceErrorSecondary = normalizedSecondary;
    emit assistantSurfaceChanged();
}

void AssistantController::clearSurfaceError(const QString &source)
{
    if (!source.isEmpty() && m_surfaceErrorSource != source) {
        return;
    }

    if (m_surfaceErrorSource.isEmpty()
        && m_surfaceErrorPrimary.isEmpty()
        && m_surfaceErrorSecondary.isEmpty()) {
        return;
    }

    m_surfaceErrorSource.clear();
    m_surfaceErrorPrimary.clear();
    m_surfaceErrorSecondary.clear();
    emit assistantSurfaceChanged();
}

void AssistantController::startWebSearchSummaryRequest(const BackgroundTaskResult &result)
{
    const QString query = result.payload.value(QStringLiteral("query")).toString().trimmed();
    const QString provider = result.payload.value(QStringLiteral("provider")).toString().trimmed();
    const QString content = result.payload.value(QStringLiteral("content")).toString();
    const bool reliable = result.payload.value(QStringLiteral("reliable")).toBool(true);
    const QString reliabilityReason = result.payload.value(QStringLiteral("reliability_reason")).toString().trimmed();
    if (query.isEmpty() || content.trimmed().isEmpty()) {
        return;
    }

    if (!reliable) {
        deliverLocalResponse(
            QStringLiteral("I couldn't verify reliable web sources for that yet. Please try a more specific query or ask for source details."),
            reliabilityReason.isEmpty() ? QStringLiteral("Web search low confidence") : reliabilityReason,
            true);
        return;
    }

    const QString clippedContent = content.left(12000);
    const bool wantsDetails = asksForDetailedAnswer(query);

    const QString synthesisInput = wantsDetails
        ? QStringLiteral(
              "You previously asked me to search the web. "
              "Provide the final answer using only the fetched payload below. "
              "Keep it concise, accurate, and include uncertainty only if needed. "
              "Do not claim hidden browsing beyond this data.\n\n"
              "User query: %1\n"
              "Search provider: %2\n"
              "Fetched payload (JSON/text):\n%3")
              .arg(query, provider.isEmpty() ? QStringLiteral("unknown") : provider, clippedContent)
        : QStringLiteral(
              "You previously asked me to search the web. "
              "Return ONLY the direct answer from the fetched payload below. "
              "Do not provide explanations, context, caveats, or extra sentences unless the user asked for details. "
              "Use one short sentence, ideally 4-12 words.\n\n"
              "User query: %1\n"
              "Search provider: %2\n"
              "Fetched payload (JSON/text):\n%3")
              .arg(query, provider.isEmpty() ? QStringLiteral("unknown") : provider, clippedContent);

    m_lastPromptForAiLog = query;
    startConversationRequest(synthesisInput);
}

QStringList AssistantController::backgroundAllowedRoots() const
{
    return {
        QDir::cleanPath(QDir::currentPath()),
        QDir::cleanPath(QDir::currentPath() + QStringLiteral("/config")),
        QDir::cleanPath(QDir::currentPath() + QStringLiteral("/bin/logs")),
        QDir::cleanPath(QDir::currentPath() + QStringLiteral("/skills")),
        QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
    };
}

void AssistantController::logPromptResponsePair(const QString &response, const QString &source, const QString &status)
{
    if (!m_loggingService) {
        return;
    }

    const QString prompt = m_lastPromptForAiLog.trimmed().isEmpty()
        ? QStringLiteral("[no prompt captured]")
        : m_lastPromptForAiLog.trimmed();

    const bool ok = m_loggingService->logAiExchange(prompt, response, source, status);
    if (!ok) {
        m_loggingService->warn(QStringLiteral("Failed to write AI exchange log file."));
    }

    m_lastPromptForAiLog.clear();
}

void AssistantController::appendAgentTrace(const QString &kind, const QString &title, const QString &detail, bool success)
{
    m_agentTrace.push_back({
        .timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
        .kind = kind,
        .title = title,
        .detail = detail,
        .success = success
    });
    while (m_agentTrace.size() > 200) {
        m_agentTrace.pop_front();
    }
    emit agentTraceChanged();
}

CommandEnvelope AssistantController::parseCommand(const QString &payload) const
{
    const auto json = nlohmann::json::parse(payload.toStdString(), nullptr, false);
    if (json.is_discarded()) {
        return {};
    }

    CommandEnvelope command;
    command.intent = QString::fromStdString(json.value("intent", std::string{}));
    command.target = QString::fromStdString(json.value("target", std::string{}));
    command.action = QString::fromStdString(json.value("action", std::string{}));
    command.confidence = json.value("confidence", 0.0f);
    command.args = json.contains("args") ? json.at("args") : nlohmann::json::object();
    command.valid = !command.intent.isEmpty() && command.intent != QStringLiteral("unknown");
    return command;
}
