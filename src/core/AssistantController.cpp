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
#include "core/AssistantBehaviorPolicy.h"
#include "core/ExecutionNarrator.h"
#include "core/InputRouter.h"
#include "core/IntentRouter.h"
#include "core/LocalResponseEngine.h"
#include "core/MemoryPolicyHandler.h"
#include "core/PermissionOverrideSettings.h"
#include "core/ResponseFinalizer.h"
#include "core/ToolCoordinator.h"
#include "behavior_tuning/CompiledContextPolicyTuningPromotionPolicy.h"
#include "behavior_tuning/FeedbackSignalEventBuilder.h"
#include "connectors/BrowserBookmarksMonitor.h"
#include "connectors/CalendarIcsMonitor.h"
#include "connectors/ConnectorSnapshotMonitor.h"
#include "connectors/InboxMaildropMonitor.h"
#include "connectors/NotesDirectoryMonitor.h"
#include "cognition/CompiledContextDeltaTracker.h"
#include "cognition/CompiledContextHistoryPolicy.h"
#include "cognition/CompiledContextHistorySummaryBuilder.h"
#include "cognition/CompiledContextLayeredSignalBuilder.h"
#include "cognition/CompiledContextPolicyTuningSignalBuilder.h"
#include "cognition/CompiledContextStabilityMemoryBuilder.h"
#include "cognition/CompiledContextStabilityTracker.h"
#include "cognition/PromptContextTemporalPolicy.h"
#include "cognition/ProactiveCooldownTracker.h"
#include "cognition/SelectionContextCompiler.h"
#include "cognition/ConnectorHistoryTracker.h"
#include "cognition/ConnectorResultSignal.h"
#include "cognition/ProactiveSuggestionGate.h"
#include "cognition/ProactiveSuggestionPlanner.h"
#include "core/tasks/TaskDispatcher.h"
#include "core/tasks/ToolWorker.h"
#include "cognition/DesktopContextSelectionBuilder.h"
#include "core/ActionRiskPermissionService.h"
#include "cognition/ProactiveSurfaceGate.h"
#include "devices/DeviceManager.h"
#include "logging/LoggingService.h"
#include "memory/MemoryStore.h"
#include "settings/AppSettings.h"
#include "settings/IdentityProfileService.h"
#include "skills/SkillStore.h"
#include "stt/RuntimeSpeechRecognizer.h"
#include "telemetry/SelectionTelemetryBuilder.h"
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

QString routeKindToString(InputRouteKind kind)
{
    switch (kind) {
    case InputRouteKind::None:
        return QStringLiteral("none");
    case InputRouteKind::LocalResponse:
        return QStringLiteral("local_response");
    case InputRouteKind::DeterministicTasks:
        return QStringLiteral("deterministic_tasks");
    case InputRouteKind::BackgroundTasks:
        return QStringLiteral("background_tasks");
    case InputRouteKind::Conversation:
        return QStringLiteral("conversation");
    case InputRouteKind::AgentConversation:
        return QStringLiteral("agent_conversation");
    case InputRouteKind::CommandExtraction:
        return QStringLiteral("command_extraction");
    case InputRouteKind::AgentCapabilityError:
        return QStringLiteral("agent_capability_error");
    }
    return QStringLiteral("none");
}

QString intentTypeToString(IntentType intent)
{
    switch (intent) {
    case IntentType::LIST_FILES:
        return QStringLiteral("list_files");
    case IntentType::READ_FILE:
        return QStringLiteral("read_file");
    case IntentType::WRITE_FILE:
        return QStringLiteral("write_file");
    case IntentType::MEMORY_WRITE:
        return QStringLiteral("memory_write");
    case IntentType::GENERAL_CHAT:
    default:
        return QStringLiteral("general_chat");
    }
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

MemoryContext fallbackMemoryContext(const QList<MemoryRecord> &memory)
{
    MemoryContext context;
    context.episodic = memory;
    return context;
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

bool isLowSignalConversationToken(const QString &token)
{
    static const QSet<QString> lowSignalTokens = {
        QStringLiteral("you"),
        QStringLiteral("your"),
        QStringLiteral("it"),
        QStringLiteral("its"),
        QStringLiteral("this"),
        QStringLiteral("that"),
        QStringLiteral("there"),
        QStringLiteral("tip"),
        QStringLiteral("uh"),
        QStringLiteral("um"),
        QStringLiteral("hmm"),
        QStringLiteral("hm"),
        QStringLiteral("ah"),
        QStringLiteral("oh"),
        QStringLiteral("yeah"),
        QStringLiteral("yep")
    };
    return lowSignalTokens.contains(token);
}

bool allWordsLowSignal(const QStringList &words)
{
    if (words.isEmpty()) {
        return true;
    }

    for (const QString &word : words) {
        if (!isLowSignalConversationToken(word)) {
            return false;
        }
    }

    return true;
}

QString clippedBackgroundPayload(const BackgroundTaskResult &result, int maxChars = 12000)
{
    QString content = result.payload.value(QStringLiteral("content")).toString().trimmed();
    if (content.isEmpty()) {
        content = result.payload.value(QStringLiteral("text")).toString().trimmed();
    }
    if (content.isEmpty()) {
        content = result.payload.value(QStringLiteral("summary")).toString().trimmed();
    }
    if (content.isEmpty()) {
        const QString detail = result.detail.trimmed();
        if (!detail.isEmpty()) {
            content = detail;
        }
    }
    if (content.isEmpty()) {
        const QString summary = result.summary.trimmed();
        if (!summary.isEmpty()) {
            content = summary;
        }
    }
    if (content.isEmpty() && !result.payload.isEmpty()) {
        content = QString::fromUtf8(QJsonDocument(result.payload).toJson(QJsonDocument::Compact));
    }
    if (content.size() > maxChars) {
        content = content.left(maxChars);
    }
    return content;
}

QStringList sourceUrlsForResult(const BackgroundTaskResult &result)
{
    QStringList urls;
    const QJsonArray sources = result.payload.value(QStringLiteral("sources")).toArray();
    for (const QJsonValue &value : sources) {
        const QString url = value.toObject().value(QStringLiteral("url")).toString().trimmed();
        if (!url.isEmpty() && !urls.contains(url)) {
            urls.push_back(url);
        }
    }
    return urls;
}

QString taskTypeForTasks(const QList<AgentTask> &tasks)
{
    return tasks.isEmpty() ? QStringLiteral("task") : tasks.first().type.trimmed();
}

bool isConnectorLikeTaskType(const QString &taskType)
{
    const QString normalized = taskType.trimmed().toLower();
    return normalized.contains(QStringLiteral("calendar"))
        || normalized.contains(QStringLiteral("schedule"))
        || normalized.contains(QStringLiteral("meeting"))
        || normalized.contains(QStringLiteral("timer"))
        || normalized.contains(QStringLiteral("reminder"))
        || normalized.contains(QStringLiteral("email"))
        || normalized.contains(QStringLiteral("mail"))
        || normalized.contains(QStringLiteral("message"))
        || normalized.contains(QStringLiteral("inbox"))
        || normalized.contains(QStringLiteral("note"))
        || normalized.contains(QStringLiteral("memo"))
        || normalized.contains(QStringLiteral("draft"));
}

bool isEligibleTaskResultSuggestion(const BackgroundTaskResult &result)
{
    if (ConnectorResultSignalBuilder::fromBackgroundTaskResult(result).isValid()) {
        return true;
    }

    const QString normalized = result.type.trimmed().toLower();
    if (normalized == QStringLiteral("web_search")
        || normalized == QStringLiteral("web_fetch")
        || normalized == QStringLiteral("browser_fetch_text")
        || normalized == QStringLiteral("file_read")
        || normalized == QStringLiteral("file_search")
        || normalized == QStringLiteral("dir_list")) {
        return true;
    }

    if (isConnectorLikeTaskType(normalized)) {
        return true;
    }

    return !sourceUrlsForResult(result).isEmpty();
}

QString userFacingPromptForLogging(const QString &input)
{
    const QString trimmed = input.trimmed();
    if (!trimmed.startsWith(QStringLiteral("You previously asked me to search the web."), Qt::CaseInsensitive)) {
        const bool actionThreadFollowUp = trimmed.startsWith(QStringLiteral("You are continuing the current assistant action thread."), Qt::CaseInsensitive);
        const bool actionThreadCompletion = trimmed.startsWith(QStringLiteral("A task just completed."), Qt::CaseInsensitive);
        if (!actionThreadFollowUp && !actionThreadCompletion) {
            return trimmed;
        }

        const QRegularExpression followUpPattern(QStringLiteral("User follow-up:\\s*(.+?)(?:\\n|$)"), QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch followUpMatch = followUpPattern.match(trimmed);
        if (followUpMatch.hasMatch()) {
            return followUpMatch.captured(1).trimmed();
        }

        return actionThreadCompletion
            ? QStringLiteral("[action thread completion]")
            : QStringLiteral("[action thread follow-up]");
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

bool shouldUseDesktopContextForPrompt(const QString &input, IntentType intent)
{
    const QString lowered = input.trimmed().toLower();
    if (intent != IntentType::GENERAL_CHAT) {
        return true;
    }

    return lowered.contains(QStringLiteral("this"))
        || lowered.contains(QStringLiteral("current"))
        || lowered.contains(QStringLiteral("here"))
        || lowered.contains(QStringLiteral("tab"))
        || lowered.contains(QStringLiteral("page"))
        || lowered.contains(QStringLiteral("window"))
        || lowered.contains(QStringLiteral("file"))
        || lowered.contains(QStringLiteral("document"))
        || lowered.contains(QStringLiteral("clipboard"))
        || lowered.contains(QStringLiteral("copied"));
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
            call.id = QString::fromStdString(callJson.value("id", std::string{}));
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
        call.id = QString::fromStdString(taskJson.value("id", std::string{}));
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
            *spoken = QStringLiteral("I'm opening those YouTube results now.");
            return true;
        }
    }

    if (lowered.contains(QStringLiteral("open youtube")) || lowered == QStringLiteral("youtube")) {
        task->type = QStringLiteral("browser_open");
        task->args = QJsonObject{{QStringLiteral("url"), QStringLiteral("https://www.youtube.com")}};
        task->priority = 85;
        *spoken = QStringLiteral("I'm opening YouTube now.");
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
        *spoken = QStringLiteral("I'm setting that timer now.");
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
        *spoken = QStringLiteral("I'm creating that file now.");
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
            *spoken = QStringLiteral("I'm opening those search results now.");
        } else {
            task->args = QJsonObject{{QStringLiteral("url"), QStringLiteral("https://www.google.com")}};
            *spoken = QStringLiteral("I'm opening the browser now.");
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
    m_memoryStore = new MemoryStore(QString(), this);
    m_assistantBehaviorPolicy = std::make_unique<AssistantBehaviorPolicy>();
    m_inputRouter = std::make_unique<InputRouter>(m_assistantBehaviorPolicy.get());
    m_aiRequestCoordinator = std::make_unique<AiRequestCoordinator>(m_settings, m_reasoningRouter, m_loggingService);
    m_executionNarrator = std::make_unique<ExecutionNarrator>();
    m_memoryPolicyHandler = std::make_unique<MemoryPolicyHandler>(m_identityProfileService, m_memoryStore);
    m_toolCoordinator = std::make_unique<ToolCoordinator>(m_loggingService, m_executionNarrator.get());
    m_skillStore = new SkillStore(this);
    m_agentToolbox = new AgentToolbox(m_settings, m_memoryStore, m_skillStore, m_loggingService, this);
    m_deviceManager = new DeviceManager(this);
    m_intentEngine = new IntentEngine(m_settings, m_loggingService, this);
    m_backgroundIntentDetector = new IntentDetector(this);
    m_intentRouter = new IntentRouter(this);
    m_localResponseEngine = new LocalResponseEngine(this);
    m_taskDispatcher = new TaskDispatcher(m_loggingService, this);
    m_toolWorker = new ToolWorker(backgroundAllowedRoots(), m_loggingService, m_settings);
    m_browserBookmarksMonitor = new BrowserBookmarksMonitor(this);
    m_calendarIcsMonitor = new CalendarIcsMonitor(this);
    m_connectorSnapshotMonitor = new ConnectorSnapshotMonitor(m_settings, m_loggingService, this);
    m_inboxMaildropMonitor = new InboxMaildropMonitor(this);
    m_notesDirectoryMonitor = new NotesDirectoryMonitor(this);
    m_whisperSttEngine = new RuntimeSpeechRecognizer(m_voicePipelineRuntime, this);
    m_ttsEngine = new WorkerTtsEngine(m_voicePipelineRuntime, this);
    m_responseFinalizer = std::make_unique<ResponseFinalizer>(m_memoryStore, m_ttsEngine, m_loggingService);
    m_connectorHistoryTracker = std::make_unique<ConnectorHistoryTracker>(m_memoryStore);
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
        if (m_toolCoordinator->handleTaskCanceled(taskId)) {
            emit assistantSurfaceChanged();
        }
    });
    connect(m_taskDispatcher, &TaskDispatcher::activeTaskChanged, this, [this](const QString &taskKey, int taskId) {
        if (m_toolCoordinator->handleTaskActivated(taskKey, taskId)) {
            emit assistantSurfaceChanged();
        }
    });
    connect(m_toolWorker, &ToolWorker::connectorEventReady, m_taskDispatcher, &TaskDispatcher::handleConnectorEvent, Qt::QueuedConnection);
    connect(m_toolWorker, &ToolWorker::taskStarted, m_taskDispatcher, &TaskDispatcher::handleTaskStarted, Qt::QueuedConnection);
    connect(m_toolWorker, &ToolWorker::taskFinished, m_taskDispatcher, &TaskDispatcher::handleTaskFinished, Qt::QueuedConnection);
    connect(m_taskDispatcher, &TaskDispatcher::connectorEventReady, this, [this](const ConnectorEvent &event) {
        recordConnectorEvent(event);
    }, Qt::QueuedConnection);
    connect(m_taskDispatcher, &TaskDispatcher::taskResultReady, this, [this](const QJsonObject &resultObject) {
        recordTaskResult(resultObject);
    }, Qt::QueuedConnection);
    connect(m_browserBookmarksMonitor, &BrowserBookmarksMonitor::connectorEventDetected, this, [this](const ConnectorEvent &event) {
        recordConnectorEvent(event);
    }, Qt::QueuedConnection);
    connect(m_calendarIcsMonitor, &CalendarIcsMonitor::connectorEventDetected, this, [this](const ConnectorEvent &event) {
        recordConnectorEvent(event);
    }, Qt::QueuedConnection);
    connect(m_connectorSnapshotMonitor, &ConnectorSnapshotMonitor::connectorEventDetected, this, [this](const ConnectorEvent &event) {
        recordConnectorEvent(event);
    }, Qt::QueuedConnection);
    connect(m_inboxMaildropMonitor, &InboxMaildropMonitor::connectorEventDetected, this, [this](const ConnectorEvent &event) {
        recordConnectorEvent(event);
    }, Qt::QueuedConnection);
    connect(m_notesDirectoryMonitor, &NotesDirectoryMonitor::connectorEventDetected, this, [this](const ConnectorEvent &event) {
        recordConnectorEvent(event);
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
    if (m_browserBookmarksMonitor != nullptr) {
        m_browserBookmarksMonitor->start();
    }
    if (m_calendarIcsMonitor != nullptr) {
        m_calendarIcsMonitor->start();
    }
    if (m_connectorSnapshotMonitor != nullptr) {
        m_connectorSnapshotMonitor->start();
    }
    if (m_inboxMaildropMonitor != nullptr) {
        m_inboxMaildropMonitor->start();
    }
    if (m_notesDirectoryMonitor != nullptr) {
        m_notesDirectoryMonitor->start();
    }
    if (!m_gestureActionRouterThread.isRunning()) {
        m_gestureActionRouterThread.start();
    }
    reconfigureGestureActionRouter();
    if (m_visionIngestService) {
        m_visionIngestService->start();
    }
    m_voicePipelineRuntime->start();
    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("route_audit"),
            QStringLiteral("[ai_provider_config] action=initialize provider=%1 endpoint=%2 apiKeyConfigured=%3")
                .arg(m_settings->chatBackendKind().trimmed().toLower(),
                     m_settings->chatBackendEndpoint().trimmed(),
                     m_settings->chatBackendApiKey().trimmed().isEmpty() ? QStringLiteral("false") : QStringLiteral("true")));
    }
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
            deliverLocalResponse(
                m_executionNarrator->clarificationPrompt(m_activeActionSession),
                QStringLiteral("Please repeat"),
                true);
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
            m_loggingService->info(QStringLiteral("Local AI backend request started. requestId=%1 kind=%2 provider=%3 endpoint=%4")
                .arg(requestId)
                .arg(m_activeRequestKind == RequestKind::CommandExtraction
                         ? QStringLiteral("command")
                         : (m_activeRequestKind == RequestKind::AgentConversation
                                ? QStringLiteral("agent")
                                : QStringLiteral("conversation"))
                .arg(m_settings->chatBackendKind().trimmed().toLower(),
                     m_settings->chatBackendEndpoint().trimmed()));
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
        if (m_loggingService) {
            m_loggingService->infoFor(
                QStringLiteral("route_audit"),
                QStringLiteral("[ai_provider_capabilities] provider=%1 endpoint=%2 responsesApi=%3 toolCalling=%4 selectedModelToolCapable=%5 effectiveMode=%6 status=%7")
                    .arg(m_settings->chatBackendKind().trimmed().toLower(),
                         m_settings->chatBackendEndpoint().trimmed(),
                         m_agentCapabilities.responsesApi ? QStringLiteral("true") : QStringLiteral("false"),
                         m_agentCapabilities.toolCalling ? QStringLiteral("true") : QStringLiteral("false"),
                         m_agentCapabilities.selectedModelToolCapable ? QStringLiteral("true") : QStringLiteral("false"),
                         m_agentCapabilities.providerMode,
                         m_agentCapabilities.status.simplified()));
        }
        emit agentStateChanged();
    });
    connect(m_aiBackendClient, &AiBackendClient::requestFinished, this, [this](quint64 requestId, const QString &fullText) {
        if (requestId != m_activeRequestId) {
            return;
        }

        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("Local AI backend request finished. requestId=%1 chars=%2 provider=%3 endpoint=%4")
                .arg(requestId)
                .arg(fullText.size())
                .arg(m_settings->chatBackendKind().trimmed().toLower(),
                     m_settings->chatBackendEndpoint().trimmed()));
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
                m_loggingService->error(QStringLiteral("Local AI backend request failed. requestId=%1 provider=%2 endpoint=%3 error=\"%4\"")
                    .arg(QString::number(requestId),
                         m_settings->chatBackendKind().trimmed().toLower(),
                         m_settings->chatBackendEndpoint().trimmed(),
                         errorText));
            }
            const QString errorGroup = m_aiRequestCoordinator->errorGroupFor(errorText);
            refreshConversationSession();
            setSurfaceError(QStringLiteral("assistant"), compactSurfaceText(errorText));
            deliverLocalResponse(
                m_localResponseEngine->respondToError(
                    errorGroup,
                    buildLocalResponseContext(),
                    m_activeActionSession.responseMode),
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
    if (m_toolCoordinator != nullptr
        && m_toolCoordinator->surfaceBackgroundTaskId() >= 0
        && !m_toolCoordinator->surfaceBackgroundPrimary().trimmed().isEmpty()) {
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
    return m_toolCoordinator ? m_toolCoordinator->surfaceBackgroundPrimary() : QString{};
}

QString AssistantController::assistantSurfaceActivitySecondary() const
{
    if (!m_surfaceErrorPrimary.trimmed().isEmpty()) {
        return m_surfaceErrorSecondary;
    }
    return m_toolCoordinator ? m_toolCoordinator->surfaceBackgroundSecondary() : QString{};
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
QList<BackgroundTaskResult> AssistantController::backgroundTaskResults() const { return m_toolCoordinator ? m_toolCoordinator->backgroundTaskResults() : QList<BackgroundTaskResult>{}; }
bool AssistantController::backgroundPanelVisible() const { return m_backgroundPanelVisible; }
QString AssistantController::latestTaskToast() const { return m_toolCoordinator ? m_toolCoordinator->latestTaskToast() : QString{}; }
QString AssistantController::latestTaskToastTone() const { return m_toolCoordinator ? m_toolCoordinator->latestTaskToastTone() : QStringLiteral("status"); }
int AssistantController::latestTaskToastTaskId() const { return m_toolCoordinator ? m_toolCoordinator->latestTaskToastTaskId() : -1; }
QString AssistantController::latestTaskToastType() const { return m_toolCoordinator ? m_toolCoordinator->latestTaskToastType() : QStringLiteral("background"); }
QString AssistantController::latestProactiveSuggestion() const { return m_latestProactiveSuggestion; }
QString AssistantController::latestProactiveSuggestionTone() const { return m_latestProactiveSuggestionTone; }
QString AssistantController::latestProactiveSuggestionType() const { return m_latestProactiveSuggestionType; }
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
    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("route_audit"),
            QStringLiteral("[ai_provider_config] action=refresh_models provider=%1 endpoint=%2 apiKeyConfigured=%3")
                .arg(m_settings->chatBackendKind().trimmed().toLower(),
                     m_settings->chatBackendEndpoint().trimmed(),
                     m_settings->chatBackendApiKey().trimmed().isEmpty() ? QStringLiteral("false") : QStringLiteral("true")));
    }
    m_aiBackendClient->setProviderConfig(m_settings->chatBackendKind(), m_settings->chatBackendApiKey());
    m_aiBackendClient->setEndpoint(m_settings->chatBackendEndpoint());
    m_modelCatalogService->refresh();
}

QString AssistantController::prepareSubmitInput(const QString &trimmed, bool *wakeDetected) const
{
    const bool detected = WakeWordDetector::isWakeWordDetected(trimmed);
    if (wakeDetected != nullptr) {
        *wakeDetected = detected;
    }

    return detected
        ? normalizeForRouting(WakeWordDetector::stripWakeWordPrefix(trimmed))
        : trimmed;
}

InputRouterContext AssistantController::buildInputRouteContext(const QString &routedInput,
                                                              const IntentResult &detectedIntent,
                                                              const IntentResult &effectiveIntent,
                                                              LocalIntent localIntent,
                                                              const AiAvailability &availability,
                                                              bool visionRelevantQuery,
                                                              bool wakeOnly,
                                                              bool shouldEndConversation,
                                                              bool isTimeQuery,
                                                              bool isDateQuery,
                                                              bool hasDeterministicTask,
                                                              const AgentTask &deterministicTask,
                                                              const QString &deterministicSpoken,
                                                              qint64 nowMs) const
{
    Q_UNUSED(nowMs);

    QString extractedWebQuery;
    if (!visionRelevantQuery) {
        extractedWebQuery = extractWebSearchQuery(routedInput);
        if (isWebSearchVerificationQuery(routedInput)) {
            extractedWebQuery = defaultWebSearchProbeQuery();
        }
        if (extractedWebQuery.isEmpty()) {
            extractedWebQuery = routedInput;
        }
    }

    InputRouterContext routeContext;
    routeContext.wakeOnly = wakeOnly;
    routeContext.shouldEndConversation = shouldEndConversation;
    routeContext.isTimeQuery = isTimeQuery;
    routeContext.isDateQuery = isDateQuery;
    routeContext.hasDeterministicTask = hasDeterministicTask;
    routeContext.deterministicTask = deterministicTask;
    routeContext.deterministicSpoken = deterministicSpoken;
    routeContext.backgroundIntentReady = detectedIntent.confidence > 0.8f && !detectedIntent.tasks.isEmpty();
    routeContext.backgroundTasks = detectedIntent.tasks;
    routeContext.backgroundSpokenMessage = detectedIntent.spokenMessage;
    routeContext.localIntent = localIntent;
    routeContext.visionRelevant = visionRelevantQuery;
    routeContext.directVisionResponse = visionRelevantQuery && !isExplicitComputerControlQuery(routedInput)
        ? buildDirectVisionResponse(routedInput)
        : QString{};
    routeContext.aiAvailable = availability.online && availability.modelAvailable;
    routeContext.explicitToolInventory = isExplicitToolInventoryQuery(routedInput);
    routeContext.toolInventoryText = groundedToolInventoryText(m_agentToolbox->builtInTools(), m_settings);
    routeContext.explicitWebSearch = !visionRelevantQuery && isExplicitWebSearchQuery(routedInput);
    routeContext.explicitWebQuery = extractedWebQuery;
    routeContext.freshnessCode = freshnessCodeForQuery(routedInput);
    routeContext.likelyKnowledgeLookup = !visionRelevantQuery && m_settings->agentEnabled() && isLikelyKnowledgeLookupQuery(routedInput);
    routeContext.freshnessSensitive = !visionRelevantQuery && m_settings->agentEnabled() && isFreshnessSensitiveQuery(routedInput);
    routeContext.agentEnabled = m_settings->agentEnabled();
    routeContext.explicitAgentWorldQuery = isExplicitAgentWorldQuery(routedInput);
    routeContext.likelyCommand = m_reasoningRouter->isLikelyCommand(routedInput);
    routeContext.effectiveIntent = effectiveIntent.type;
    routeContext.effectiveIntentConfidence = effectiveIntent.confidence;
    routeContext.explicitComputerControl = isExplicitComputerControlQuery(routedInput);
    return routeContext;
}

bool AssistantController::executeRouteDecision(const InputRouteDecision &decision,
                                               const QString &routedInput,
                                               LocalIntent localIntent,
                                               bool confirmationGranted,
                                               qint64 nowMs)
{
    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("route_audit"),
            QStringLiteral("[route_execute] kind=%1 intent=%2 speak=%3 useVisionContext=%4 tasks=%5 status=%6 input=%7")
                .arg(routeKindToString(decision.kind),
                     intentTypeToString(decision.intent),
                     decision.speak ? QStringLiteral("true") : QStringLiteral("false"),
                     decision.useVisionContext ? QStringLiteral("true") : QStringLiteral("false"),
                     QString::number(decision.tasks.size()),
                     decision.status.simplified(),
                     userFacingPromptForLogging(routedInput).left(360)));
    }

    const QList<AgentToolSpec> availableTools = m_agentToolbox ? m_agentToolbox->builtInTools() : QList<AgentToolSpec>{};
    const QString selectionInput = buildDesktopSelectionInput(
        routedInput,
        decision.intent,
        QStringLiteral("route.tool_plan"));
    const ToolPlan toolPlan = m_assistantBehaviorPolicy
        ? m_assistantBehaviorPolicy->buildToolPlan(selectionInput, decision.intent, availableTools)
        : ToolPlan{};
    TrustDecision trustDecision = m_assistantBehaviorPolicy
        ? m_assistantBehaviorPolicy->assessTrust(routedInput, decision, toolPlan, m_latestDesktopContext)
        : TrustDecision{};
    if (confirmationGranted) {
        trustDecision.requiresConfirmation = false;
        trustDecision.userMessage.clear();
    }
    const ActionRiskPermissionEvaluation riskPermissionEvaluation =
        ActionRiskPermissionService::evaluate(
            toolPlan,
            trustDecision,
            confirmationGranted,
            PermissionOverrideSettings::rulesFromSettings(m_settings));

    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("safety_audit"),
            QStringLiteral("[trust_decision] kind=%1 highRisk=%2 requiresConfirmation=%3 reason=%4 userMessage=%5 sideEffecting=%6 requiresGrounding=%7 desktopWorkMode=%8 contextReason=%9")
                .arg(routeKindToString(decision.kind),
                     trustDecision.highRisk ? QStringLiteral("true") : QStringLiteral("false"),
                     trustDecision.requiresConfirmation ? QStringLiteral("true") : QStringLiteral("false"),
                     trustDecision.reason.simplified(),
                     trustDecision.userMessage.simplified(),
                     toolPlan.sideEffecting ? QStringLiteral("true") : QStringLiteral("false"),
                     toolPlan.requiresGrounding ? QStringLiteral("true") : QStringLiteral("false"),
                     trustDecision.desktopWorkMode.simplified(),
                     trustDecision.contextReasonCode.simplified()));
        m_loggingService->logBehaviorEvent(ActionRiskPermissionService::riskEvent(
            riskPermissionEvaluation,
            routeKindToString(decision.kind),
            m_latestDesktopContext));
        m_loggingService->logBehaviorEvent(ActionRiskPermissionService::permissionEvent(
            riskPermissionEvaluation,
            routeKindToString(decision.kind),
            m_latestDesktopContext));
    }

    m_activeActionSession = m_assistantBehaviorPolicy
        ? m_assistantBehaviorPolicy->createActionSession(routedInput, decision, toolPlan, trustDecision, m_latestDesktopContext)
        : ActionSession{};

    if (!confirmationGranted
        && m_assistantBehaviorPolicy
        && m_activeActionSession.trust.requiresConfirmation
        && requiresConfirmationFor(decision)) {
        if (m_loggingService) {
            m_loggingService->warnFor(
                QStringLiteral("safety_audit"),
                QStringLiteral("[confirmation_required] kind=%1 reason=%2 userMessage=%3")
                    .arg(routeKindToString(decision.kind),
                         m_activeActionSession.trust.reason.simplified(),
                         m_activeActionSession.trust.userMessage.simplified()));
        }
        storePendingConfirmation(decision, routedInput, localIntent);
        deliverLocalResponse(
            m_executionNarrator
                ? m_executionNarrator->confirmationPrompt(m_activeActionSession)
                : m_activeActionSession.preamble,
            m_executionNarrator
                ? m_executionNarrator->statusForSession(m_activeActionSession, QStringLiteral("Confirmation needed"))
                : QStringLiteral("Confirmation needed"),
            true);
        return true;
    }

    switch (decision.kind) {
    case InputRouteKind::LocalResponse: {
        if (m_loggingService) {
            m_loggingService->infoFor(QStringLiteral("route_audit"), QStringLiteral("[route_outcome] resolved=local_response"));
        }
        QString response = decision.message;
        const LocalResponseContext context = buildLocalResponseContext();
        if (decision.status == QStringLiteral("Conversation ended")) {
            endConversationSession();
        }
        if (response.isEmpty() && decision.status == QStringLiteral("Listening")) {
            response = m_localResponseEngine->wakeWordReady(context, m_activeActionSession.responseMode);
        } else if (response.isEmpty() && decision.status == QStringLiteral("Conversation ended")) {
            response = m_executionNarrator->gestureReply(QStringLiteral("conversation_end"));
        } else if (response.isEmpty() && decision.status == QStringLiteral("Local time response")) {
            response = m_localResponseEngine->currentTimeResponse(context, m_activeActionSession.responseMode);
        } else if (response.isEmpty() && decision.status == QStringLiteral("Local date response")) {
            response = m_localResponseEngine->currentDateResponse(context, m_activeActionSession.responseMode);
        } else if (response.isEmpty() && (localIntent == LocalIntent::Greeting || localIntent == LocalIntent::SmallTalk)) {
            response = m_localResponseEngine->respondToIntent(localIntent, context, m_activeActionSession.responseMode);
        }
        if (decision.status == QStringLiteral("Vision response")) {
            m_lastVisionQueryMs = nowMs;
        }
        if (response.isEmpty()) {
            response = m_localResponseEngine->respondToError(QStringLiteral("error_invalid"), context, m_activeActionSession.responseMode);
        }
        const QString status = decision.status.isEmpty() && m_executionNarrator
            ? m_executionNarrator->statusForSession(m_activeActionSession, QStringLiteral("Local response"))
            : (decision.status.isEmpty() ? QStringLiteral("Local response") : decision.status);
        deliverLocalResponse(response, status, decision.speak);
        return true;
    }
    case InputRouteKind::AgentCapabilityError:
        if (m_loggingService) {
            m_loggingService->warnFor(QStringLiteral("route_audit"), QStringLiteral("[route_outcome] resolved=agent_capability_error"));
        }
        deliverLocalResponse(
            m_localResponseEngine->respondToError(
                QStringLiteral("ai_offline"),
                buildLocalResponseContext(),
                m_activeActionSession.responseMode),
            QStringLiteral("AI unavailable"),
            true);
        return true;
    case InputRouteKind::BackgroundTasks:
    case InputRouteKind::DeterministicTasks: {
        if (m_loggingService) {
            m_loggingService->infoFor(
                QStringLiteral("route_audit"),
                QStringLiteral("[route_outcome] resolved=background_or_deterministic_tasks taskCount=%1")
                    .arg(QString::number(decision.tasks.size())));
        }
        if (decision.tasks.isEmpty()) {
            return false;
        }
        beginActionThread(decision.tasks, nowMs);
        dispatchBackgroundTasks(decision.tasks);
        const QString fallbackPreamble = decision.message.isEmpty()
            ? QStringLiteral("All right, I'm handling that now.")
            : decision.message;
        const QString preamble = m_executionNarrator
            ? m_executionNarrator->preActionText(m_activeActionSession, fallbackPreamble)
            : fallbackPreamble;
        const QString status = decision.status.isEmpty() && m_executionNarrator
            ? m_executionNarrator->statusForSession(m_activeActionSession, QStringLiteral("Background task queued"))
            : (decision.status.isEmpty() ? QStringLiteral("Background task queued") : decision.status);
        deliverLocalResponse(
            preamble,
            status,
            decision.speak);
        return true;
    }
    case InputRouteKind::AgentConversation:
        if (m_loggingService) {
            m_loggingService->infoFor(QStringLiteral("route_audit"), QStringLiteral("[route_outcome] resolved=agent_conversation"));
        }
        if (m_activeActionSession.shouldAnnounceProgress) {
            setStatus(m_executionNarrator
                ? m_executionNarrator->statusForSession(m_activeActionSession, QStringLiteral("Working on request"))
                : QStringLiteral("Working on request"));
        }
        startAgentConversationRequest(routedInput, decision.intent);
        return true;
    case InputRouteKind::CommandExtraction:
        if (m_loggingService) {
            m_loggingService->infoFor(QStringLiteral("route_audit"), QStringLiteral("[route_outcome] resolved=command_extraction"));
        }
        if (m_activeActionSession.shouldAnnounceProgress) {
            setStatus(m_executionNarrator
                ? m_executionNarrator->statusForSession(m_activeActionSession, QStringLiteral("Working on request"))
                : QStringLiteral("Working on request"));
        }
        startCommandRequest(routedInput);
        return true;
    case InputRouteKind::Conversation:
        if (m_loggingService) {
            m_loggingService->infoFor(QStringLiteral("route_audit"), QStringLiteral("[route_outcome] resolved=conversation"));
        }
        if (decision.useVisionContext && !isExplicitComputerControlQuery(routedInput) && m_loggingService) {
            m_loggingService->info(QStringLiteral("Routing vision-relevant query through conversation. input=\"%1\"")
                                       .arg(routedInput.left(240)));
        }
        startConversationRequest(routedInput);
        return true;
    case InputRouteKind::None:
    default:
        return false;
    }
}

void AssistantController::submitText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    if (handlePendingConfirmationInput(trimmed)) {
        return;
    }

    clearSurfaceError(QStringLiteral("assistant"));
    m_lastPromptForAiLog = trimmed;
    invalidateWakeMonitorResume();

    bool wakeDetected = false;
    const QString routedInput = prepareSubmitInput(trimmed, &wakeDetected);
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
    m_memoryPolicyHandler->processUserTurn(trimmed, effectiveInput);

    AgentTask deterministicTask;
    QString deterministicSpoken;
    const bool hasDeterministicTask = buildDeterministicComputerTask(routedInput, &deterministicTask, &deterministicSpoken);

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

    const LocalIntent intent = m_intentRouter->classify(routedInput);
    const AiAvailability availability = m_modelCatalogService->availability();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool recentVisionQuery = (nowMs - m_lastVisionQueryMs) <= 12000;
    const bool visionRelevantQuery = isVisionRelevantQuery(routedInput)
        || (recentVisionQuery && isVisionFollowUpQuery(routedInput));
    const bool shouldEndConversation = shouldEndConversationSession(effectiveInput);
    const bool isTimeQuery = isCurrentTimeQuery(routedInput);
    const bool isDateQuery = isCurrentDateQuery(routedInput);
    const bool wakeOnly = wakeDetected && routedInput.isEmpty();
    if (wakeOnly) {
        m_followUpListeningAfterWakeAck = true;
    }
    const InputRouterContext routeContext = buildInputRouteContext(
        routedInput,
        detectedIntent,
        effectiveIntent,
        intent,
        availability,
        visionRelevantQuery,
        wakeOnly,
        shouldEndConversation,
        isTimeQuery,
        isDateQuery,
        hasDeterministicTask,
        deterministicTask,
        deterministicSpoken,
        nowMs);
    const InputRouteDecision decision = m_inputRouter->decide(routeContext);
    if (m_loggingService) {
        m_loggingService->infoFor(
            QStringLiteral("route_audit"),
            QStringLiteral("[route_decide] input=%1 wakeDetected=%2 wakeOnly=%3 aiAvailable=%4 localIntent=%5 decision=%6 intent=%7 status=%8 taskCount=%9")
                .arg(userFacingPromptForLogging(effectiveInput).left(360),
                     wakeDetected ? QStringLiteral("true") : QStringLiteral("false"),
                     wakeOnly ? QStringLiteral("true") : QStringLiteral("false"),
                     (availability.online && availability.modelAvailable) ? QStringLiteral("true") : QStringLiteral("false"),
                     QString::number(static_cast<int>(intent)),
                     routeKindToString(decision.kind),
                     intentTypeToString(decision.intent),
                     decision.status.simplified(),
                     QString::number(decision.tasks.size())));
    }
    if (shouldContinueActionThread(effectiveInput, decision, nowMs)) {
        m_lastPromptForAiLog = effectiveInput;
        const QString continuationInput = buildActionThreadContinuationInput(effectiveInput);
        if (m_loggingService) {
            m_loggingService->infoFor(
                QStringLiteral("follow_up_audit"),
                QStringLiteral("[action_thread_continue] input=%1 routedTo=%2")
                    .arg(userFacingPromptForLogging(effectiveInput).left(320),
                         (m_settings->agentEnabled() && availability.online && availability.modelAvailable)
                             ? QStringLiteral("agent_conversation")
                             : QStringLiteral("conversation")));
        }
        if (m_settings->agentEnabled() && availability.online && availability.modelAvailable) {
            startAgentConversationRequest(continuationInput, IntentType::GENERAL_CHAT);
        } else {
            startConversationRequest(continuationInput);
        }
        return;
    }
    if (m_recentActionThread.has_value() && !m_recentActionThread->isUsable(nowMs)) {
        clearActionThread();
    } else {
        switch (decision.kind) {
        case InputRouteKind::BackgroundTasks:
        case InputRouteKind::DeterministicTasks:
        case InputRouteKind::CommandExtraction:
        case InputRouteKind::AgentConversation:
            clearActionThread();
            break;
        default:
            break;
        }
    }
    if (!executeRouteDecision(decision, routedInput, intent, false, nowMs)) {
        if (m_loggingService) {
            m_loggingService->warnFor(
                QStringLiteral("route_audit"),
                QStringLiteral("[route_fallback] decision unresolved, defaulting to conversation input=%1")
                    .arg(userFacingPromptForLogging(routedInput).left(320)));
        }
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

void AssistantController::noteProactiveSuggestionFeedback(const QString &signalType,
                                                          const QString &suggestionType)
{
    logProactiveSuggestionFeedback(signalType, suggestionType);
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
        if (allWordsLowSignal(words)) {
            return true;
        }

        if (words.size() <= 3
            && isLowSignalConversationToken(words.first())
            && allWordsLowSignal(words.mid(0, std::min<qsizetype>(words.size(), 3)))) {
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

FocusModeState AssistantController::currentFocusModeState() const
{
    return {
        .enabled = m_settings != nullptr && m_settings->focusModeEnabled(),
        .allowCriticalAlerts = m_settings != nullptr && m_settings->focusModeAllowCriticalAlerts(),
        .durationMinutes = m_settings != nullptr ? m_settings->focusModeDurationMinutes() : 0,
        .untilEpochMs = m_settings != nullptr ? m_settings->focusModeUntilEpochMs() : 0,
        .source = QStringLiteral("settings")
    };
}

QVariantMap AssistantController::buildConnectorPlannerMetadata(const QString &sourceKind,
                                                              const QString &connectorKind,
                                                              const QString &historyKey,
                                                              const QVariantMap &baseMetadata,
                                                              qint64 nowMs)
{
    QVariantMap metadata = baseMetadata;
    if (m_connectorHistoryTracker == nullptr || historyKey.trimmed().isEmpty()) {
        return metadata;
    }

    m_connectorHistoryTracker->recordSeen(sourceKind, connectorKind, historyKey, nowMs);
    const QVariantMap history = m_connectorHistoryTracker->buildMetadata(
        sourceKind,
        connectorKind,
        historyKey,
        nowMs);
    for (auto it = history.cbegin(); it != history.cend(); ++it) {
        metadata.insert(it.key(), it.value());
    }
    return metadata;
}

ProactiveSuggestionPlan AssistantController::planNextStepSuggestion(const QString &sourceKind,
                                                                   const QString &taskType,
                                                                   const QString &resultSummary,
                                                                   const QStringList &sourceUrls,
                                                                   const QVariantMap &sourceMetadata,
                                                                   const QString &presentationKey,
                                                                   bool success) const
{
    QVariantMap plannerMetadata = sourceMetadata;
    if (m_memoryPolicyHandler != nullptr) {
        const QList<MemoryRecord> policySummaryRecords = m_memoryPolicyHandler->compiledContextPolicySummaryRecords();
        QStringList summaryKeys;
        QStringList summaryValues;
        for (const MemoryRecord &record : policySummaryRecords) {
            if (!record.key.trimmed().isEmpty()) {
                summaryKeys.push_back(record.key.trimmed());
            }
            if (!record.value.trimmed().isEmpty()) {
                summaryValues.push_back(record.value.simplified());
            }
        }
        if (!summaryKeys.isEmpty()) {
            plannerMetadata.insert(QStringLiteral("compiledContextPolicySummaryKeys"), summaryKeys);
            plannerMetadata.insert(QStringLiteral("compiledContextPolicySummary"), summaryValues.join(QStringLiteral(" ")));
        }
        const QList<MemoryRecord> layeredMemoryRecords = m_memoryPolicyHandler->compiledContextLayeredMemoryRecords();
        const QStringList layeredKeys = CompiledContextLayeredSignalBuilder::buildPlannerKeys(layeredMemoryRecords);
        const QString layeredSummary = CompiledContextLayeredSignalBuilder::buildPlannerSummary(layeredMemoryRecords);
        if (!layeredKeys.isEmpty()) {
            plannerMetadata.insert(QStringLiteral("compiledContextLayeredKeys"), layeredKeys);
            plannerMetadata.insert(QStringLiteral("compiledContextLayeredSummary"), layeredSummary);
        }
        const QList<MemoryRecord> evolutionRecords = m_memoryPolicyHandler->compiledContextPolicyEvolutionRecords();
        const QStringList evolutionKeys = CompiledContextLayeredSignalBuilder::buildPlannerKeys(evolutionRecords);
        const QString evolutionSummary = CompiledContextLayeredSignalBuilder::buildPlannerSummary(evolutionRecords);
        if (!evolutionKeys.isEmpty()) {
            plannerMetadata.insert(QStringLiteral("compiledContextEvolutionKeys"), evolutionKeys);
            plannerMetadata.insert(QStringLiteral("compiledContextEvolutionSummary"), evolutionSummary);
        }
        const QList<MemoryRecord> tuningRecords = m_memoryPolicyHandler->compiledContextPolicyTuningSignalRecords();
        const QStringList tuningKeys = CompiledContextLayeredSignalBuilder::buildPlannerKeys(tuningRecords);
        const QString tuningSummary = CompiledContextLayeredSignalBuilder::buildPlannerSummary(tuningRecords);
        if (!tuningKeys.isEmpty()) {
            plannerMetadata.insert(QStringLiteral("compiledContextTuningKeys"), tuningKeys);
            plannerMetadata.insert(QStringLiteral("compiledContextTuningSummary"), tuningSummary);
        }
        const QList<MemoryRecord> tuningEpisodeRecords =
            m_memoryPolicyHandler->compiledContextPolicyTuningEpisodeRecords();
        const QStringList tuningEpisodeKeys =
            CompiledContextLayeredSignalBuilder::buildPlannerKeys(tuningEpisodeRecords);
        const QString tuningEpisodeSummary =
            CompiledContextLayeredSignalBuilder::buildPlannerSummary(tuningEpisodeRecords);
        if (!tuningEpisodeKeys.isEmpty()) {
            plannerMetadata.insert(QStringLiteral("compiledContextTuningEpisodeKeys"), tuningEpisodeKeys);
            plannerMetadata.insert(QStringLiteral("compiledContextTuningEpisodeSummary"), tuningEpisodeSummary);
        }
        const QVariantMap tuningMetadata = m_memoryPolicyHandler->compiledContextPolicyTuningMetadata();
        for (auto it = tuningMetadata.constBegin(); it != tuningMetadata.constEnd(); ++it) {
            plannerMetadata.insert(it.key(), it.value());
        }
    }
    QVariantMap historyMetadata = m_memoryPolicyHandler
        ? m_memoryPolicyHandler->compiledContextPolicyState()
        : QVariantMap{};
    if (historyMetadata.isEmpty()) {
        historyMetadata = CompiledContextHistorySummaryBuilder::buildPlannerMetadata(
            buildCompiledContextHistoryMemory());
    }
    for (auto it = historyMetadata.constBegin(); it != historyMetadata.constEnd(); ++it) {
        plannerMetadata.insert(it.key(), it.value());
    }

    const ProactiveSuggestionPlan plan = ProactiveSuggestionPlanner::plan({
        .sourceKind = sourceKind,
        .taskType = taskType,
        .resultSummary = resultSummary,
        .sourceUrls = sourceUrls,
        .sourceMetadata = plannerMetadata,
        .presentationKey = presentationKey,
        .lastPresentedKey = m_lastProactiveSuggestionThreadId,
        .lastPresentedAtMs = m_lastProactiveSuggestionMs,
        .success = success,
        .desktopContext = m_latestDesktopContext,
        .desktopContextAtMs = m_latestDesktopContextAtMs,
        .cooldownState = m_proactiveCooldownState,
        .focusMode = currentFocusModeState(),
        .nowMs = QDateTime::currentMSecsSinceEpoch()
    });
    logPlannedSuggestion(plan, sourceKind, taskType);
    return plan;
}

void AssistantController::logPlannedSuggestion(const ProactiveSuggestionPlan &plan,
                                               const QString &sourceKind,
                                               const QString &taskType) const
{
    if (m_loggingService == nullptr) {
        return;
    }

    const QString threadId = m_latestDesktopContext.value(QStringLiteral("threadId")).toString();
    if (m_loggingService != nullptr) {
        QStringList titles;
        QStringList priorities;
        for (const ActionProposal &proposal : plan.generatedProposals) {
            titles.push_back(proposal.title);
            priorities.push_back(proposal.priority);
        }
        BehaviorTraceEvent event = BehaviorTraceEvent::create(
            QStringLiteral("action_proposal"),
            QStringLiteral("generated"),
            plan.generatedProposals.isEmpty()
                ? QStringLiteral("proposal.none_generated")
                : QStringLiteral("proposal.generated"),
            {
                {QStringLiteral("sourceKind"), sourceKind},
                {QStringLiteral("taskType"), taskType},
                {QStringLiteral("proposalCount"), plan.generatedProposals.size()},
                {QStringLiteral("proposalTitles"), titles},
                {QStringLiteral("proposalPriorities"), priorities},
                {QStringLiteral("desktopSummary"), m_latestDesktopContextSummary}
            },
            QStringLiteral("system"));
        event.capabilityId = QStringLiteral("suggestion_proposal_builder");
        event.threadId = threadId;
        m_loggingService->logBehaviorEvent(event);
    }

    if (!plan.rankedProposals.isEmpty()) {
        const RankedSuggestionProposal &top = plan.rankedProposals.first();
        QStringList rankedTitles;
        QStringList rankedScores;
        for (const RankedSuggestionProposal &proposal : plan.rankedProposals) {
            rankedTitles.push_back(proposal.proposal.title);
            rankedScores.push_back(QString::number(proposal.score, 'f', 2));
        }
        QVariantMap payload = top.toVariantMap();
        payload.insert(QStringLiteral("sourceKind"), sourceKind);
        payload.insert(QStringLiteral("taskType"), taskType);
        payload.insert(QStringLiteral("rankedTitles"), rankedTitles);
        payload.insert(QStringLiteral("rankedScores"), rankedScores);
        payload.insert(QStringLiteral("desktopSummary"), m_latestDesktopContextSummary);
        BehaviorTraceEvent event = BehaviorTraceEvent::create(
            QStringLiteral("action_proposal"),
            QStringLiteral("ranked"),
            top.reasonCode,
            payload,
            QStringLiteral("system"));
        event.capabilityId = QStringLiteral("suggestion_proposal_ranker");
        event.threadId = threadId;
        m_loggingService->logBehaviorEvent(event);
    }

    if (plan.hasSelectedProposal()) {
        QVariantMap payload = plan.selectedProposal.toVariantMap();
        const QVariantMap decisionMap = plan.decision.toVariantMap();
        for (auto it = decisionMap.constBegin(); it != decisionMap.constEnd(); ++it) {
            payload.insert(it.key(), it.value());
        }
        payload.insert(QStringLiteral("sourceKind"), sourceKind);
        payload.insert(QStringLiteral("taskType"), taskType);
        payload.insert(QStringLiteral("desktopSummary"), m_latestDesktopContextSummary);
        payload.insert(QStringLiteral("desktopTaskId"), m_latestDesktopContext.value(QStringLiteral("taskId")).toString());
        payload.insert(QStringLiteral("desktopThreadId"), threadId);
        payload.insert(QStringLiteral("selectedSummary"), plan.selectedSummary);
        payload.insert(QStringLiteral("confidenceScore"), plan.confidenceScore);
        payload.insert(QStringLiteral("noveltyScore"), plan.noveltyScore);
        payload.insert(QStringLiteral("cooldownState"), m_proactiveCooldownState.toVariantMap());
        payload.insert(QStringLiteral("nextCooldownState"), plan.nextCooldownState.toVariantMap());
        payload.insert(QStringLiteral("cooldownDecision"), plan.cooldownDecision.toVariantMap());
        BehaviorTraceEvent event = BehaviorTraceEvent::create(
            QStringLiteral("action_proposal"),
            QStringLiteral("gated"),
            plan.decision.reasonCode,
            payload,
            QStringLiteral("system"));
        event.capabilityId = QStringLiteral("proactive_suggestion_planner");
        event.threadId = threadId;
        m_loggingService->logBehaviorEvent(event);
    }
}

void AssistantController::logProactiveSuggestionFeedback(const QString &signalType,
                                                         const QString &suggestionType)
{
    const QString type = suggestionType.trimmed().isEmpty()
        ? m_latestProactiveSuggestionType
        : suggestionType.trimmed();
    if (!type.startsWith(QStringLiteral("proactive"))) {
        return;
    }

    const FeedbackSignal signal = FeedbackSignalEventBuilder::proactiveSuggestionSignal(
        signalType,
        type,
        m_latestProactiveSuggestion,
        m_lastProactiveSuggestionThreadId,
        QDateTime::currentMSecsSinceEpoch());
    if (m_memoryStore != nullptr) {
        m_memoryStore->appendFeedbackSignal(signal);
    }
    if (m_loggingService != nullptr) {
        m_loggingService->logBehaviorEvent(FeedbackSignalEventBuilder::behaviorEvent(signal));
    }
}

void AssistantController::commitProactivePresentation(const QString &surfaceKind,
                                                      const QString &taskType,
                                                      const QString &priority,
                                                      const QString &reasonCode)
{
    const ProactiveCooldownCommit commit = ProactiveCooldownTracker::commitPresentedSurface({
        .state = m_proactiveCooldownState,
        .desktopContext = m_latestDesktopContext,
        .taskType = taskType,
        .surfaceKind = surfaceKind,
        .priority = priority,
        .nowMs = QDateTime::currentMSecsSinceEpoch()
    });
    m_proactiveCooldownState = commit.nextState;

    if (m_loggingService == nullptr) {
        return;
    }

    BehaviorTraceEvent event = BehaviorTraceEvent::create(
        QStringLiteral("cooldown"),
        QStringLiteral("presented"),
        reasonCode.trimmed().isEmpty() ? QStringLiteral("surface.presented") : reasonCode.trimmed(),
        {
            {QStringLiteral("surfaceKind"), surfaceKind},
            {QStringLiteral("taskType"), taskType},
            {QStringLiteral("priority"), priority},
            {QStringLiteral("confidence"), commit.confidence},
            {QStringLiteral("novelty"), commit.novelty},
            {QStringLiteral("topic"), commit.context.topic},
            {QStringLiteral("nextCooldownState"), commit.nextState.toVariantMap()}
        },
        QStringLiteral("system"));
    event.capabilityId = QStringLiteral("proactive_cooldown_tracker");
    event.threadId = commit.context.threadId.value;
    m_loggingService->logBehaviorEvent(event);
}

void AssistantController::logCompiledContextDelta(const QString &purpose,
                                                  const QString &input,
                                                  const SelectionContextCompilation &selectionContext)
{
    if (m_loggingService == nullptr) {
        return;
    }

    const QString previousSummary = m_lastCompiledContextSummaryByPurpose.value(purpose);
    const QStringList previousKeys = m_lastCompiledContextKeysByPurpose.value(purpose);
    QStringList currentKeys;
    for (const MemoryRecord &record : selectionContext.compiledContextRecords) {
        const QString key = record.key.trimmed();
        if (!key.isEmpty() && !currentKeys.contains(key)) {
            currentKeys.push_back(key);
        }
    }

    const CompiledContextDelta delta = CompiledContextDeltaTracker::evaluate(
        previousSummary,
        previousKeys,
        selectionContext.compiledDesktopSummary,
        currentKeys);
    const bool unchanged = !delta.hasChanges();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    int stableCycles = unchanged ? (m_compiledContextStableCyclesByPurpose.value(purpose) + 1) : 0;
    qint64 changedAtMs = unchanged
        ? m_lastCompiledContextChangedAtMsByPurpose.value(purpose, nowMs)
        : nowMs;
    if (changedAtMs <= 0) {
        changedAtMs = nowMs;
    }
    const qint64 stableDurationMs = unchanged ? qMax<qint64>(0, nowMs - changedAtMs) : 0;

    if (delta.hasChanges()) {
        m_loggingService->logBehaviorEvent(SelectionTelemetryBuilder::compiledContextDeltaEvent(
            purpose,
            input,
            m_latestDesktopContext,
            selectionContext.compiledDesktopSummary,
            delta.previousSummary,
            delta.previousKeys,
            delta.currentSummary,
            delta.currentKeys,
            delta.addedKeys,
            delta.removedKeys,
            delta.summaryChanged));
    }

    const CompiledContextStabilitySummary stability = CompiledContextStabilityTracker::evaluate(
        selectionContext.compiledDesktopSummary,
        currentKeys,
        stableCycles,
        stableDurationMs);
    m_loggingService->logBehaviorEvent(SelectionTelemetryBuilder::compiledContextStabilityEvent(
        purpose,
        input,
        m_latestDesktopContext,
        selectionContext.compiledDesktopSummary,
        stability.summaryText,
        stability.stableKeys,
        stability.stableCycles,
        stability.stableDurationMs,
        stability.stableContext));

    m_lastCompiledContextSummaryByPurpose.insert(purpose, selectionContext.compiledDesktopSummary);
    m_lastCompiledContextKeysByPurpose.insert(purpose, currentKeys);
    m_lastCompiledContextChangedAtMsByPurpose.insert(purpose, changedAtMs);
    m_compiledContextStableCyclesByPurpose.insert(purpose, stableCycles);
    m_lastCompiledContextStabilitySummaryByPurpose.insert(purpose, stability.summaryText);
    m_lastCompiledContextStableKeysByPurpose.insert(purpose, stability.stableKeys);
    m_lastCompiledContextStableDurationMsByPurpose.insert(purpose, stability.stableDurationMs);
}

MemoryRecord AssistantController::buildCompiledContextStabilityMemory(const QString &purpose) const
{
    return CompiledContextStabilityMemoryBuilder::build(
        purpose,
        m_lastCompiledContextStabilitySummaryByPurpose.value(purpose),
        m_lastCompiledContextStableKeysByPurpose.value(purpose),
        m_compiledContextStableCyclesByPurpose.value(purpose),
        m_lastCompiledContextStableDurationMsByPurpose.value(purpose));
}

QList<MemoryRecord> AssistantController::buildCompiledContextHistoryMemory() const
{
    const QList<MemoryRecord> records = CompiledContextHistorySummaryBuilder::build(
        m_lastCompiledContextStabilitySummaryByPurpose,
        m_lastCompiledContextStableKeysByPurpose,
        m_compiledContextStableCyclesByPurpose,
        m_lastCompiledContextStableDurationMsByPurpose);

    if (m_memoryStore != nullptr) {
        const CompiledContextHistoryPolicyDecision policyDecision =
            CompiledContextHistoryPolicy::evaluate(records);
        if (policyDecision.isValid()) {
            m_memoryStore->upsertCompiledContextPolicyState(
                CompiledContextHistoryPolicy::buildState(policyDecision));
            const QVariantList policyHistory = m_memoryStore->compiledContextPolicyHistory();
            const QVariantMap candidateTuningState =
                CompiledContextPolicyTuningSignalBuilder::buildState(policyHistory);
            const CompiledContextPolicyTuningPromotionDecision tuningDecision =
                CompiledContextPolicyTuningPromotionPolicy::evaluate(
                    candidateTuningState,
                    m_memoryStore->compiledContextPolicyTuningState(),
                    m_memoryStore->compiledContextPolicyTuningHistory(),
                    QDateTime::currentMSecsSinceEpoch(),
                    m_memoryStore->compiledContextPolicyTuningFeedbackScores());
            if (tuningDecision.action
                == CompiledContextPolicyTuningPromotionDecision::Action::Promote) {
                m_memoryStore->promoteCompiledContextPolicyTuningState(tuningDecision.nextState);
            } else if (tuningDecision.action
                       == CompiledContextPolicyTuningPromotionDecision::Action::Rollback) {
                m_memoryStore->rollbackCompiledContextPolicyTuningState({
                    {QStringLiteral("tuningPromotionAction"), QStringLiteral("rollback")},
                    {QStringLiteral("tuningPromotionReason"), tuningDecision.reasonCode},
                    {QStringLiteral("tuningPolicySource"), QStringLiteral("bounded_promotion_policy")}
                });
            }

            if (m_loggingService != nullptr) {
                QVariantMap payload = {
                    {QStringLiteral("decisionAction"),
                     tuningDecision.action
                         == CompiledContextPolicyTuningPromotionDecision::Action::Promote
                         ? QStringLiteral("promote")
                         : (tuningDecision.action
                                == CompiledContextPolicyTuningPromotionDecision::Action::Rollback
                                ? QStringLiteral("rollback")
                                : QStringLiteral("hold"))},
                    {QStringLiteral("reasonCode"), tuningDecision.reasonCode},
                    {QStringLiteral("candidateState"), candidateTuningState}
                };
                if (!tuningDecision.nextState.isEmpty()) {
                    payload.insert(QStringLiteral("nextState"), tuningDecision.nextState);
                }
                BehaviorTraceEvent event = BehaviorTraceEvent::create(
                    QStringLiteral("policy_adaptation"),
                    QStringLiteral("compiled_context_tuning"),
                    tuningDecision.reasonCode,
                    payload,
                    QStringLiteral("system"));
                event.capabilityId = QStringLiteral("compiled_context_policy_tuning");
                m_loggingService->logBehaviorEvent(event);
            }
        } else {
            m_memoryStore->deleteCompiledContextPolicyState();
            m_memoryStore->deleteCompiledContextPolicyTuningState();
        }
    }

    return records;
}

QString AssistantController::selectTemporalPromptContext(const QString &purpose,
                                                         const SelectionContextCompilation &selectionContext,
                                                         QList<MemoryRecord> *selectedPromptRecords,
                                                         QStringList *suppressedPromptKeys,
                                                         int *stablePromptCycles,
                                                         qint64 *stablePromptDurationMs,
                                                         QString *reasonCode)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QString currentPromptContext = selectionContext.promptContext.simplified();

    QStringList currentKeys;
    currentKeys.reserve(selectionContext.promptContextRecords.size());
    for (const MemoryRecord &record : selectionContext.promptContextRecords) {
        const QString key = record.key.trimmed();
        if (!key.isEmpty() && !currentKeys.contains(key)) {
            currentKeys.push_back(key);
        }
    }

    const QString previousPromptContext = m_lastPromptContextByPurpose.value(purpose);
    const QStringList previousKeys = m_lastPromptContextKeysByPurpose.value(purpose);
    const bool unchanged = previousPromptContext == currentPromptContext && previousKeys == currentKeys;

    int stableCycles = unchanged ? (m_promptContextStableCyclesByPurpose.value(purpose) + 1) : 0;
    qint64 changedAtMs = unchanged
        ? m_lastPromptContextChangedAtMsByPurpose.value(purpose, nowMs)
        : nowMs;
    if (changedAtMs <= 0) {
        changedAtMs = nowMs;
    }
    const qint64 stableDurationMs = unchanged ? qMax<qint64>(0, nowMs - changedAtMs) : 0;

    const PromptContextTemporalDecision decision = PromptContextTemporalPolicy::apply(
        selectionContext.promptContextRecords,
        stableCycles,
        stableDurationMs);

    m_lastPromptContextByPurpose.insert(purpose, currentPromptContext);
    m_lastPromptContextKeysByPurpose.insert(purpose, currentKeys);
    m_lastPromptContextChangedAtMsByPurpose.insert(purpose, changedAtMs);
    m_promptContextStableCyclesByPurpose.insert(purpose, stableCycles);

    if (selectedPromptRecords != nullptr) {
        *selectedPromptRecords = decision.selectedRecords;
    }
    if (suppressedPromptKeys != nullptr) {
        *suppressedPromptKeys = decision.suppressedKeys;
    }
    if (stablePromptCycles != nullptr) {
        *stablePromptCycles = decision.stableCycles;
    }
    if (stablePromptDurationMs != nullptr) {
        *stablePromptDurationMs = decision.stableDurationMs;
    }
    if (reasonCode != nullptr) {
        *reasonCode = decision.reasonCode;
    }
    return decision.promptContext;
}

ActionProposal AssistantController::buildNextStepProposal(const QString &hint,
                                                          const QString &sourceKind,
                                                          const QString &taskType,
                                                          bool success) const
{
    ActionProposal proposal;
    proposal.proposalId = QStringLiteral("next_step_%1")
                              .arg(QString::number(QDateTime::currentMSecsSinceEpoch()));
    proposal.capabilityId = QStringLiteral("next_step_hint");
    proposal.title = QStringLiteral("Suggested next step");
    proposal.summary = hint.trimmed();
    proposal.priority = success ? QStringLiteral("medium") : QStringLiteral("high");
    proposal.arguments = {
        {QStringLiteral("sourceKind"), sourceKind},
        {QStringLiteral("taskType"), taskType},
        {QStringLiteral("success"), success}
    };
    return proposal;
}

QString AssistantController::gateNextStepHint(const QString &hint,
                                              const QString &sourceKind,
                                              const QString &taskType,
                                              bool success) const
{
    const QString trimmedHint = hint.trimmed();
    if (trimmedHint.isEmpty()) {
        return {};
    }

    const ActionProposal proposal = buildNextStepProposal(trimmedHint, sourceKind, taskType, success);
    const BehaviorDecision decision = ProactiveSuggestionGate::evaluate({
        .proposal = proposal,
        .desktopContext = m_latestDesktopContext,
        .desktopContextAtMs = m_latestDesktopContextAtMs,
        .focusMode = currentFocusModeState(),
        .nowMs = QDateTime::currentMSecsSinceEpoch()
    });

    if (m_loggingService != nullptr) {
        QVariantMap payload = proposal.toVariantMap();
        const QVariantMap decisionMap = decision.toVariantMap();
        for (auto it = decisionMap.constBegin(); it != decisionMap.constEnd(); ++it) {
            payload.insert(it.key(), it.value());
        }
        payload.insert(QStringLiteral("sourceKind"), sourceKind);
        payload.insert(QStringLiteral("taskType"), taskType);
        payload.insert(QStringLiteral("desktopSummary"), m_latestDesktopContextSummary);
        payload.insert(QStringLiteral("desktopTaskId"), m_latestDesktopContext.value(QStringLiteral("taskId")).toString());
        payload.insert(QStringLiteral("desktopThreadId"), m_latestDesktopContext.value(QStringLiteral("threadId")).toString());
        BehaviorTraceEvent event = BehaviorTraceEvent::create(
            QStringLiteral("action_proposal"),
            QStringLiteral("gated"),
            decision.reasonCode,
            payload,
            QStringLiteral("system"));
        event.capabilityId = QStringLiteral("proactive_suggestion_gate");
        event.threadId = m_latestDesktopContext.value(QStringLiteral("threadId")).toString();
        m_loggingService->logBehaviorEvent(event);
    }

    return decision.allowed ? trimmedHint : QString{};
}

void AssistantController::considerDesktopContextSuggestion(const QString &summary, const QVariantMap &context)
{
    if (m_settings == nullptr || m_settings->privateModeEnabled()) {
        return;
    }

    const QString taskId = context.value(QStringLiteral("taskId")).toString().trimmed();
    if (taskId != QStringLiteral("clipboard") && taskId != QStringLiteral("notification")) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QString threadId = context.value(QStringLiteral("threadId")).toString();
    if (!threadId.isEmpty() && threadId == m_lastProactiveSuggestionThreadId
        && (nowMs - m_lastProactiveSuggestionMs) <= 120000) {
        return;
    }

    const ProactiveSuggestionPlan plan = planNextStepSuggestion(
        QStringLiteral("desktop_context"),
        taskId,
        summary,
        {},
        context,
        threadId,
        true);
    if (plan.selectedSummary.isEmpty()) {
        return;
    }

    m_lastProactiveSuggestionThreadId = threadId;
    m_lastProactiveSuggestionMs = nowMs;
    m_proactiveCooldownState = plan.nextCooldownState;
    setLatestProactiveSuggestion(
        plan.selectedSummary,
        QStringLiteral("response"),
        QStringLiteral("proactive_%1").arg(taskId));

    if (m_loggingService != nullptr) {
        BehaviorTraceEvent event = BehaviorTraceEvent::create(
            QStringLiteral("ui_presentation"),
            QStringLiteral("suggested"),
            QStringLiteral("proposal.presented"),
            {
                {QStringLiteral("message"), plan.selectedSummary},
                {QStringLiteral("surfaceKind"), QStringLiteral("desktop_context_toast")},
                {QStringLiteral("taskType"), taskId},
                {QStringLiteral("desktopSummary"), summary}
            },
            QStringLiteral("system"));
        event.capabilityId = QStringLiteral("desktop_context_suggestion");
        event.threadId = threadId;
        m_loggingService->logBehaviorEvent(event);
    }
}

void AssistantController::considerConnectorEvent(const ConnectorEvent &event)
{
    if (m_settings == nullptr || m_settings->privateModeEnabled() || !event.isValid()) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QString dedupeKey = event.taskKey.trimmed().isEmpty()
        ? event.eventId
        : QStringLiteral("connector:%1").arg(event.taskKey.trimmed());
    if (!dedupeKey.isEmpty()
        && dedupeKey == m_lastProactiveSuggestionThreadId
        && (nowMs - m_lastProactiveSuggestionMs) <= 120000) {
        return;
    }

    const ProactiveSuggestionPlan plan = planNextStepSuggestion(
        event.sourceKind,
        event.taskType,
        event.summary,
        {},
        buildConnectorPlannerMetadata(
            event.sourceKind,
            event.connectorKind,
            dedupeKey,
            event.toVariantMap(),
            nowMs),
        dedupeKey,
        event.priority.compare(QStringLiteral("high"), Qt::CaseInsensitive) != 0);
    if (plan.selectedSummary.isEmpty()) {
        return;
    }

    m_lastProactiveSuggestionThreadId = dedupeKey;
    m_lastProactiveSuggestionMs = nowMs;
    if (m_connectorHistoryTracker != nullptr) {
        m_connectorHistoryTracker->recordPresented(dedupeKey, nowMs);
    }
    setLatestProactiveSuggestion(
        plan.selectedSummary,
        QStringLiteral("response"),
        QStringLiteral("proactive_connector"));
    commitProactivePresentation(
        QStringLiteral("connector_event_toast"),
        event.taskType,
        event.priority,
        QStringLiteral("connector_event.presented"));

    if (m_loggingService != nullptr) {
        QVariantMap payload = event.toVariantMap();
        payload.insert(QStringLiteral("message"), plan.selectedSummary);
        payload.insert(QStringLiteral("surfaceKind"), QStringLiteral("connector_event_toast"));
        payload.insert(QStringLiteral("desktopSummary"), m_latestDesktopContextSummary);
        BehaviorTraceEvent traceEvent = BehaviorTraceEvent::create(
            QStringLiteral("ui_presentation"),
            QStringLiteral("suggested"),
            QStringLiteral("connector_event.presented"),
            payload,
            QStringLiteral("system"));
        traceEvent.capabilityId = QStringLiteral("connector_event_suggestion");
        traceEvent.threadId = m_latestDesktopContext.value(QStringLiteral("threadId")).toString();
        m_loggingService->logBehaviorEvent(traceEvent);
    }
}

void AssistantController::considerTaskResultSuggestion(const BackgroundTaskResult &result)
{
    if (m_settings == nullptr || m_settings->privateModeEnabled()) {
        return;
    }
    if (!isEligibleTaskResultSuggestion(result)) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QString resultKey = result.taskKey.trimmed().isEmpty()
        ? QStringLiteral("%1:%2").arg(result.type.trimmed(), QString::number(result.taskId))
        : result.taskKey.trimmed();
    if (!resultKey.isEmpty()
        && resultKey == m_lastProactiveSuggestionThreadId
        && (nowMs - m_lastProactiveSuggestionMs) <= 120000) {
        return;
    }

    const ConnectorResultSignal connectorSignal = ConnectorResultSignalBuilder::fromBackgroundTaskResult(result);
    const QString summary = connectorSignal.isValid()
        ? connectorSignal.summary
        : (m_executionNarrator
        ? m_executionNarrator->summarizeBackgroundResult(result)
        : (result.summary.trimmed().isEmpty() ? result.detail.trimmed() : result.summary.trimmed()));
    QVariantMap resultMetadata = result.payload.toVariantMap();
    resultMetadata.insert(QStringLiteral("taskKey"), result.taskKey);
    resultMetadata.insert(QStringLiteral("taskId"), result.taskId);
    resultMetadata.insert(QStringLiteral("finishedAt"), result.finishedAt);
    const QString resultConnectorKind = connectorSignal.isValid()
        ? connectorSignal.connectorKind
        : result.payload.value(QStringLiteral("connectorKind")).toString().trimmed();
    if (connectorSignal.isValid()) {
        resultMetadata.insert(QStringLiteral("connectorKind"), connectorSignal.connectorKind);
        resultMetadata.insert(QStringLiteral("itemCount"), connectorSignal.itemCount);
        resultMetadata.insert(QStringLiteral("occurredAtUtc"), result.finishedAt);
    }
    const ProactiveSuggestionPlan plan = planNextStepSuggestion(
        connectorSignal.isValid() ? connectorSignal.sourceKind : QStringLiteral("task_result_surface"),
        connectorSignal.isValid() ? connectorSignal.taskType : result.type,
        summary,
        sourceUrlsForResult(result),
        buildConnectorPlannerMetadata(
            connectorSignal.isValid() ? connectorSignal.sourceKind : QStringLiteral("task_result_surface"),
            resultConnectorKind,
            resultKey,
            resultMetadata,
            nowMs),
        resultKey,
        result.success);
    if (plan.selectedSummary.isEmpty()) {
        return;
    }

    m_lastProactiveSuggestionThreadId = resultKey;
    m_lastProactiveSuggestionMs = nowMs;
    if (m_connectorHistoryTracker != nullptr && !resultConnectorKind.isEmpty()) {
        m_connectorHistoryTracker->recordPresented(resultKey, nowMs);
    }
    setLatestProactiveSuggestion(
        plan.selectedSummary,
        QStringLiteral("response"),
        QStringLiteral("proactive_task_result"));
    commitProactivePresentation(
        QStringLiteral("task_result_suggestion"),
        result.type,
        result.success ? QStringLiteral("medium") : QStringLiteral("high"),
        QStringLiteral("proposal.presented"));

    if (m_loggingService != nullptr) {
        BehaviorTraceEvent event = BehaviorTraceEvent::create(
            QStringLiteral("ui_presentation"),
            QStringLiteral("suggested"),
            QStringLiteral("proposal.presented"),
            {
                {QStringLiteral("message"), plan.selectedSummary},
                {QStringLiteral("surfaceKind"), QStringLiteral("task_result_toast")},
                {QStringLiteral("taskType"), result.type},
                {QStringLiteral("taskId"), result.taskId},
                {QStringLiteral("taskKey"), result.taskKey},
                {QStringLiteral("connectorKind"), connectorSignal.connectorKind},
                {QStringLiteral("connectorItemCount"), connectorSignal.itemCount},
                {QStringLiteral("desktopSummary"), m_latestDesktopContextSummary}
            },
            QStringLiteral("system"));
        event.capabilityId = QStringLiteral("task_result_suggestion");
        event.threadId = m_latestDesktopContext.value(QStringLiteral("threadId")).toString();
        m_loggingService->logBehaviorEvent(event);
    }
}

void AssistantController::setLatestProactiveSuggestion(const QString &message,
                                                       const QString &tone,
                                                       const QString &type)
{
    const QString normalizedMessage = message.simplified();
    const QString normalizedTone = tone.trimmed().isEmpty() ? QStringLiteral("response") : tone.trimmed();
    const QString normalizedType = type.trimmed().isEmpty() ? QStringLiteral("proactive") : type.trimmed();

    if (m_latestProactiveSuggestion == normalizedMessage
        && m_latestProactiveSuggestionTone == normalizedTone
        && m_latestProactiveSuggestionType == normalizedType) {
        return;
    }

    m_latestProactiveSuggestion = normalizedMessage;
    m_latestProactiveSuggestionTone = normalizedTone;
    m_latestProactiveSuggestionType = normalizedType;
    emit latestProactiveSuggestionChanged();
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

bool AssistantController::finalizeReply(const QString &source,
                                        const SpokenReply &reply,
                                        const QString &status,
                                        int restartDelayMs,
                                        bool logAgentExchange,
                                        bool allowFollowUpWakeDelay)
{
    ActionSession finalSession = m_activeActionSession;
    finalSession.nextStepHint = gateNextStepHint(
        finalSession.nextStepHint,
        source,
        m_recentActionThread.has_value() ? m_recentActionThread->taskType : QStringLiteral("conversation"),
        !m_recentActionThread.has_value() || m_recentActionThread->success);
    const bool appendedHint = m_responseFinalizer != nullptr
        && m_responseFinalizer->willAppendHint(finalSession, reply)
        && !finalSession.nextStepHint.trimmed().isEmpty();
    const bool spoke = m_responseFinalizer->finalizeResponse(
        source,
        reply,
        finalSession,
        &m_responseText,
        [this]() { emit responseTextChanged(); },
        [this]() { refreshConversationSession(); },
        [this](const QString &response, const QString &responseSource, const QString &logStatus) { logPromptResponsePair(response, responseSource, logStatus); },
        status,
        [this](const QString &newStatus) { setStatus(newStatus); });

    if (appendedHint) {
        commitProactivePresentation(
            QStringLiteral("response_hint"),
            m_recentActionThread.has_value() ? m_recentActionThread->taskType : QStringLiteral("conversation"),
            (!m_recentActionThread.has_value() || m_recentActionThread->success)
                ? QStringLiteral("medium")
                : QStringLiteral("high"),
            QStringLiteral("surface.response_hint_presented"));
    }

    if (!spoke && !m_ttsEngine->isSpeaking()) {
        setDuplexState(DuplexState::Open);
        if (conversationSessionShouldContinue()) {
            const int nextRestartDelayMs = allowFollowUpWakeDelay && m_followUpListeningAfterWakeAck
                ? followUpListeningDelayMs()
                : restartDelayMs;
            if (!scheduleConversationSessionListening(nextRestartDelayMs)) {
                endConversationSession();
                scheduleWakeMonitorRestart();
            }
        } else {
            endConversationSession();
            scheduleWakeMonitorRestart();
        }
        emit idleRequested();
    }

    if (logAgentExchange && m_loggingService) {
        m_loggingService->logAgentExchange(m_lastPromptForAiLog,
                                           reply.displayText,
                                           source,
                                           m_agentCapabilities,
                                           samplingProfile(),
                                           m_agentTrace,
                                           status);
        m_lastPromptForAiLog.clear();
    }

    return spoke;
}

void AssistantController::deliverLocalResponse(const QString &text, const QString &status, bool speak)
{
    SpokenReply reply;
    reply.displayText = text;
    reply.spokenText = speak ? text : QString{};
    reply.shouldSpeak = speak;
    finalizeReply(QStringLiteral("local"),
                  reply,
                  status,
                  conversationSessionRestartDelayMs(),
                  false,
                  true);
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
    const SelectionContextCompilation selectionContext = SelectionContextCompiler::compile(
        input,
        IntentType::GENERAL_CHAT,
        m_latestDesktopContext,
        m_latestDesktopContextSummary,
        m_latestDesktopContextAtMs,
        m_settings != nullptr && m_settings->privateModeEnabled(),
        runtimeToolStatusMemory(m_settings),
        buildCompiledContextHistoryMemory(),
        m_memoryPolicyHandler.get(),
        m_assistantBehaviorPolicy.get());
    const QList<MemoryRecord> compiledContextHistoryRecords = buildCompiledContextHistoryMemory();
    const QString selectionInput = selectionContext.selectionInput;
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Starting conversation request. model=\"%1\" input=\"%2\"")
            .arg(modelId, userFacingPromptForLogging(input).left(240)));
    }

    if (m_assistantBehaviorPolicy) {
        InputRouteDecision decision;
        decision.kind = InputRouteKind::Conversation;
        const ToolPlan plan = m_assistantBehaviorPolicy->buildToolPlan(
            selectionInput,
            IntentType::GENERAL_CHAT,
            m_agentToolbox ? m_agentToolbox->builtInTools() : QList<AgentToolSpec>{});
        if (m_loggingService) {
            m_loggingService->logBehaviorEvent(SelectionTelemetryBuilder::toolPlanEvent(
                QStringLiteral("conversation"),
                input,
                m_latestDesktopContext,
                selectionContext.compiledDesktopSummary,
                plan));
        }
        const TrustDecision trust = m_assistantBehaviorPolicy->assessTrust(input, decision, plan, m_latestDesktopContext);
        m_activeActionSession = m_assistantBehaviorPolicy->createActionSession(input, decision, plan, trust, m_latestDesktopContext);
    }

    const QList<MemoryRecord> &memoryRecords = selectionContext.selectedMemoryRecords;
    logCompiledContextDelta(QStringLiteral("conversation"), input, selectionContext);
    QList<MemoryRecord> compiledContextRecords = selectionContext.compiledContextRecords;
    MemoryContext memoryContext = selectionContext.memoryContext;
    const QList<MemoryRecord> compiledContextHistoryMemory = buildCompiledContextHistoryMemory();
    for (const MemoryRecord &record : compiledContextHistoryMemory) {
        if (record.key.trimmed().isEmpty()) {
            continue;
        }
        compiledContextRecords.push_back(record);
        memoryContext.activeCommitments.push_front(record);
    }
    QList<MemoryRecord> promptContextRecords = selectionContext.promptContextRecords;
    QStringList suppressedPromptContextKeys;
    int stablePromptCycles = 0;
    qint64 stablePromptDurationMs = 0;
    QString promptContextReasonCode;
    const QString desktopPromptContext = selectTemporalPromptContext(
        QStringLiteral("conversation"),
        selectionContext,
        &promptContextRecords,
        &suppressedPromptContextKeys,
        &stablePromptCycles,
        &stablePromptDurationMs,
        &promptContextReasonCode);
    const QString visionContext = buildVisionPromptContext(input, IntentType::GENERAL_CHAT);
    const QString assistantPromptContext = desktopPromptContext.isEmpty()
        ? visionContext
        : (visionContext.isEmpty()
               ? desktopPromptContext
               : desktopPromptContext + QStringLiteral(" Visual context: ") + visionContext);
    if (m_loggingService) {
        m_loggingService->logBehaviorEvent(SelectionTelemetryBuilder::promptContextEvent(
            QStringLiteral("conversation"),
            input,
            m_latestDesktopContext,
            selectionContext.compiledDesktopSummary,
            desktopPromptContext,
            promptContextRecords,
            suppressedPromptContextKeys,
            stablePromptCycles,
            stablePromptDurationMs,
            promptContextReasonCode));
        m_loggingService->logBehaviorEvent(SelectionTelemetryBuilder::memoryContextEvent(
            QStringLiteral("conversation"),
            input,
            m_latestDesktopContext,
            selectionContext.compiledDesktopSummary,
            memoryContext,
            compiledContextRecords));
    }

    const ConversationRequestContext requestContext{
        .modelId = modelId,
        .input = input,
        .history = m_memoryStore->recentMessages(8),
        .memory = memoryContext,
        .identity = m_identityProfileService->identity(),
        .userProfile = m_identityProfileService->userProfile(),
        .visionContext = assistantPromptContext,
        .responseMode = m_activeActionSession.responseMode,
        .sessionGoal = m_activeActionSession.goal,
        .nextStepHint = m_activeActionSession.nextStepHint,
        .sampling = samplingProfile(),
        .streaming = m_settings->streamingEnabled(),
        .timeoutMs = effectiveRequestTimeoutMs(m_settings)
    };
    m_activeRequestId = m_aiRequestCoordinator->startConversationRequest(
        m_aiBackendClient,
        m_promptAdapter,
        requestContext,
        mode);
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
    m_previousAgentResponseId.clear();
    m_agentTrace.clear();
    emit agentTraceChanged();
    const QList<AgentToolSpec> availableTools = m_agentToolbox ? m_agentToolbox->builtInTools() : QList<AgentToolSpec>{};
    const SelectionContextCompilation selectionContext = SelectionContextCompiler::compile(
        input,
        expectedIntent,
        m_latestDesktopContext,
        m_latestDesktopContextSummary,
        m_latestDesktopContextAtMs,
        m_settings != nullptr && m_settings->privateModeEnabled(),
        runtimeToolStatusMemory(m_settings),
        buildCompiledContextHistoryMemory(),
        m_memoryPolicyHandler.get(),
        m_assistantBehaviorPolicy.get());
    const QList<MemoryRecord> compiledContextHistoryRecords = buildCompiledContextHistoryMemory();
    const QString selectionInput = selectionContext.selectionInput;
    const ToolPlan toolPlan = m_assistantBehaviorPolicy
        ? m_assistantBehaviorPolicy->buildToolPlan(selectionInput, expectedIntent, availableTools)
        : ToolPlan{};
    if (m_loggingService) {
        m_loggingService->logBehaviorEvent(SelectionTelemetryBuilder::toolPlanEvent(
            QStringLiteral("agent"),
            input,
            m_latestDesktopContext,
            selectionContext.compiledDesktopSummary,
            toolPlan));
    }
    InputRouteDecision routeDecision;
    routeDecision.kind = InputRouteKind::AgentConversation;
    routeDecision.intent = expectedIntent;
    const TrustDecision trustDecision = m_assistantBehaviorPolicy
        ? m_assistantBehaviorPolicy->assessTrust(input, routeDecision, toolPlan, m_latestDesktopContext)
        : TrustDecision{};
    m_activeActionSession = m_assistantBehaviorPolicy
        ? m_assistantBehaviorPolicy->createActionSession(input, routeDecision, toolPlan, trustDecision, m_latestDesktopContext)
        : ActionSession{};
    const QList<AgentToolSpec> relevantTools = m_assistantBehaviorPolicy
        ? m_assistantBehaviorPolicy->selectRelevantTools(selectionInput, expectedIntent, availableTools)
        : m_promptAdapter->getRelevantTools(input, expectedIntent, availableTools);
    if (m_loggingService) {
        m_loggingService->logBehaviorEvent(SelectionTelemetryBuilder::toolExposureEvent(
            QStringLiteral("agent"),
            input,
            m_latestDesktopContext,
            selectionContext.compiledDesktopSummary,
            relevantTools));
    }
    const AgentTransportMode transportMode = m_aiRequestCoordinator->resolveAgentTransport(m_agentCapabilities, modelId);
    const bool directToolCalling = transportMode == AgentTransportMode::Responses;
    m_activeAgentUsesResponses = directToolCalling;
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

    if (transportMode == AgentTransportMode::CapabilityError) {
        deliverLocalResponse(
            m_localResponseEngine->respondToError(
                QStringLiteral("error_capability"),
                buildLocalResponseContext(),
                m_activeActionSession.responseMode),
            m_aiRequestCoordinator->capabilityErrorText(m_agentCapabilities, modelId),
            true);
        return;
    }

    const QList<MemoryRecord> &memoryRecords = selectionContext.selectedMemoryRecords;
    logCompiledContextDelta(QStringLiteral("agent"), input, selectionContext);
    QList<MemoryRecord> compiledContextRecords = selectionContext.compiledContextRecords;
    MemoryContext memoryContext = selectionContext.memoryContext;
    const QList<MemoryRecord> compiledContextHistoryMemory = buildCompiledContextHistoryMemory();
    for (const MemoryRecord &record : compiledContextHistoryMemory) {
        if (record.key.trimmed().isEmpty()) {
            continue;
        }
        compiledContextRecords.push_back(record);
        memoryContext.activeCommitments.push_front(record);
    }
    QList<MemoryRecord> promptContextRecords = selectionContext.promptContextRecords;
    QStringList suppressedPromptContextKeys;
    int stablePromptCycles = 0;
    qint64 stablePromptDurationMs = 0;
    QString promptContextReasonCode;
    const QString desktopPromptContext = selectTemporalPromptContext(
        QStringLiteral("agent"),
        selectionContext,
        &promptContextRecords,
        &suppressedPromptContextKeys,
        &stablePromptCycles,
        &stablePromptDurationMs,
        &promptContextReasonCode);
    const QString visionContext = buildVisionPromptContext(input, expectedIntent);
    const QString assistantPromptContext = desktopPromptContext.isEmpty()
        ? visionContext
        : (visionContext.isEmpty()
               ? desktopPromptContext
               : desktopPromptContext + QStringLiteral(" Visual context: ") + visionContext);
    if (m_loggingService) {
        m_loggingService->logBehaviorEvent(SelectionTelemetryBuilder::promptContextEvent(
            QStringLiteral("agent"),
            input,
            m_latestDesktopContext,
            selectionContext.compiledDesktopSummary,
            desktopPromptContext,
            promptContextRecords,
            suppressedPromptContextKeys,
            stablePromptCycles,
            stablePromptDurationMs,
            promptContextReasonCode));
        m_loggingService->logBehaviorEvent(SelectionTelemetryBuilder::memoryContextEvent(
            QStringLiteral("agent"),
            input,
            m_latestDesktopContext,
            selectionContext.compiledDesktopSummary,
            memoryContext,
            compiledContextRecords));
    }

    const AgentRequestContext requestContext{
        .modelId = modelId,
        .input = input,
        .intent = expectedIntent,
        .memory = memoryContext,
        .skills = m_skillStore->listSkills(),
        .tools = relevantTools,
        .identity = m_identityProfileService->identity(),
        .userProfile = m_identityProfileService->userProfile(),
        .workspaceRoot = QDir::currentPath(),
        .visionContext = assistantPromptContext,
        .responseMode = m_activeActionSession.responseMode,
        .sessionGoal = m_activeActionSession.goal,
        .nextStepHint = m_activeActionSession.nextStepHint,
        .sampling = samplingProfile(),
        .mode = mode,
        .memoryAutoWrite = m_settings->memoryAutoWrite(),
        .timeoutMs = effectiveRequestTimeoutMs(m_settings)
    };
    const AgentStartRequestResult startResult = m_aiRequestCoordinator->startAgentRequest(
        m_aiBackendClient,
        m_promptAdapter,
        m_agentCapabilities,
        requestContext);
    m_activeAgentUsesResponses = startResult.transportMode == AgentTransportMode::Responses;
    m_activeRequestId = startResult.requestId;
}

void AssistantController::continueAgentConversation(const QList<AgentToolResult> &results)
{
    if (m_activeAgentIteration >= 6) {
        handleConversationFinished(QStringLiteral("I’ve hit the tool-call limit for this request. Please narrow it down and try again."));
        return;
    }

    ++m_activeAgentIteration;
    const QList<AgentToolSpec> availableTools = m_agentToolbox ? m_agentToolbox->builtInTools() : QList<AgentToolSpec>{};
    const SelectionContextCompilation selectionContext = SelectionContextCompiler::compile(
        m_lastAgentInput,
        m_lastAgentIntent,
        m_latestDesktopContext,
        m_latestDesktopContextSummary,
        m_latestDesktopContextAtMs,
        m_settings != nullptr && m_settings->privateModeEnabled(),
        runtimeToolStatusMemory(m_settings),
        buildCompiledContextHistoryMemory(),
        m_memoryPolicyHandler.get(),
        m_assistantBehaviorPolicy.get());
    const QList<MemoryRecord> compiledContextHistoryRecords = buildCompiledContextHistoryMemory();
    const QString selectionInput = selectionContext.selectionInput;
    QList<AgentToolSpec> relevantTools = m_assistantBehaviorPolicy
        ? m_assistantBehaviorPolicy->selectRelevantTools(selectionInput, m_lastAgentIntent, availableTools)
        : m_promptAdapter->getRelevantTools(m_lastAgentInput, m_lastAgentIntent, availableTools);
    if (m_loggingService) {
        m_loggingService->logBehaviorEvent(SelectionTelemetryBuilder::toolExposureEvent(
            QStringLiteral("agent_continuation"),
            m_lastAgentInput,
            m_latestDesktopContext,
            selectionContext.compiledDesktopSummary,
            relevantTools));
    }
    if (relevantTools.isEmpty()) {
        relevantTools = availableTools;
    }

    const QString modelId = m_aiRequestCoordinator->resolveModelId(availableModelIds());
    if (modelId.isEmpty()) {
        deliverLocalResponse(
            m_localResponseEngine->respondToError(
                QStringLiteral("ai_offline"),
                buildLocalResponseContext(),
                m_activeActionSession.responseMode),
            QStringLiteral("No local AI backend model selected"),
            true);
        return;
    }

    const QList<MemoryRecord> &memoryRecords = selectionContext.selectedMemoryRecords;
    logCompiledContextDelta(QStringLiteral("agent_continuation"), m_lastAgentInput, selectionContext);
    QList<MemoryRecord> compiledContextRecords = selectionContext.compiledContextRecords;
    MemoryContext memoryContext = selectionContext.memoryContext;
    const QList<MemoryRecord> compiledContextHistoryMemory = buildCompiledContextHistoryMemory();
    for (const MemoryRecord &record : compiledContextHistoryMemory) {
        if (record.key.trimmed().isEmpty()) {
            continue;
        }
        compiledContextRecords.push_back(record);
        memoryContext.activeCommitments.push_front(record);
    }
    QList<MemoryRecord> promptContextRecords = selectionContext.promptContextRecords;
    QStringList suppressedPromptContextKeys;
    int stablePromptCycles = 0;
    qint64 stablePromptDurationMs = 0;
    QString promptContextReasonCode;
    const QString desktopPromptContext = selectTemporalPromptContext(
        QStringLiteral("agent_continuation"),
        selectionContext,
        &promptContextRecords,
        &suppressedPromptContextKeys,
        &stablePromptCycles,
        &stablePromptDurationMs,
        &promptContextReasonCode);
    const QString visionContext = buildVisionPromptContext(m_lastAgentInput, m_lastAgentIntent);
    const QString assistantPromptContext = desktopPromptContext.isEmpty()
        ? visionContext
        : (visionContext.isEmpty()
               ? desktopPromptContext
               : desktopPromptContext + QStringLiteral(" Visual context: ") + visionContext);
    if (m_loggingService) {
        m_loggingService->logBehaviorEvent(SelectionTelemetryBuilder::promptContextEvent(
            QStringLiteral("agent_continuation"),
            m_lastAgentInput,
            m_latestDesktopContext,
            selectionContext.compiledDesktopSummary,
            desktopPromptContext,
            promptContextRecords,
            suppressedPromptContextKeys,
            stablePromptCycles,
            stablePromptDurationMs,
            promptContextReasonCode));
        m_loggingService->logBehaviorEvent(SelectionTelemetryBuilder::memoryContextEvent(
            QStringLiteral("agent_continuation"),
            m_lastAgentInput,
            m_latestDesktopContext,
            selectionContext.compiledDesktopSummary,
            memoryContext,
            compiledContextRecords));
    }

    const AgentRequestContext requestContext{
        .modelId = modelId,
        .input = m_lastAgentInput,
        .previousResponseId = m_previousAgentResponseId,
        .intent = m_lastAgentIntent,
        .memory = memoryContext,
        .skills = m_skillStore->listSkills(),
        .tools = relevantTools,
        .toolResults = results,
        .identity = m_identityProfileService->identity(),
        .userProfile = m_identityProfileService->userProfile(),
        .workspaceRoot = QDir::currentPath(),
        .visionContext = assistantPromptContext,
        .responseMode = m_activeActionSession.responseMode,
        .sessionGoal = m_activeActionSession.goal,
        .nextStepHint = m_activeActionSession.nextStepHint,
        .sampling = samplingProfile(),
        .mode = m_activeReasoningMode,
        .memoryAutoWrite = m_settings->memoryAutoWrite(),
        .timeoutMs = effectiveRequestTimeoutMs(m_settings)
    };
    m_activeRequestId = m_aiRequestCoordinator->continueAgentRequest(
        m_aiBackendClient,
        m_promptAdapter,
        m_activeAgentUsesResponses,
        requestContext);
}

QList<AgentToolResult> AssistantController::executeAgentToolCalls(const QList<AgentToolCall> &toolCalls)
{
    if (!m_toolCoordinator) {
        return {};
    }

    return m_toolCoordinator->executeAgentToolCalls(
        toolCalls,
        m_agentToolbox,
        [this](const QString &kind, const QString &title, const QString &detail, bool success) {
            appendAgentTrace(kind, title, detail, success);
        });
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

QString AssistantController::buildDesktopPromptContext(const QString &input, IntentType intent) const
{
    if (m_settings == nullptr || m_settings->privateModeEnabled()) {
        return {};
    }
    if (m_latestDesktopContextSummary.trimmed().isEmpty()) {
        return {};
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_latestDesktopContextAtMs <= 0 || (nowMs - m_latestDesktopContextAtMs) > 90000) {
        return {};
    }
    if (!shouldUseDesktopContextForPrompt(input, intent)) {
        return {};
    }

    return SelectionContextCompiler::buildPromptContext(
        intent,
        m_latestDesktopContextSummary,
        m_latestDesktopContext,
        buildCompiledContextHistoryMemory());
}

QString AssistantController::buildAssistantPromptContext(const QString &input, IntentType intent) const
{
    const QString desktopContext = buildDesktopPromptContext(input, intent);
    const QString visionContext = buildVisionPromptContext(input, intent);
    if (desktopContext.isEmpty()) {
        return visionContext;
    }
    if (visionContext.isEmpty()) {
        return desktopContext;
    }
    return desktopContext + QStringLiteral(" Visual context: ") + visionContext;
}

QString AssistantController::buildDesktopSelectionInput(const QString &input,
                                                       IntentType intent,
                                                       const QString &purpose) const
{
    const QString compiledDesktopSummary = SelectionContextCompiler::buildCompiledDesktopSummary(
        m_latestDesktopContext,
        m_latestDesktopContextSummary);
    const QList<MemoryRecord> compiledContextHistoryRecords = buildCompiledContextHistoryMemory();
    const QString selectionInput = SelectionContextCompiler::buildSelectionInput(
        input,
        intent,
        m_latestDesktopContext,
        m_latestDesktopContextSummary,
        m_latestDesktopContextAtMs,
        m_settings != nullptr && m_settings->privateModeEnabled(),
        compiledContextHistoryRecords);
    if (m_loggingService != nullptr && selectionInput != input) {
        BehaviorTraceEvent event = BehaviorTraceEvent::create(
            QStringLiteral("selection_context"),
            QStringLiteral("compiled"),
            QStringLiteral("selection.desktop_context_applied"),
            {
                {QStringLiteral("purpose"), purpose},
                {QStringLiteral("intent"), QString::number(static_cast<int>(intent))},
                {QStringLiteral("desktopSummary"), compiledDesktopSummary},
                {QStringLiteral("desktopTaskId"), m_latestDesktopContext.value(QStringLiteral("taskId")).toString()},
                {QStringLiteral("desktopThreadId"), m_latestDesktopContext.value(QStringLiteral("threadId")).toString()},
                {QStringLiteral("desktopTopic"), m_latestDesktopContext.value(QStringLiteral("topic")).toString()},
                {QStringLiteral("inputPreview"), input.left(160)}
            },
            QStringLiteral("system"));
        event.capabilityId = QStringLiteral("desktop_context_selector");
        event.threadId = m_latestDesktopContext.value(QStringLiteral("threadId")).toString();
        m_loggingService->logBehaviorEvent(event);
    }
    return selectionInput;
}

void AssistantController::updateDesktopContext(const QString &summary, const QVariantMap &context)
{
    const QString normalizedSummary = summary.simplified();
    if (normalizedSummary.isEmpty()) {
        return;
    }

    m_latestDesktopContextSummary = normalizedSummary;
    m_latestDesktopContext = context;
    m_latestDesktopContextAtMs = QDateTime::currentMSecsSinceEpoch();
    considerDesktopContextSuggestion(normalizedSummary, context);
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

    deliverLocalResponse(
        m_executionNarrator
            ? m_executionNarrator->gestureReply(QStringLiteral("farewell"))
            : QStringLiteral("Bye."),
        QStringLiteral("Gesture farewell"),
        true);
    endConversationSession();
}

void AssistantController::handleGestureConfirm()
{
    if (m_currentState == AssistantState::Listening) {
        stopListening();
    }

    if (m_hasPendingConfirmation) {
        const InputRouteDecision pendingDecision = m_pendingRouteDecision;
        const QString pendingInput = m_pendingRouteInput;
        const LocalIntent pendingLocalIntent = m_pendingLocalIntent;
        clearPendingConfirmation();
        executeRouteDecision(pendingDecision, pendingInput, pendingLocalIntent, true, QDateTime::currentMSecsSinceEpoch());
        return;
    }

    deliverLocalResponse(
        m_executionNarrator->gestureReply(QStringLiteral("confirm")),
        QStringLiteral("Gesture confirm"),
        true);
}

void AssistantController::handleGestureReject()
{
    if (m_currentState == AssistantState::Listening) {
        stopListening();
    } else if (m_currentState == AssistantState::Processing || m_currentState == AssistantState::Speaking) {
        cancelActiveRequest();
    }

    if (m_hasPendingConfirmation) {
        const ActionSession pendingSession = m_pendingActionSession;
        clearPendingConfirmation();
        deliverLocalResponse(
            m_executionNarrator
                ? m_executionNarrator->confirmationCanceled(pendingSession)
                : QStringLiteral("Okay, I won't run that action."),
            QStringLiteral("Action canceled"),
            true);
        return;
    }

    deliverLocalResponse(
        m_executionNarrator
            ? m_executionNarrator->gestureReply(QStringLiteral("reject"))
            : QStringLiteral("Canceled."),
        QStringLiteral("Gesture reject"),
        true);
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
    if (m_assistantBehaviorPolicy) {
        InputRouteDecision decision;
        decision.kind = InputRouteKind::CommandExtraction;
        const QString selectionInput = buildDesktopSelectionInput(
            input,
            IntentType::GENERAL_CHAT,
            QStringLiteral("command.selection"));
        const ToolPlan plan = m_assistantBehaviorPolicy->buildToolPlan(
            selectionInput,
            IntentType::GENERAL_CHAT,
            m_agentToolbox ? m_agentToolbox->builtInTools() : QList<AgentToolSpec>{});
        const TrustDecision trust = m_assistantBehaviorPolicy->assessTrust(input, decision, plan, m_latestDesktopContext);
        m_activeActionSession = m_assistantBehaviorPolicy->createActionSession(input, decision, plan, trust, m_latestDesktopContext);
    }
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Starting command extraction request. model=\"%1\" input=\"%2\"")
            .arg(modelId, input.left(240)));
    }
    m_activeRequestId = m_aiRequestCoordinator->startCommandRequest(
        m_aiBackendClient,
        m_promptAdapter,
        {
            .modelId = modelId,
            .input = input,
            .identity = m_identityProfileService->identity(),
            .userProfile = m_identityProfileService->userProfile(),
            .responseMode = m_activeActionSession.responseMode,
            .sessionGoal = m_activeActionSession.goal,
            .timeoutMs = effectiveRequestTimeoutMs(m_settings),
            .temperature = m_settings->toolUseTemperature(),
            .topP = m_settings->conversationTopP(),
            .providerTopK = m_settings->providerTopK(),
            .maxTokens = m_settings->maxOutputTokens()
        });
}

void AssistantController::handleConversationFinished(const QString &text)
{
    const SpokenReply reply = parseSpokenReply(text);
    m_streamAssembler->drainRemainingText();
    finalizeReply(QStringLiteral("conversation"),
                  reply,
                  QStringLiteral("Response ready"),
                  conversationSessionRestartDelayMs());
}

void AssistantController::handleHybridAgentFinished(const QString &payload)
{
    appendAgentTrace(QStringLiteral("model"), QStringLiteral("Hybrid agent response"), QStringLiteral("Received hybrid payload"), true);

    const QString jsonPayload = extractJsonObjectPayload(payload);
    const auto json = nlohmann::json::parse(jsonPayload.toStdString(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        appendAgentTrace(QStringLiteral("validation"), QStringLiteral("Hybrid payload rejected"), QStringLiteral("The model returned invalid JSON."), false);
        deliverLocalResponse(
            m_executionNarrator
                ? m_executionNarrator->validationFailure(m_activeActionSession)
                : m_localResponseEngine->respondToError(
                    QStringLiteral("error_invalid"),
                    buildLocalResponseContext(),
                    m_activeActionSession.responseMode),
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

    QList<AgentToolCall> toolCalls;
    for (const AgentToolCall &call : parseAdapterToolCalls(json)) {
        if (!allowedTaskTypes.isEmpty() && !allowedTaskTypes.contains(call.name)) {
            appendAgentTrace(QStringLiteral("validation"),
                             QStringLiteral("Rejected tool call"),
                             QStringLiteral("Tool %1 is not allowed for this intent.").arg(call.name),
                             false);
            continue;
        }
        toolCalls.push_back(call);
    }

    if (intentRequiresTool(returnedIntent) && toolCalls.isEmpty()) {
        appendAgentTrace(QStringLiteral("validation"),
                         QStringLiteral("Hybrid payload rejected"),
                         QStringLiteral("A tool-backed intent was returned without a valid tool call."),
                         false);
        deliverLocalResponse(
            m_executionNarrator->validationFailure(m_activeActionSession),
            QStringLiteral("Adapter tool call invalid"),
            true);
        return;
    }

    if (!toolCalls.isEmpty()) {
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("Chat adapter produced %1 tool calls; executing through unified agent loop.")
                                       .arg(toolCalls.size()));
        }
        continueAgentConversation(executeAgentToolCalls(toolCalls));
        return;
    }

    if (m_settings->memoryAutoWrite()) {
        m_memoryPolicyHandler->captureExplicitMemoryFromInput(m_lastAgentInput);
    }

    const SpokenReply reply = parseSpokenReply(
        message.isEmpty()
            ? (m_executionNarrator
                ? m_executionNarrator->outcomeSummary(m_activeActionSession, true, QStringLiteral("I finished that request."))
                : QStringLiteral("I finished that request."))
            : message);
    if (m_activeActionSession.responseMode == ResponseMode::Act
        || m_activeActionSession.responseMode == ResponseMode::ActWithProgress
        || m_activeActionSession.responseMode == ResponseMode::Recover) {
        rememberCompletedActionReply(
            m_activeActionSession.selectedTools.isEmpty() ? QStringLiteral("assistant_action") : m_activeActionSession.selectedTools.first(),
            reply.displayText,
            true,
            QDateTime::currentMSecsSinceEpoch());
    }
    finalizeReply(QStringLiteral("agent"),
                  reply,
                  QStringLiteral("Response ready"),
                  conversationSessionRestartDelayMs(),
                  true);
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
        continueAgentConversation(executeAgentToolCalls(response.toolCalls));
        return;
    }

    if (m_settings->memoryAutoWrite()) {
        m_memoryPolicyHandler->captureExplicitMemoryFromInput(m_lastAgentInput);
    }

    const SpokenReply reply = parseSpokenReply(
        response.outputText.trimmed().isEmpty()
            ? (m_executionNarrator
                ? m_executionNarrator->outcomeSummary(m_activeActionSession, true, QStringLiteral("I finished that request."))
                : QStringLiteral("I finished that request."))
            : response.outputText);
    if (m_activeActionSession.responseMode == ResponseMode::Act
        || m_activeActionSession.responseMode == ResponseMode::ActWithProgress
        || m_activeActionSession.responseMode == ResponseMode::Recover) {
        rememberCompletedActionReply(
            m_activeActionSession.selectedTools.isEmpty() ? QStringLiteral("assistant_action") : m_activeActionSession.selectedTools.first(),
            reply.displayText,
            true,
            QDateTime::currentMSecsSinceEpoch());
    }
    finalizeReply(QStringLiteral("agent"),
                  reply,
                  QStringLiteral("Response ready"),
                  conversationSessionRestartDelayMs(),
                  true);
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
    const QString message = m_executionNarrator
        ->commandSummary(m_activeActionSession, command.target, result);
    SpokenReply reply;
    reply.displayText = message;
    reply.spokenText = message;
    reply.shouldSpeak = true;
    rememberCompletedActionReply(command.target.trimmed().isEmpty() ? QStringLiteral("device_action") : command.target.trimmed(),
                                 message,
                                 true,
                                 QDateTime::currentMSecsSinceEpoch());
    finalizeReply(QStringLiteral("command"),
                  reply,
                  QStringLiteral("Command executed"),
                  conversationSessionRestartDelayMs());
}

void AssistantController::dispatchBackgroundTasks(const QList<AgentTask> &tasks)
{
    if (m_toolCoordinator == nullptr || m_taskDispatcher == nullptr || tasks.isEmpty()) {
        return;
    }

    m_toolCoordinator->dispatchBackgroundTasks(m_taskDispatcher, tasks);
    emit assistantSurfaceChanged();
}

void AssistantController::recordConnectorEvent(const ConnectorEvent &event)
{
    if (!event.isValid()) {
        return;
    }

    if (m_loggingService != nullptr) {
        QVariantMap payload = event.toVariantMap();
        payload.insert(QStringLiteral("desktopSummary"), m_latestDesktopContextSummary);
        BehaviorTraceEvent traceEvent = BehaviorTraceEvent::create(
            QStringLiteral("connector_event"),
            QStringLiteral("ingested"),
            QStringLiteral("connector_event.live_ingested"),
            payload,
            QStringLiteral("system"));
        traceEvent.capabilityId = QStringLiteral("connector_event_stream");
        traceEvent.threadId = m_latestDesktopContext.value(QStringLiteral("threadId")).toString();
        m_loggingService->logBehaviorEvent(traceEvent);
    }

    considerConnectorEvent(event);
}

void AssistantController::recordTaskResult(const QJsonObject &resultObject)
{
    if (m_toolCoordinator == nullptr) {
        return;
    }

    const ToolResultHandling handling = m_toolCoordinator->handleTaskResult(resultObject, m_backgroundPanelVisible);
    if (handling.ignored) {
        return;
    }

    if (handling.surfaceChanged) {
        emit assistantSurfaceChanged();
    }
    if (handling.resultsChanged) {
        emit backgroundTaskResultsChanged();
    }
    if (handling.toastChanged) {
        const BehaviorDecision toastDecision = ProactiveSurfaceGate::evaluateTaskToast({
            .result = handling.completedResult.value_or(BackgroundTaskResult{}),
            .desktopContext = m_latestDesktopContext,
            .desktopContextAtMs = m_latestDesktopContextAtMs,
            .cooldownState = m_proactiveCooldownState,
            .focusMode = FocusModeState{
                .enabled = m_settings != nullptr && m_settings->focusModeEnabled(),
                .allowCriticalAlerts = m_settings != nullptr && m_settings->focusModeAllowCriticalAlerts(),
                .durationMinutes = m_settings != nullptr ? m_settings->focusModeDurationMinutes() : 0,
                .untilEpochMs = m_settings != nullptr ? m_settings->focusModeUntilEpochMs() : 0,
                .source = QStringLiteral("settings")
            },
            .nowMs = QDateTime::currentMSecsSinceEpoch()
        });
        if (m_loggingService) {
            QVariantMap payload = toastDecision.toVariantMap();
            payload.insert(QStringLiteral("taskId"), handling.completedResult.has_value() ? handling.completedResult->taskId : -1);
            payload.insert(QStringLiteral("taskType"), handling.completedResult.has_value() ? handling.completedResult->type : QString());
            payload.insert(QStringLiteral("desktopSummary"), m_latestDesktopContextSummary);
            BehaviorTraceEvent event = BehaviorTraceEvent::create(
                QStringLiteral("ui_presentation"),
                QStringLiteral("gated"),
                toastDecision.reasonCode,
                payload,
                QStringLiteral("system"));
            event.capabilityId = QStringLiteral("proactive_surface_gate");
            event.threadId = m_latestDesktopContext.value(QStringLiteral("threadId")).toString();
            m_loggingService->logBehaviorEvent(event);
        }
        if (toastDecision.allowed) {
            if (handling.completedResult.has_value()) {
                commitProactivePresentation(
                    QStringLiteral("task_toast"),
                    handling.completedResult->type,
                    handling.completedResult->success ? QStringLiteral("medium") : QStringLiteral("high"),
                    toastDecision.reasonCode);
            }
            emit latestTaskToastChanged();
        }
    }
    if (handling.appendTrace) {
        appendAgentTrace(handling.traceKind, handling.traceTitle, handling.traceDetail, handling.traceSuccess);
    }
    if (handling.connectorEvent.has_value()
        && (!handling.completedResult.has_value() || !handling.completedResult->connectorEventLive)
        && m_loggingService != nullptr) {
        BehaviorTraceEvent event = BehaviorTraceEvent::create(
            QStringLiteral("connector_event"),
            QStringLiteral("ingested"),
            QStringLiteral("connector_event.ingested"),
            handling.connectorEvent->toVariantMap(),
            QStringLiteral("system"));
        event.capabilityId = QStringLiteral("connector_event_builder");
        event.threadId = m_latestDesktopContext.value(QStringLiteral("threadId")).toString();
        m_loggingService->logBehaviorEvent(event);
    }
    if (!handling.completedResult.has_value()) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    rememberActionThreadResult(*handling.completedResult, nowMs);

    const ProactiveSurfaceGate::Input followUpInput{
        .result = *handling.completedResult,
        .desktopContext = m_latestDesktopContext,
        .desktopContextAtMs = m_latestDesktopContextAtMs,
        .cooldownState = m_proactiveCooldownState,
        .focusMode = FocusModeState{
            .enabled = m_settings != nullptr && m_settings->focusModeEnabled(),
            .allowCriticalAlerts = m_settings != nullptr && m_settings->focusModeAllowCriticalAlerts(),
            .durationMinutes = m_settings != nullptr ? m_settings->focusModeDurationMinutes() : 0,
            .untilEpochMs = m_settings != nullptr ? m_settings->focusModeUntilEpochMs() : 0,
            .source = QStringLiteral("settings")
        },
        .nowMs = nowMs
    };

    if (m_currentState == AssistantState::Processing) {
        return;
    }

    if (m_recentActionThread.has_value()
        && m_recentActionThread->success
        && m_recentActionThread->hasArtifacts()) {
        const BehaviorDecision completionDecision = ProactiveSurfaceGate::evaluateCompletionFollowUp(
            followUpInput,
            true);
        if (m_loggingService) {
            m_loggingService->infoFor(
                QStringLiteral("follow_up_audit"),
                QStringLiteral("[completion_follow_up_gate] allowed=%1 reason=%2 taskId=%3 taskType=%4")
                    .arg(completionDecision.allowed ? QStringLiteral("true") : QStringLiteral("false"),
                         completionDecision.reasonCode,
                         QString::number(handling.completedResult->taskId),
                         handling.completedResult->type));
        }
        if (m_loggingService) {
            QVariantMap payload = completionDecision.toVariantMap();
            payload.insert(QStringLiteral("taskId"), handling.completedResult->taskId);
            payload.insert(QStringLiteral("taskType"), handling.completedResult->type);
            payload.insert(QStringLiteral("desktopSummary"), m_latestDesktopContextSummary);
            payload.insert(QStringLiteral("surfaceKind"), QStringLiteral("completion_request"));
            BehaviorTraceEvent event = BehaviorTraceEvent::create(
                QStringLiteral("ui_presentation"),
                QStringLiteral("gated_follow_up"),
                completionDecision.reasonCode,
                payload,
                QStringLiteral("system"));
            event.capabilityId = QStringLiteral("proactive_surface_gate");
            event.threadId = m_latestDesktopContext.value(QStringLiteral("threadId")).toString();
            m_loggingService->logBehaviorEvent(event);
        }
        if (completionDecision.allowed) {
            if (m_loggingService) {
                m_loggingService->infoFor(
                    QStringLiteral("follow_up_audit"),
                    QStringLiteral("[completion_follow_up_action] starting completion request for thread=%1")
                        .arg(m_recentActionThread->id));
            }
            startActionThreadCompletionRequest(*m_recentActionThread);
            return;
        }
    }

    const QString fallbackSummary = m_executionNarrator
        ? m_executionNarrator->summarizeBackgroundResult(*handling.completedResult)
        : (handling.completedResult->summary.isEmpty()
            ? handling.completedResult->detail
            : handling.completedResult->summary);
    ActionSession outcomeSession = m_activeActionSession;
    if (m_recentActionThread.has_value()) {
        outcomeSession.nextStepHint = m_recentActionThread->nextStepHint;
        outcomeSession.successSummary = m_recentActionThread->resultSummary;
        outcomeSession.failureSummary = m_recentActionThread->resultSummary;
    }
    const QString message = m_executionNarrator
        ? m_executionNarrator->outcomeSummary(
            outcomeSession,
            handling.completedResult->success,
            fallbackSummary)
        : fallbackSummary;
    if (!message.trimmed().isEmpty()) {
        const BehaviorDecision followUpDecision = ProactiveSurfaceGate::evaluateCompletionFollowUp(
            followUpInput,
            false);
        if (m_loggingService) {
            m_loggingService->infoFor(
                QStringLiteral("follow_up_audit"),
                QStringLiteral("[spoken_follow_up_gate] allowed=%1 reason=%2 taskId=%3 taskType=%4 connectorEventLive=%5")
                    .arg(followUpDecision.allowed ? QStringLiteral("true") : QStringLiteral("false"),
                         followUpDecision.reasonCode,
                         QString::number(handling.completedResult->taskId),
                         handling.completedResult->type,
                         handling.completedResult->connectorEventLive ? QStringLiteral("true") : QStringLiteral("false")));
        }
        if (m_loggingService) {
            QVariantMap payload = followUpDecision.toVariantMap();
            payload.insert(QStringLiteral("taskId"), handling.completedResult->taskId);
            payload.insert(QStringLiteral("taskType"), handling.completedResult->type);
            payload.insert(QStringLiteral("desktopSummary"), m_latestDesktopContextSummary);
            payload.insert(QStringLiteral("surfaceKind"), QStringLiteral("spoken_follow_up"));
            BehaviorTraceEvent event = BehaviorTraceEvent::create(
                QStringLiteral("ui_presentation"),
                QStringLiteral("gated_follow_up"),
                followUpDecision.reasonCode,
                payload,
                QStringLiteral("system"));
            event.capabilityId = QStringLiteral("proactive_surface_gate");
            event.threadId = m_latestDesktopContext.value(QStringLiteral("threadId")).toString();
            m_loggingService->logBehaviorEvent(event);
        }
        if (!followUpDecision.allowed) {
            if (m_loggingService) {
                m_loggingService->warnFor(
                    QStringLiteral("follow_up_audit"),
                    QStringLiteral("[spoken_follow_up_suppressed] reason=%1 taskId=%2")
                        .arg(followUpDecision.reasonCode,
                             QString::number(handling.completedResult->taskId)));
            }
            if (handling.completedResult->connectorEventLive) {
                return;
            }
            if (handling.connectorEvent.has_value()) {
                considerConnectorEvent(*handling.connectorEvent);
            } else {
                considerTaskResultSuggestion(*handling.completedResult);
            }
            return;
        }
        if (outcomeSession.nextStepHint.trimmed().isEmpty()) {
            commitProactivePresentation(
                QStringLiteral("spoken_follow_up"),
                handling.completedResult->type,
                handling.completedResult->success ? QStringLiteral("medium") : QStringLiteral("high"),
                followUpDecision.reasonCode);
        }
        const QString status = m_executionNarrator
            ? m_executionNarrator->statusForBackgroundResult(*handling.completedResult)
            : (handling.completedResult->success
                ? QStringLiteral("Task finished")
                : QStringLiteral("Task failed"));
        if (m_loggingService) {
            m_loggingService->infoFor(
                QStringLiteral("follow_up_audit"),
                QStringLiteral("[spoken_follow_up_delivered] status=%1 taskId=%2")
                    .arg(status, QString::number(handling.completedResult->taskId)));
        }
        deliverLocalResponse(message, status, true);
        return;
    }

    if (handling.completedResult->connectorEventLive) {
        return;
    }
    if (handling.connectorEvent.has_value()) {
        considerConnectorEvent(*handling.connectorEvent);
    } else {
        considerTaskResultSuggestion(*handling.completedResult);
    }
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

void AssistantController::startActionThreadCompletionRequest(const ActionThread &thread)
{
    if (!thread.hasArtifacts()) {
        return;
    }

    ActionThread gatedThread = thread;
    gatedThread.nextStepHint = gateNextStepHint(
        thread.nextStepHint,
        QStringLiteral("completion_prompt"),
        thread.taskType,
        thread.success);

    m_lastPromptForAiLog = gatedThread.userGoal.trimmed().isEmpty()
        ? gatedThread.taskType
        : gatedThread.userGoal;
    startConversationRequest(buildActionThreadCompletionInput(gatedThread));
}

bool AssistantController::shouldContinueActionThread(const QString &input,
                                                     const InputRouteDecision &decision,
                                                     qint64 nowMs) const
{
    if (!m_recentActionThread.has_value() || m_assistantBehaviorPolicy == nullptr) {
        return false;
    }

    return m_assistantBehaviorPolicy->shouldContinueActionThread(
        input,
        decision,
        *m_recentActionThread,
        nowMs);
}

QString AssistantController::buildActionThreadContinuationInput(const QString &userInput) const
{
    if (!m_recentActionThread.has_value()) {
        return userInput;
    }

    const ActionThread &thread = *m_recentActionThread;
    const QString sources = thread.sourceUrls.join(QStringLiteral("\n"));
    const QString stateText = thread.state == ActionThreadState::Running
        ? QStringLiteral("running")
        : (thread.state == ActionThreadState::Completed
            ? QStringLiteral("completed")
            : (thread.state == ActionThreadState::Failed
                ? QStringLiteral("failed")
                : QStringLiteral("canceled")));

    return QStringLiteral(
        "You are continuing the current assistant action thread.\n"
        "Treat the user's message as a follow-up to this task when appropriate. "
        "Only start a brand-new unrelated task if the user clearly asks for one.\n\n"
        "Thread state: %1\n"
        "Task type: %2\n"
        "User goal: %3\n"
        "Result summary: %4\n"
        "Artifacts:\n%5\n"
        "Sources:\n%6\n"
        "Suggested next step: %7\n\n"
        "User follow-up: %8")
        .arg(stateText,
             thread.taskType.trimmed().isEmpty() ? QStringLiteral("task") : thread.taskType.trimmed(),
             thread.userGoal.trimmed().isEmpty() ? QStringLiteral("unknown") : thread.userGoal.trimmed(),
             thread.resultSummary.trimmed().isEmpty() ? QStringLiteral("none") : thread.resultSummary.trimmed(),
             thread.artifactText.trimmed().isEmpty() ? QStringLiteral("none") : thread.artifactText.trimmed(),
             sources.trimmed().isEmpty() ? QStringLiteral("none") : sources.trimmed(),
             thread.nextStepHint.trimmed().isEmpty() ? QStringLiteral("none") : thread.nextStepHint.trimmed(),
             userInput.trimmed());
}

QString AssistantController::buildActionThreadCompletionInput(const ActionThread &thread) const
{
    const QString sources = thread.sourceUrls.join(QStringLiteral("\n"));
    return QStringLiteral(
        "A task just completed.\n"
        "Give the user a short useful completion summary grounded only in the task result below. "
        "If there is an obvious next step, suggest one short follow-up.\n\n"
        "Task type: %1\n"
        "User goal: %2\n"
        "Result summary: %3\n"
        "Artifacts:\n%4\n"
        "Sources:\n%5\n"
        "Suggested next step: %6")
        .arg(thread.taskType.trimmed().isEmpty() ? QStringLiteral("task") : thread.taskType.trimmed(),
             thread.userGoal.trimmed().isEmpty() ? QStringLiteral("unknown") : thread.userGoal.trimmed(),
             thread.resultSummary.trimmed().isEmpty() ? QStringLiteral("none") : thread.resultSummary.trimmed(),
             thread.artifactText.trimmed().isEmpty() ? QStringLiteral("none") : thread.artifactText.trimmed(),
             sources.trimmed().isEmpty() ? QStringLiteral("none") : sources.trimmed(),
             thread.nextStepHint.trimmed().isEmpty() ? QStringLiteral("none") : thread.nextStepHint.trimmed());
}

void AssistantController::beginActionThread(const QList<AgentTask> &tasks, qint64 nowMs)
{
    if (tasks.isEmpty()) {
        return;
    }

    ActionThread thread;
    thread.id = m_activeActionSession.id;
    thread.taskType = taskTypeForTasks(tasks);
    thread.userGoal = m_activeActionSession.userRequest.trimmed().isEmpty()
        ? m_activeActionSession.goal
        : m_activeActionSession.userRequest.trimmed();
    thread.resultSummary = m_executionNarrator
        ? m_executionNarrator->preActionText(m_activeActionSession, QStringLiteral("Working on it."))
        : QStringLiteral("Working on it.");
    thread.nextStepHint = planNextStepSuggestion(
        QStringLiteral("task_start"),
        thread.taskType,
        thread.resultSummary,
        {},
        {},
        {},
        true).selectedSummary;
    thread.state = ActionThreadState::Running;
    thread.success = false;
    thread.valid = true;
    thread.updatedAtMs = nowMs;
    thread.expiresAtMs = nowMs + conversationSessionTimeoutMs();
    m_recentActionThread = thread;
}

void AssistantController::rememberActionThreadResult(const BackgroundTaskResult &result, qint64 nowMs)
{
    ActionThread thread;
    if (m_recentActionThread.has_value()) {
        thread = *m_recentActionThread;
    } else {
        thread.id = m_activeActionSession.id;
        thread.userGoal = m_activeActionSession.userRequest.trimmed().isEmpty()
            ? m_activeActionSession.goal
            : m_activeActionSession.userRequest.trimmed();
    }

    thread.taskType = result.type.trimmed().isEmpty() ? thread.taskType : result.type.trimmed();
    thread.resultSummary = m_executionNarrator
        ? m_executionNarrator->summarizeBackgroundResult(result)
        : (result.summary.trimmed().isEmpty() ? result.detail.trimmed() : result.summary.trimmed());
    thread.artifactText = clippedBackgroundPayload(result);
    thread.payload = result.payload;
    thread.sourceUrls = sourceUrlsForResult(result);
    thread.nextStepHint = planNextStepSuggestion(
        QStringLiteral("task_result"),
        thread.taskType,
        thread.resultSummary,
        thread.sourceUrls,
        result.payload.toVariantMap(),
        result.taskKey,
        result.success).selectedSummary;
    thread.state = result.success ? ActionThreadState::Completed : ActionThreadState::Failed;
    if (result.state == TaskState::Canceled) {
        thread.state = ActionThreadState::Canceled;
    }
    thread.success = result.success;
    thread.valid = true;
    thread.updatedAtMs = nowMs;
    thread.expiresAtMs = nowMs + conversationSessionTimeoutMs();
    m_recentActionThread = thread;
}

void AssistantController::rememberCompletedActionReply(const QString &taskType,
                                                       const QString &summary,
                                                       bool success,
                                                       qint64 nowMs)
{
    ActionThread thread;
    thread.id = m_activeActionSession.id;
    thread.taskType = taskType.trimmed().isEmpty() ? QStringLiteral("assistant_action") : taskType.trimmed();
    thread.userGoal = m_activeActionSession.userRequest.trimmed().isEmpty()
        ? m_activeActionSession.goal
        : m_activeActionSession.userRequest.trimmed();
    thread.resultSummary = summary.trimmed();
    thread.nextStepHint = planNextStepSuggestion(
        QStringLiteral("reply_result"),
        thread.taskType,
        thread.resultSummary,
        {},
        {},
        {},
        success).selectedSummary;
    thread.state = success ? ActionThreadState::Completed : ActionThreadState::Failed;
    thread.success = success;
    thread.valid = true;
    thread.updatedAtMs = nowMs;
    thread.expiresAtMs = nowMs + conversationSessionTimeoutMs();
    m_recentActionThread = thread;
}

void AssistantController::clearActionThread()
{
    m_recentActionThread.reset();
}

bool AssistantController::handlePendingConfirmationInput(const QString &input)
{
    if (!m_hasPendingConfirmation || m_assistantBehaviorPolicy == nullptr) {
        return false;
    }

    if (m_assistantBehaviorPolicy->isConfirmationReply(input, m_pendingActionSession)) {
        const ActionRiskPermissionEvaluation evaluation = ActionRiskPermissionService::evaluate(
            m_pendingActionSession.toolPlan,
            m_pendingActionSession.trust,
            false,
            PermissionOverrideSettings::rulesFromSettings(m_settings));
        if (m_loggingService) {
            m_loggingService->infoFor(
                QStringLiteral("safety_audit"),
                QStringLiteral("[confirmation_reply] accepted input=%1")
                    .arg(userFacingPromptForLogging(input).left(240)));
            m_loggingService->logBehaviorEvent(ActionRiskPermissionService::confirmationOutcomeEvent(
                evaluation,
                routeKindToString(m_pendingRouteDecision.kind),
                m_pendingActionSession,
                QStringLiteral("approved"),
                userFacingPromptForLogging(input),
                m_latestDesktopContext));
        }
        const InputRouteDecision pendingDecision = m_pendingRouteDecision;
        const QString pendingInput = m_pendingRouteInput;
        const LocalIntent pendingLocalIntent = m_pendingLocalIntent;
        clearPendingConfirmation();
        executeRouteDecision(pendingDecision, pendingInput, pendingLocalIntent, true, QDateTime::currentMSecsSinceEpoch());
        return true;
    }

    if (m_assistantBehaviorPolicy->isRejectionReply(input)) {
        const ActionRiskPermissionEvaluation evaluation = ActionRiskPermissionService::evaluate(
            m_pendingActionSession.toolPlan,
            m_pendingActionSession.trust,
            false,
            PermissionOverrideSettings::rulesFromSettings(m_settings));
        if (m_loggingService) {
            m_loggingService->infoFor(
                QStringLiteral("safety_audit"),
                QStringLiteral("[confirmation_reply] rejected input=%1")
                    .arg(userFacingPromptForLogging(input).left(240)));
            m_loggingService->logBehaviorEvent(ActionRiskPermissionService::confirmationOutcomeEvent(
                evaluation,
                routeKindToString(m_pendingRouteDecision.kind),
                m_pendingActionSession,
                QStringLiteral("rejected"),
                userFacingPromptForLogging(input),
                m_latestDesktopContext));
        }
        const ActionSession pendingSession = m_pendingActionSession;
        clearPendingConfirmation();
        deliverLocalResponse(
            m_executionNarrator
                ? m_executionNarrator->confirmationCanceled(pendingSession)
                : QStringLiteral("Okay, I won't run that action."),
            QStringLiteral("Action canceled"),
            true);
        return true;
    }

    if (m_loggingService) {
        const ActionRiskPermissionEvaluation evaluation = ActionRiskPermissionService::evaluate(
            m_pendingActionSession.toolPlan,
            m_pendingActionSession.trust,
            false,
            PermissionOverrideSettings::rulesFromSettings(m_settings));
        m_loggingService->warnFor(
            QStringLiteral("safety_audit"),
            QStringLiteral("[confirmation_reply] unrecognized input=%1; pending state cleared")
                .arg(userFacingPromptForLogging(input).left(240)));
        m_loggingService->logBehaviorEvent(ActionRiskPermissionService::confirmationOutcomeEvent(
            evaluation,
            routeKindToString(m_pendingRouteDecision.kind),
            m_pendingActionSession,
            QStringLiteral("unrecognized"),
            userFacingPromptForLogging(input),
            m_latestDesktopContext));
    }
    clearPendingConfirmation();
    return false;
}

void AssistantController::storePendingConfirmation(const InputRouteDecision &decision,
                                                   const QString &input,
                                                   LocalIntent localIntent)
{
    m_hasPendingConfirmation = true;
    m_pendingActionSession = m_activeActionSession;
    m_pendingRouteDecision = decision;
    m_pendingRouteInput = input;
    m_pendingLocalIntent = localIntent;
    if (m_loggingService) {
        m_loggingService->warnFor(
            QStringLiteral("safety_audit"),
            QStringLiteral("[pending_confirmation_stored] kind=%1 intent=%2 input=%3")
                .arg(routeKindToString(decision.kind),
                     intentTypeToString(decision.intent),
                     userFacingPromptForLogging(input).left(300)));
    }
}

void AssistantController::clearPendingConfirmation()
{
    if (m_loggingService && m_hasPendingConfirmation) {
        m_loggingService->infoFor(
            QStringLiteral("safety_audit"),
            QStringLiteral("[pending_confirmation_cleared]"));
    }
    m_hasPendingConfirmation = false;
    m_pendingActionSession = ActionSession{};
    m_pendingRouteDecision = InputRouteDecision{};
    m_pendingRouteInput.clear();
    m_pendingLocalIntent = LocalIntent::Unknown;
}

bool AssistantController::requiresConfirmationFor(const InputRouteDecision &decision) const
{
    switch (decision.kind) {
    case InputRouteKind::BackgroundTasks:
    case InputRouteKind::DeterministicTasks:
    case InputRouteKind::AgentConversation:
    case InputRouteKind::CommandExtraction:
        return true;
    case InputRouteKind::None:
    case InputRouteKind::LocalResponse:
    case InputRouteKind::Conversation:
    case InputRouteKind::AgentCapabilityError:
    default:
        return false;
    }
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

    m_loggingService->infoFor(
        QStringLiteral("ai_prompt"),
        QStringLiteral("[assistant_exchange] source=%1 status=%2\n--- user ---\n%3\n--- assistant ---\n%4")
            .arg(source,
                 status,
                 userFacingPromptForLogging(prompt),
                 response.left(12000)));

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
