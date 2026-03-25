#include "core/AssistantController.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QTime>
#include <QVector>

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
#include "core/IntentRouter.h"
#include "core/LocalResponseEngine.h"
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
#include "wakeword/SherpaWakeWordEngine.h"
#include "wakeword/WakeWordEngine.h"
#include "wakeword/WakeWordEnginePrecise.h"
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
        return QStringLiteral("PROCESSING");
    case AssistantState::Speaking:
        return QStringLiteral("SPEAKING");
    }
    return QStringLiteral("IDLE");
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

QString normalizeWakeToken(const QString &value)
{
    QString normalized = value.toLower();
    normalized.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    return normalized;
}

int editDistance(const QString &left, const QString &right)
{
    const int leftSize = left.size();
    const int rightSize = right.size();
    QVector<int> costs(rightSize + 1);
    for (int j = 0; j <= rightSize; ++j) {
        costs[j] = j;
    }

    for (int i = 1; i <= leftSize; ++i) {
        int previousDiagonal = costs[0];
        costs[0] = i;
        for (int j = 1; j <= rightSize; ++j) {
            const int temp = costs[j];
            const int substitution = previousDiagonal + (left.at(i - 1) == right.at(j - 1) ? 0 : 1);
            const int insertion = costs[j] + 1;
            const int deletion = costs[j - 1] + 1;
            costs[j] = std::min({substitution, insertion, deletion});
            previousDiagonal = temp;
        }
    }

    return costs[rightSize];
}

bool matchesWakeWordToken(const QString &token, const QString &wakeWord)
{
    const QString normalizedToken = normalizeWakeToken(token);
    const QString normalizedWakeWord = normalizeWakeToken(wakeWord);
    if (normalizedToken.isEmpty() || normalizedWakeWord.isEmpty()) {
        return false;
    }

    const QStringList candidates = {
        normalizedWakeWord,
        QStringLiteral("jervis"),
        QStringLiteral("jervus"),
        QStringLiteral("jarviss")
    };

    for (const QString &candidate : candidates) {
        if (normalizedToken == candidate) {
            return true;
        }

        const int distance = editDistance(normalizedToken, candidate);
        if (distance <= 2 && normalizedToken.size() >= std::max(3, static_cast<int>(candidate.size()) - 2)) {
            return true;
        }
    }

    return false;
}

bool extractWakeWordPayload(const QString &input, const QString &wakeWord, QString *payload)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty() || wakeWord.trimmed().isEmpty()) {
        if (payload) {
            *payload = trimmed;
        }
        return false;
    }

    const QStringList words = trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        if (payload) {
            *payload = {};
        }
        return false;
    }

    int index = 0;
    while (index < words.size()) {
        const QString filler = normalizeWakeToken(words.at(index));
        if (filler == QStringLiteral("hey")
            || filler == QStringLiteral("hi")
            || filler == QStringLiteral("hello")
            || filler == QStringLiteral("ok")
            || filler == QStringLiteral("okay")
            || filler == QStringLiteral("yo")) {
            ++index;
            continue;
        }
        break;
    }

    if (index >= words.size() || !matchesWakeWordToken(words.at(index), wakeWord)) {
        if (payload) {
            *payload = trimmed;
        }
        return false;
    }

    if (payload) {
        *payload = normalizeForRouting(words.mid(index + 1).join(QStringLiteral(" ")));
    }
    return true;
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

QList<AgentTask> parseBackgroundTasks(const nlohmann::json &jsonObject)
{
    QList<AgentTask> tasks;
    if (!jsonObject.contains("background_tasks") || !jsonObject.at("background_tasks").is_array()) {
        return tasks;
    }

    for (const auto &taskJson : jsonObject.at("background_tasks")) {
        if (!taskJson.is_object()) {
            continue;
        }

        AgentTask task;
        task.type = QString::fromStdString(taskJson.value("type", std::string{}));
        task.priority = taskJson.value("priority", 50);
        if (taskJson.contains("args") && taskJson.at("args").is_object()) {
            task.args = QJsonDocument::fromJson(QByteArray::fromStdString(taskJson.at("args").dump())).object();
        }
        if (!task.type.isEmpty()) {
            tasks.push_back(task);
        }
    }

    return tasks;
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
        QStringLiteral("jarvis log"),
        QStringLiteral("correct tools available"),
        QStringLiteral("what are the tools"),
        QStringLiteral("what tools"),
        QStringLiteral("reach the tools"),
        QStringLiteral("what are your tools"),
        QStringLiteral("tool list"),
        QStringLiteral("tools available"),
        QStringLiteral("what can you access"),
        QStringLiteral("latest model")
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
        QStringLiteral("correct tools available")
    });
}

bool isExplicitWebSearchQuery(const QString &input)
{
    return containsAnyNormalized(input, {
        QStringLiteral("search the web"),
        QStringLiteral("search web"),
        QStringLiteral("browse the web"),
        QStringLiteral("web search"),
        QStringLiteral("latest news"),
        QStringLiteral("latest model"),
        QStringLiteral("reach the web")
    });
}

QString extractWebSearchQuery(QString input)
{
    input = input.trimmed();
    input.remove(QRegularExpression(QStringLiteral("^(yeah|yes|okay|ok|please|jarvis)\\s*,?\\s*"),
                                    QRegularExpression::CaseInsensitiveOption));
    input.remove(QRegularExpression(QStringLiteral("^(can you|could you|would you|please)\\s+"),
                                    QRegularExpression::CaseInsensitiveOption));
    input.remove(QRegularExpression(QStringLiteral("^(search|browse)\\s+(the\\s+)?web\\s+(for|about|on)\\s+"),
                                    QRegularExpression::CaseInsensitiveOption));
    input.remove(QRegularExpression(QStringLiteral("^(search|browse)\\s+(the\\s+)?web\\s*"),
                                    QRegularExpression::CaseInsensitiveOption));
    input.remove(QRegularExpression(QStringLiteral("^(what('?s| is)\\s+the\\s+latest\\s+model)"),
                                    QRegularExpression::CaseInsensitiveOption));
    input.remove(QRegularExpression(QStringLiteral("^(latest\\s+news\\s+(in|about)\\s+)"),
                                    QRegularExpression::CaseInsensitiveOption));
    input = input.trimmed();
    input.remove(QRegularExpression(QStringLiteral("^[\\s,.:;!?-]+|[\\s,.:;!?-]+$")));
    return input.trimmed();
}

QString groundedToolInventoryText(const QList<AgentToolSpec> &tools)
{
    QStringList names;
    for (const auto &tool : tools) {
        if (!tool.name.isEmpty()) {
            names.push_back(tool.name);
        }
    }
    names.removeDuplicates();
    return QStringLiteral("I can use these tools right now: %1. File reads can access readable paths on this PC. File writes stay sandboxed to the app roots.")
        .arg(names.join(QStringLiteral(", ")));
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
    m_skillStore = new SkillStore(this);
    m_agentToolbox = new AgentToolbox(m_settings, m_memoryStore, m_skillStore, m_loggingService, this);
    m_deviceManager = new DeviceManager(this);
    m_intentEngine = new IntentEngine(m_settings, m_loggingService, this);
    m_backgroundIntentDetector = new IntentDetector(this);
    m_intentRouter = new IntentRouter(this);
    m_localResponseEngine = new LocalResponseEngine(this);
    m_taskDispatcher = new TaskDispatcher(m_loggingService, this);
    m_toolWorker = new ToolWorker(backgroundAllowedRoots(), m_loggingService);
    m_whisperSttEngine = new RuntimeSpeechRecognizer(m_voicePipelineRuntime, this);
    m_ttsEngine = new WorkerTtsEngine(m_voicePipelineRuntime, this);
    m_toolWorkerThread.setObjectName(QStringLiteral("BackgroundToolWorkerThread"));
    m_toolWorker->moveToThread(&m_toolWorkerThread);
    connect(&m_toolWorkerThread, &QThread::finished, m_toolWorker, &QObject::deleteLater);
    connect(m_taskDispatcher, &TaskDispatcher::taskReady, m_toolWorker, &ToolWorker::processTask, Qt::QueuedConnection);
    connect(m_taskDispatcher, &TaskDispatcher::taskCanceled, m_toolWorker, &ToolWorker::cancelTask, Qt::QueuedConnection);
    connect(m_taskDispatcher, &TaskDispatcher::activeTaskChanged, this, [this](const QString &type, int taskId) {
        m_activeBackgroundTaskIds.insert(type, taskId);
    });
    connect(m_toolWorker, &ToolWorker::taskStarted, m_taskDispatcher, &TaskDispatcher::handleTaskStarted, Qt::QueuedConnection);
    connect(m_toolWorker, &ToolWorker::taskFinished, m_taskDispatcher, &TaskDispatcher::handleTaskFinished, Qt::QueuedConnection);
    connect(m_taskDispatcher, &TaskDispatcher::taskResultReady, this, [this](const QJsonObject &resultObject) {
        recordTaskResult(resultObject);
    }, Qt::QueuedConnection);
    createWakeWordEngine();
}

AssistantController::~AssistantController()
{
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
    m_voicePipelineRuntime->start();
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
        m_agentCapabilities.providerMode = m_agentCapabilities.responsesApi
            ? QStringLiteral("responses_hybrid")
            : QStringLiteral("chat_hybrid");
        m_agentCapabilities.status = m_agentCapabilities.responsesApi
            ? QStringLiteral("Hybrid agent ready with responses backend")
            : QStringLiteral("Hybrid agent ready with chat fallback");
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
        setStatus(errorText);
        resumeWakeMonitor(shortWakeResumeDelayMs());
        emit idleRequested();
    });

    connect(m_streamAssembler, &StreamAssembler::partialTextUpdated, this, [this](const QString &text) {
        m_responseText = text;
        emit responseTextChanged();
    });

    connect(m_ttsEngine, &TtsEngine::playbackStarted, this, [this]() {
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("TTS playback started."));
        }
        beginTtsExclusiveMode();
        emit speakingRequested();
    });
    connect(m_ttsEngine, &TtsEngine::playbackFinished, this, [this]() {
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("TTS playback finished."));
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
        resumeWakeMonitor(postSpeechWakeResumeDelayMs());
        emit idleRequested();
    });
    connect(m_ttsEngine, &TtsEngine::playbackFailed, this, [this](const QString &errorText) {
        enterPostSpeechCooldown();
        setStatus(errorText);
        endConversationSession();
        resumeWakeMonitor(shortWakeResumeDelayMs());
        emit idleRequested();
    });

    connect(m_aiBackendClient, &AiBackendClient::requestStarted, this, [this](quint64 requestId) {
        m_activeRequestId = requestId;
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
        m_agentCapabilities.providerMode = capabilities.responsesApi
            ? QStringLiteral("responses_hybrid")
            : QStringLiteral("chat_hybrid");
        m_agentCapabilities.status = capabilities.responsesApi
            ? QStringLiteral("Hybrid agent ready with responses backend")
            : QStringLiteral("Hybrid agent ready with chat fallback");
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
            const QString errorGroup = errorText.contains(QStringLiteral("timed out"), Qt::CaseInsensitive)
                ? QStringLiteral("error_timeout")
                : QStringLiteral("ai_offline");
            refreshConversationSession();
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
    m_aiBackendClient->setEndpoint(m_settings->chatBackendEndpoint());
    m_modelCatalogService->refresh();
}

void AssistantController::submitText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    m_lastPromptForAiLog = trimmed;
    invalidateWakeMonitorResume();

    const QString wakeWord = m_settings->wakeWordPhrase().trimmed().isEmpty()
        ? QStringLiteral("Jarvis")
        : m_settings->wakeWordPhrase().trimmed();
    QString routedInput = trimmed;
    const bool wakeDetected = extractWakeWordPayload(trimmed, wakeWord, &routedInput);
    const QString effectiveInput = routedInput.isEmpty() ? trimmed : routedInput;

    if (wakeDetected) {
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
        m_loggingService->info(QStringLiteral("submitText received. raw=\"%1\" wakeDetected=%2 routed=\"%3\"")
            .arg(trimmed.left(240))
            .arg(wakeDetected ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(routedInput.left(240)));
    }
    updateUserProfileFromInput(effectiveInput);
    m_memoryStore->appendConversation(QStringLiteral("user"), trimmed);

    if (wakeDetected && routedInput.isEmpty()) {
        m_followUpListeningAfterWakeAck = true;
        deliverLocalResponse(
            m_localResponseEngine->wakeWordReady(buildLocalResponseContext()),
            QStringLiteral("Wake phrase detected"),
            true);
        return;
    }

    if (shouldEndConversationSession(effectiveInput)) {
        endConversationSession();
        deliverLocalResponse(QStringLiteral("All right. See You Soon Sir."), QStringLiteral("Conversation ended"), true);
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

    if (intent == LocalIntent::Greeting || intent == LocalIntent::SmallTalk) {
        deliverLocalResponse(
            m_localResponseEngine->respondToIntent(intent, buildLocalResponseContext()),
            QStringLiteral("Local response"),
            true);
        return;
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
            groundedToolInventoryText(m_agentToolbox->builtInTools()),
            QStringLiteral("Tool inventory"),
            true);
        return;
    }

    if (isExplicitWebSearchQuery(routedInput)) {
        const QString extractedQuery = extractWebSearchQuery(routedInput);
        if (extractedQuery.isEmpty() || extractedQuery.compare(routedInput, Qt::CaseInsensitive) == 0) {
            deliverLocalResponse(
                QStringLiteral("Yes. Tell me what you want me to search for, and I'll show the result in the panel."),
                QStringLiteral("Web search ready"),
                true);
            return;
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

    if (m_settings->agentEnabled() && isExplicitAgentWorldQuery(routedInput)) {
        startAgentConversationRequest(routedInput, expectedAgentIntentForQuery(routedInput));
        return;
    }

    if (intent == LocalIntent::Command || m_reasoningRouter->isLikelyCommand(routedInput)) {
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
    pauseWakeMonitor();
    startAudioCapture(AudioCaptureMode::Direct, true);
}

void AssistantController::startWakeMonitor()
{
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
            static_cast<float>(m_settings->preciseTriggerThreshold()),
            m_settings->preciseTriggerCooldownMs(),
            m_settings->selectedAudioInputDeviceId())) {
        m_wakeEngineReady = false;
        if (m_loggingService) {
            m_loggingService->warn(QStringLiteral("Wake monitor could not start."));
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
        m_loggingService->info(QStringLiteral("Wake monitor stopped."));
    }
    updateStartupState();
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

void AssistantController::setSelectedModel(const QString &modelId)
{
    m_settings->setChatBackendModel(modelId);
    m_settings->save();
    m_agentCapabilities.selectedModelToolCapable = modelId.toLower().contains(QStringLiteral("qwen"))
        || modelId.toLower().contains(QStringLiteral("granite"))
        || modelId.toLower().contains(QStringLiteral("llama"))
        || modelId.toLower().contains(QStringLiteral("gpt-oss"))
        || modelId.toLower().contains(QStringLiteral("tool"));
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
    m_settings->setTracePanelEnabled(tracePanelEnabled);
    m_settings->save();
    emit agentStateChanged();
}

void AssistantController::saveSettings(
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
    const QString &preciseEnginePath,
    const QString &preciseModelPath,
    double preciseThreshold,
    int preciseCooldownMs,
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
    m_settings->setPreciseEngineExecutable(preciseEnginePath);
    m_settings->setPreciseModelPath(preciseModelPath);
    m_settings->setPreciseTriggerThreshold(preciseThreshold);
    m_settings->setPreciseTriggerCooldownMs(preciseCooldownMs);
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

    if (m_settings->wakeEngineKind() == QStringLiteral("sherpa-onnx")) {
        m_wakeWordEngine = new SherpaWakeWordEngine(m_settings, m_loggingService, this);
    } else {
        m_wakeWordEngine = new WakeWordEnginePrecise(m_loggingService, this);
    }
}

void AssistantController::bindWakeWordEngineSignals()
{
    connect(m_wakeWordEngine, &WakeWordEngine::engineReady, this, [this]() {
        m_wakeEngineReady = true;
        m_lastWakeError.clear();
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

        if (m_currentState == AssistantState::Speaking || m_currentState == AssistantState::Processing || m_ttsEngine->isSpeaking()) {
            if (m_loggingService) {
                m_loggingService->info(QStringLiteral("Wake trigger ignored while assistant is busy."));
            }
            return;
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
        activateConversationSession();
        m_followUpListeningAfterWakeAck = true;
        m_lastPromptForAiLog = m_settings->wakeWordPhrase();
        deliverLocalResponse(
            m_localResponseEngine->wakeWordReady(buildLocalResponseContext()),
            QStringLiteral("Wake word detected"),
            true);
    });
    connect(m_wakeWordEngine, &WakeWordEngine::errorOccurred, this, [this](const QString &message) {
        m_wakeEngineReady = false;
        m_lastWakeError = message;
        updateStartupState();
        if (m_loggingService) {
            m_loggingService->error(QStringLiteral("%1 wake engine error: %2").arg(wakeEngineDisplayName(), message));
        }
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

    setBlocked(false);
    return {};
}

void AssistantController::setStatus(const QString &status)
{
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
    return std::max(350, m_settings->preciseTriggerCooldownMs() / 2);
}

int AssistantController::postSpeechWakeResumeDelayMs() const
{
    return std::max(1200, m_settings->preciseTriggerCooldownMs());
}

int AssistantController::followUpListeningDelayMs() const
{
    return 200;
}

int AssistantController::conversationSessionTimeoutMs() const
{
    return 45000;
}

int AssistantController::conversationSessionRestartDelayMs() const
{
    return 140;
}

int AssistantController::maxConversationSessionMisses() const
{
    return 2;
}

QString AssistantController::buildSttPrompt() const
{
    const QString wakeWord = m_settings->wakeWordPhrase().trimmed().isEmpty()
        ? QStringLiteral("Jarvis")
        : m_settings->wakeWordPhrase().trimmed();
    return QStringLiteral(
        "%1. What time is it? What is the date today? What is your name? How are you? "
        "Turn on the light. Turn off the light. Open settings. Close overlay.")
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
    if (m_consecutiveSessionMisses >= maxConversationSessionMisses()) {
        endConversationSession();
        setStatus(QStringLiteral("Standing by"));
        resumeWakeMonitor(shortWakeResumeDelayMs());
        emit idleRequested();
        return;
    }

    refreshConversationSession();
    setStatus(QStringLiteral("Listening"));
    setDuplexState(DuplexState::Open);
    if (!scheduleConversationSessionListening(conversationSessionRestartDelayMs())) {
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
    return m_wakeMonitorEnabled
        && m_currentState != AssistantState::Listening
        && !isMicrophoneBlocked()
        && m_audioCaptureMode == AudioCaptureMode::None
        && !m_ttsEngine->isSpeaking()
        && !resolveWakeEngineRuntimePath().isEmpty()
        && !resolveWakeEngineModelPath().isEmpty();
}

QString AssistantController::resolveWakeEngineRuntimePath() const
{
    if (m_settings->wakeEngineKind() == QStringLiteral("sherpa-onnx")) {
        return firstExistingPath({
            QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/third_party/sherpa-onnx/sherpa-onnx-v1.12.33-win-x64-shared-MD-Release-no-tts"),
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + QStringLiteral("/third_party/sherpa-onnx/sherpa-onnx-v1.12.33-win-x64-shared-MD-Release-no-tts"),
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + QStringLiteral("/third_party/sherpa-onnx")
        });
    }

    return m_settings->preciseEngineExecutable();
}

QString AssistantController::resolveWakeEngineModelPath() const
{
    if (m_settings->wakeEngineKind() == QStringLiteral("sherpa-onnx")) {
        return firstExistingPath({
            QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/third_party/sherpa-kws-model/sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01"),
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + QStringLiteral("/third_party/sherpa-kws-model/sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01"),
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + QStringLiteral("/third_party/models/sherpa-kws/sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01")
        });
    }

    return m_settings->preciseModelPath();
}

QString AssistantController::wakeEngineDisplayName() const
{
    return m_settings->wakeEngineKind() == QStringLiteral("sherpa-onnx")
        ? QStringLiteral("sherpa-onnx")
        : QStringLiteral("Precise");
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
    m_responseText = text;
    emit responseTextChanged();
    m_memoryStore->appendConversation(QStringLiteral("assistant"), text);
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Local response delivered. status=\"%1\" text=\"%2\"")
            .arg(status, text.left(240)));
    }
    logPromptResponsePair(text, QStringLiteral("local"), status);
    setStatus(status);
    if (speak) {
        refreshConversationSession();
        m_ttsEngine->speakText(text);
    } else {
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

void AssistantController::startConversationRequest(const QString &input)
{
    const ReasoningMode mode = m_reasoningRouter->chooseMode(input, m_settings->autoRoutingEnabled(), m_settings->defaultReasoningMode());
    const QString modelId = m_settings->chatBackendModel().isEmpty() && !availableModelIds().isEmpty() ? availableModelIds().first() : m_settings->chatBackendModel();
    if (modelId.isEmpty()) {
        setStatus(QStringLiteral("No local AI backend model selected"));
        emit idleRequested();
        return;
    }

    m_activeRequestKind = RequestKind::Conversation;
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Starting conversation request. model=\"%1\" input=\"%2\"")
            .arg(modelId, input.left(240)));
    }
    const auto messages = m_promptAdapter->buildConversationMessages(
        input,
        m_memoryStore->recentMessages(8),
        m_memoryStore->relevantMemory(input),
        m_identityProfileService->identity(),
        m_identityProfileService->userProfile(),
        mode);

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
    const ReasoningMode mode = m_reasoningRouter->chooseMode(input, m_settings->autoRoutingEnabled(), m_settings->defaultReasoningMode());
    const QString modelId = m_settings->chatBackendModel().isEmpty() && !availableModelIds().isEmpty() ? availableModelIds().first() : m_settings->chatBackendModel();
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
    appendAgentTrace(QStringLiteral("session"), QStringLiteral("Agent request"), QStringLiteral("Starting hybrid agent conversation"), true);

    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Starting agent request. model=\"%1\" input=\"%2\"")
            .arg(modelId, input.left(240)));
    }

    const QList<AgentToolSpec> relevantTools = m_promptAdapter->getRelevantTools(
        expectedIntent,
        m_agentToolbox->builtInTools());
    const auto messages = m_promptAdapter->buildHybridAgentMessages(
        input,
        m_memoryStore->relevantMemory(input),
        m_identityProfileService->identity(),
        m_identityProfileService->userProfile(),
        QDir::currentPath(),
        expectedIntent,
        relevantTools,
        mode);

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
        m_lastAgentIntent,
        m_agentToolbox->builtInTools());
    if (relevantTools.isEmpty()) {
        relevantTools = m_agentToolbox->builtInTools();
    }
    const AgentRequest request{
        .model = selectedModel(),
        .instructions = m_promptAdapter->buildAgentInstructions(
            m_memoryStore->relevantMemory(m_lastAgentInput),
            m_skillStore->listSkills(),
            relevantTools,
            m_identityProfileService->identity(),
            m_identityProfileService->userProfile(),
            QDir::currentPath(),
            m_lastAgentIntent,
            m_settings->memoryAutoWrite()),
        .inputText = {},
        .previousResponseId = m_previousAgentResponseId,
        .tools = relevantTools,
        .toolResults = results,
        .sampling = samplingProfile(),
        .mode = m_settings->defaultReasoningMode(),
        .timeout = std::chrono::milliseconds(effectiveRequestTimeoutMs(m_settings))
    };
    m_activeRequestId = m_aiBackendClient->sendAgentRequest(request);
}

void AssistantController::startCommandRequest(const QString &input)
{
    const QString modelId = m_settings->chatBackendModel().isEmpty() && !availableModelIds().isEmpty() ? availableModelIds().first() : m_settings->chatBackendModel();
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
    m_responseText = reply.displayText;
    emit responseTextChanged();
    m_memoryStore->appendConversation(QStringLiteral("assistant"), reply.displayText);
    m_streamAssembler->drainRemainingText();
    if (reply.shouldSpeak && !reply.spokenText.isEmpty()) {
        refreshConversationSession();
        m_ttsEngine->speakText(reply.spokenText);
    } else if (!m_ttsEngine->isSpeaking()) {
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
    logPromptResponsePair(reply.displayText, QStringLiteral("conversation"), QStringLiteral("Response ready"));
    setStatus(QStringLiteral("Response ready"));
}

void AssistantController::handleHybridAgentFinished(const QString &payload)
{
    appendAgentTrace(QStringLiteral("model"), QStringLiteral("Hybrid agent response"), QStringLiteral("Received hybrid payload"), true);

    const QString jsonPayload = extractJsonObjectPayload(payload);
    const auto json = nlohmann::json::parse(jsonPayload.toStdString(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        appendAgentTrace(QStringLiteral("validation"), QStringLiteral("Hybrid payload rejected"), QStringLiteral("The model returned invalid JSON."), false);
        handleConversationFinished(payload);
        return;
    }

    const IntentType returnedIntent = intentTypeFromString(QString::fromStdString(json.value("intent", std::string{})));
    const QString message = QString::fromStdString(json.value("message", std::string{})).trimmed();
    const QList<AgentToolSpec> relevantTools = m_promptAdapter->getRelevantTools(
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
    for (const AgentTask &task : parseBackgroundTasks(json)) {
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
    m_responseText = reply.displayText;
    emit responseTextChanged();
    m_memoryStore->appendConversation(QStringLiteral("assistant"), reply.displayText);

    if (reply.shouldSpeak && !reply.spokenText.isEmpty()) {
        refreshConversationSession();
        m_ttsEngine->speakText(reply.spokenText);
    } else if (!m_ttsEngine->isSpeaking()) {
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
    setStatus(QStringLiteral("Response ready"));
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
        m_memoryStore->extractUserFacts(m_lastAgentInput);
    }

    const SpokenReply reply = parseSpokenReply(response.outputText);
    m_responseText = reply.displayText;
    emit responseTextChanged();
    m_memoryStore->appendConversation(QStringLiteral("assistant"), reply.displayText);
    if (reply.shouldSpeak && !reply.spokenText.isEmpty()) {
        refreshConversationSession();
        m_ttsEngine->speakText(reply.spokenText);
    } else if (!m_ttsEngine->isSpeaking()) {
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
    setStatus(QStringLiteral("Response ready"));
}

void AssistantController::handleCommandFinished(const QString &text)
{
    const CommandEnvelope command = parseCommand(text);
    if (!command.valid || command.confidence < 0.6f) {
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
    m_responseText = message;
    emit responseTextChanged();
    m_memoryStore->appendConversation(QStringLiteral("assistant"), message);
    refreshConversationSession();
    m_ttsEngine->speakText(message);
    logPromptResponsePair(message, QStringLiteral("command"), QStringLiteral("Command executed"));
    setStatus(QStringLiteral("Command executed"));
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
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("[TaskDispatcher] created %1 #%2").arg(task.type).arg(task.id));
        }
        m_taskDispatcher->enqueue(task);
    }
}

void AssistantController::recordTaskResult(const QJsonObject &resultObject)
{
    BackgroundTaskResult result;
    result.taskId = resultObject.value(QStringLiteral("taskId")).toInt();
    result.type = resultObject.value(QStringLiteral("type")).toString();
    result.success = resultObject.value(QStringLiteral("success")).toBool();
    result.state = static_cast<TaskState>(resultObject.value(QStringLiteral("state")).toInt(static_cast<int>(TaskState::Finished)));
    result.title = resultObject.value(QStringLiteral("title")).toString();
    result.summary = resultObject.value(QStringLiteral("summary")).toString();
    result.detail = resultObject.value(QStringLiteral("detail")).toString();
    result.payload = resultObject.value(QStringLiteral("payload")).toObject();
    result.finishedAt = resultObject.value(QStringLiteral("finishedAt")).toString();
    result.taskKey = resultObject.value(QStringLiteral("taskKey")).toString();

    const int activeTaskId = m_activeBackgroundTaskIds.value(result.type, -1);
    if (activeTaskId != result.taskId) {
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("[UI] ignored stale task id=%1 type=%2 active=%3")
                .arg(result.taskId)
                .arg(result.type)
                .arg(activeTaskId));
        }
        return;
    }

    if (result.state == TaskState::Canceled) {
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
