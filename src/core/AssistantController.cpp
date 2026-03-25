#include "core/AssistantController.h"

#include <algorithm>

#include <QDateTime>
#include <QFileInfo>
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
#include "audio/AudioInputService.h"
#include "core/IntentRouter.h"
#include "core/LocalResponseEngine.h"
#include "devices/DeviceManager.h"
#include "logging/LoggingService.h"
#include "memory/MemoryStore.h"
#include "settings/AppSettings.h"
#include "settings/IdentityProfileService.h"
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

QString firstExistingPath(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    return {};
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
    m_deviceManager = new DeviceManager(this);
    m_intentRouter = new IntentRouter(this);
    m_localResponseEngine = new LocalResponseEngine(this);
    m_audioInputService = new AudioInputService(this);
    m_whisperSttEngine = new RuntimeSpeechRecognizer(m_voicePipelineRuntime, this);
    m_ttsEngine = new WorkerTtsEngine(m_voicePipelineRuntime, this);
    createWakeWordEngine();
}

void AssistantController::initialize()
{
    m_voicePipelineRuntime->start();
    m_aiBackendClient->setEndpoint(m_settings->chatBackendEndpoint());
    m_deviceManager->registerDefaults();
    m_localResponseEngine->initialize();
    setupStateMachine();
    refreshModels();

    connect(m_modelCatalogService, &ModelCatalogService::modelsChanged, this, &AssistantController::modelsChanged);
    connect(m_modelCatalogService, &ModelCatalogService::availabilityChanged, this, [this]() {
        setStatus(m_modelCatalogService->availability().status);
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
                    : QStringLiteral("none");
            m_loggingService->info(QStringLiteral("Audio speech detected. mode=%1").arg(mode));
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
                    : QStringLiteral("none");
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
            setStatus(QStringLiteral("No speech detected"));
            resumeWakeMonitor(shortWakeResumeDelayMs());
            emit idleRequested();
            return;
        }

        emit processingRequested();
        m_activeSttRequestId = m_whisperSttEngine->transcribePcm(pcmData, buildSttPrompt(), true);
    });
    connect(m_voicePipelineRuntime, &VoicePipelineRuntime::inputCaptureFailed, this, [this](quint64 generationId, const QString &errorText) {
        if (generationId != m_activeInputCaptureId) {
            return;
        }

        m_audioCaptureMode = AudioCaptureMode::None;
        if (m_loggingService) {
            m_loggingService->error(QStringLiteral("Input capture failed: %1").arg(errorText));
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
            setStatus(QStringLiteral("No speech detected"));
            resumeWakeMonitor(shortWakeResumeDelayMs());
            emit idleRequested();
            return;
        }
        if (shouldIgnoreAmbiguousTranscript(transcript)) {
            if (m_loggingService) {
                m_loggingService->info(QStringLiteral("Ignoring ambiguous transcription. text=\"%1\"").arg(transcript.left(120)));
            }
            deliverLocalResponse(QStringLiteral("I didn't catch that."), QStringLiteral("Please repeat"), true);
            return;
        }

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
        if (m_followUpListeningAfterWakeAck) {
            QTimer::singleShot(followUpListeningDelayMs(), this, [this]() {
                m_followUpListeningAfterWakeAck = false;
                if (!m_ttsEngine->isSpeaking() && !startAudioCapture(AudioCaptureMode::Direct, true)) {
                    resumeWakeMonitor(shortWakeResumeDelayMs());
                    emit idleRequested();
                }
            });
            return;
        }
        resumeWakeMonitor(postSpeechWakeResumeDelayMs());
        emit idleRequested();
    });
    connect(m_ttsEngine, &TtsEngine::playbackFailed, this, [this](const QString &errorText) {
        enterPostSpeechCooldown();
        setStatus(errorText);
        resumeWakeMonitor(shortWakeResumeDelayMs());
        emit idleRequested();
    });

    connect(m_aiBackendClient, &AiBackendClient::requestStarted, this, [this](quint64 requestId) {
        m_activeRequestId = requestId;
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("Local AI backend request started. requestId=%1 kind=%2")
                .arg(requestId)
                .arg(m_activeRequestKind == RequestKind::CommandExtraction ? QStringLiteral("command") : QStringLiteral("conversation")));
        }
        setDuplexState(DuplexState::Processing);
        emit processingRequested();
    });
    connect(m_aiBackendClient, &AiBackendClient::requestDelta, this, [this](quint64 requestId, const QString &delta) {
        if (requestId == m_activeRequestId && m_activeRequestKind == RequestKind::Conversation) {
            m_streamAssembler->appendChunk(delta);
        }
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
        } else {
            handleConversationFinished(fullText);
        }
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
            deliverLocalResponse(
                m_localResponseEngine->respondToError(errorGroup, buildLocalResponseContext()),
                errorText,
                true);
        }
    });

    if (m_settings->initialSetupCompleted()) {
        startWakeMonitor();
    }
}

QString AssistantController::stateName() const { return stateToString(m_currentState); }
QString AssistantController::transcript() const { return m_transcript; }
QString AssistantController::responseText() const { return m_responseText; }
QString AssistantController::statusText() const { return m_statusText; }
float AssistantController::audioLevel() const { return m_audioLevel; }
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

void AssistantController::refreshModels()
{
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
    updateUserProfileFromInput(routedInput.isEmpty() ? trimmed : routedInput);
    m_memoryStore->appendConversation(QStringLiteral("user"), trimmed);

    if (wakeDetected && routedInput.isEmpty()) {
        m_followUpListeningAfterWakeAck = true;
        deliverLocalResponse(
            m_localResponseEngine->wakeWordReady(buildLocalResponseContext()),
            QStringLiteral("Wake phrase detected"),
            true);
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

    if (intent == LocalIntent::Command || m_reasoningRouter->isLikelyCommand(routedInput)) {
        startCommandRequest(routedInput);
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
    if (m_wakeWordEngine->isActive()) {
        if (m_wakeWordEngine->isPaused() && canStartWakeMonitor()) {
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
        if (m_loggingService) {
            m_loggingService->warn(QStringLiteral("Wake monitor could not start."));
        }
    }
}

void AssistantController::stopWakeMonitor()
{
    m_wakeMonitorEnabled = false;
    if (m_wakeWordEngine->isActive()) {
        m_wakeWordEngine->stop();
    }
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Wake monitor stopped."));
    }
}

void AssistantController::stopListening()
{
    if (isMicrophoneBlocked()) {
        clearActiveSpeechCapture();
        return;
    }
    invalidateWakeMonitorResume();
    const QByteArray pcm = m_audioInputService->recordedPcm();
    const AudioCaptureMode completedMode = m_audioCaptureMode;
    m_lastCompletedCaptureMode = completedMode;
    m_audioInputService->stop();
    m_audioCaptureMode = AudioCaptureMode::None;
    if (m_loggingService) {
        const QString mode = completedMode == AudioCaptureMode::Direct
                ? QStringLiteral("direct")
                : QStringLiteral("none");
        m_loggingService->info(QStringLiteral("Audio capture stopped. mode=%1 bytes=%2").arg(mode).arg(pcm.size()));
    }
    emit processingRequested();
    if (pcm.isEmpty()) {
        setStatus(QStringLiteral("No audio captured"));
        resumeWakeMonitor(shortWakeResumeDelayMs());
        emit idleRequested();
        return;
    }
    m_activeSttRequestId = m_whisperSttEngine->transcribePcm(pcm, buildSttPrompt(), true);
}

void AssistantController::cancelActiveRequest()
{
    invalidateWakeMonitorResume();
    invalidateActiveTranscription();
    m_aiBackendClient->cancelActiveRequest();
    m_ttsEngine->clear();
    setStatus(QStringLiteral("Request cancelled"));
    resumeWakeMonitor(shortWakeResumeDelayMs());
    emit idleRequested();
}

void AssistantController::setSelectedModel(const QString &modelId)
{
    m_settings->setChatBackendModel(modelId);
    m_settings->save();
    emit modelsChanged();
    refreshModels();
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
        m_followUpListeningAfterWakeAck = true;
        m_lastPromptForAiLog = m_settings->wakeWordPhrase();
        deliverLocalResponse(
            m_localResponseEngine->wakeWordReady(buildLocalResponseContext()),
            QStringLiteral("Wake word detected"),
            true);
    });
    connect(m_wakeWordEngine, &WakeWordEngine::errorOccurred, this, [this](const QString &message) {
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
    if (m_audioInputService->isActive()) {
        m_audioInputService->stop();
    }
    m_audioInputService->clearRecordedAudio();
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

void AssistantController::pauseWakeMonitor()
{
    invalidateWakeMonitorResume();
    if (!m_wakeMonitorEnabled || !m_wakeWordEngine->isActive()) {
        return;
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
        if (!m_wakeMonitorEnabled || !canStartWakeMonitor()) {
            return;
        }

        setDuplexState(DuplexState::WakeOnly);
        if (m_wakeWordEngine->isActive()) {
            if (m_wakeWordEngine->isPaused()) {
                m_wakeWordEngine->resume();
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
        && !m_audioInputService->isActive()
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
    if (m_audioInputService->isActive()) {
        return false;
    }

    if (m_audioInputService->start(m_settings->micSensitivity(), m_settings->selectedAudioInputDeviceId())) {
        m_audioCaptureMode = mode;
        if (m_loggingService) {
            const QString modeText = QStringLiteral("direct");
            m_loggingService->info(QStringLiteral("Audio capture started. mode=%1 device=\"%2\" sensitivity=%3")
                .arg(modeText, m_settings->selectedAudioInputDeviceId())
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

    if (announceListening) {
        setStatus(QStringLiteral("No microphone available"));
    }
    if (m_loggingService) {
        m_loggingService->error(QStringLiteral("Failed to start audio capture."));
    }
    return false;
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
    const QString displayName = profile.displayName.isEmpty() ? profile.userName : profile.displayName;
    const QString spokenName = profile.spokenName.isEmpty() ? displayName : profile.spokenName;

    return {
        .assistantName = m_identityProfileService->identity().assistantName,
        .userName = spokenName.isEmpty()
            ? (displayName.isEmpty() ? m_memoryStore->userName() : displayName)
            : spokenName,
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
        m_ttsEngine->speakText(text);
    } else {
        setDuplexState(DuplexState::Open);
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
        .timeout = std::chrono::milliseconds(m_settings->requestTimeoutMs())
    });
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
        {.mode = ReasoningMode::Fast, .kind = RequestKind::CommandExtraction, .stream = false, .timeout = std::chrono::milliseconds(m_settings->requestTimeoutMs())});
}

void AssistantController::handleConversationFinished(const QString &text)
{
    const SpokenReply reply = parseSpokenReply(text);
    m_responseText = reply.displayText;
    emit responseTextChanged();
    m_memoryStore->appendConversation(QStringLiteral("assistant"), reply.displayText);
    m_streamAssembler->drainRemainingText();
    if (reply.shouldSpeak && !reply.spokenText.isEmpty()) {
        m_ttsEngine->speakText(reply.spokenText);
    } else if (!m_ttsEngine->isSpeaking()) {
        setDuplexState(DuplexState::Open);
        scheduleWakeMonitorRestart();
        emit idleRequested();
    }
    logPromptResponsePair(reply.displayText, QStringLiteral("conversation"), QStringLiteral("Response ready"));
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
    m_ttsEngine->speakText(message);
    logPromptResponsePair(message, QStringLiteral("command"), QStringLiteral("Command executed"));
    setStatus(QStringLiteral("Command executed"));
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
