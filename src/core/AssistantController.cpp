#include "core/AssistantController.h"

#include <algorithm>

#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>
#include <QTimer>
#include <QTime>
#include <QVector>

#include <nlohmann/json.hpp>

#include "ai/LmStudioClient.h"
#include "ai/ModelCatalogService.h"
#include "ai/PromptAdapter.h"
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
#include "stt/WhisperSttEngine.h"
#include "tts/PiperTtsEngine.h"

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
    m_lmStudioClient = new LmStudioClient(this);
    m_modelCatalogService = new ModelCatalogService(m_settings, m_lmStudioClient, this);
    m_reasoningRouter = new ReasoningRouter(this);
    m_promptAdapter = new PromptAdapter(this);
    m_streamAssembler = new StreamAssembler(this);
    m_memoryStore = new MemoryStore(this);
    m_deviceManager = new DeviceManager(this);
    m_intentRouter = new IntentRouter(this);
    m_localResponseEngine = new LocalResponseEngine(this);
    m_audioInputService = new AudioInputService(this);
    m_whisperSttEngine = new WhisperSttEngine(m_settings, m_loggingService, this);
    m_piperTtsEngine = new PiperTtsEngine(m_settings, this);
}

void AssistantController::initialize()
{
    m_lmStudioClient->setEndpoint(m_settings->lmStudioEndpoint());
    m_deviceManager->registerDefaults();
    m_localResponseEngine->initialize();
    setupStateMachine();
    refreshModels();

    connect(m_modelCatalogService, &ModelCatalogService::modelsChanged, this, &AssistantController::modelsChanged);
    connect(m_modelCatalogService, &ModelCatalogService::availabilityChanged, this, [this]() {
        setStatus(m_modelCatalogService->availability().status);
    });

    connect(m_audioInputService, &AudioInputService::audioLevelChanged, this, [this](const AudioLevel &level) {
        m_audioLevel = level.rms;
        emit audioLevelChanged();
    });
    connect(m_audioInputService, &AudioInputService::speechDetected, this, [this]() {
        if (m_loggingService) {
            const QString mode = m_audioCaptureMode == AudioCaptureMode::WakeMonitor
                ? QStringLiteral("wake-monitor")
                : m_audioCaptureMode == AudioCaptureMode::Direct
                    ? QStringLiteral("direct")
                    : QStringLiteral("none");
            m_loggingService->info(QStringLiteral("Audio speech detected. mode=%1").arg(mode));
        }
    });
    connect(m_audioInputService, &AudioInputService::captureWindowElapsed, this, [this](bool hadSpeech) {
        if (m_loggingService) {
            const QString mode = m_audioCaptureMode == AudioCaptureMode::WakeMonitor
                ? QStringLiteral("wake-monitor")
                : m_audioCaptureMode == AudioCaptureMode::Direct
                    ? QStringLiteral("direct")
                    : QStringLiteral("none");
            m_loggingService->info(QStringLiteral("Audio capture window elapsed. mode=%1 hadSpeech=%2")
                .arg(mode)
                .arg(hadSpeech ? QStringLiteral("true") : QStringLiteral("false")));
        }

        if (!hadSpeech) {
            m_audioInputService->stop();
            m_audioCaptureMode = AudioCaptureMode::None;
            m_lastCompletedCaptureMode = AudioCaptureMode::None;
            if (m_wakeMonitorEnabled) {
                scheduleWakeMonitorRestart(50);
            } else {
                setStatus(QStringLiteral("No speech detected"));
                emit idleRequested();
            }
            return;
        }

        stopListening();
    });
    connect(m_audioInputService, &AudioInputService::speechEnded, this, &AssistantController::stopListening);

    connect(m_whisperSttEngine, &WhisperSttEngine::transcriptionReady, this, [this](const TranscriptionResult &result) {
        m_transcript = result.text;
        emit transcriptChanged();
        if (result.text.isEmpty()) {
            if (m_lastCompletedCaptureMode == AudioCaptureMode::WakeMonitor) {
                scheduleWakeMonitorRestart();
                return;
            }
            setStatus(QStringLiteral("No speech detected"));
            emit idleRequested();
            return;
        }

        if (m_loggingService) {
            const QString mode = m_lastCompletedCaptureMode == AudioCaptureMode::WakeMonitor
                ? QStringLiteral("wake-monitor")
                : QStringLiteral("direct");
            m_loggingService->info(QStringLiteral("Transcription ready. mode=%1 text=\"%2\"")
                .arg(mode, result.text.left(240)));
        }

        if (m_lastCompletedCaptureMode == AudioCaptureMode::WakeMonitor) {
            QString payload;
            if (!extractWakeWordPayload(result.text, m_settings->wakeWordPhrase(), &payload)) {
                if (m_loggingService) {
                    m_loggingService->info(QStringLiteral("Wake monitor ignored non-wake speech."));
                }
                scheduleWakeMonitorRestart();
                return;
            }
        }
        submitText(result.text);
    });
    connect(m_whisperSttEngine, &WhisperSttEngine::transcriptionFailed, this, [this](const QString &errorText) {
        if (m_loggingService) {
            m_loggingService->error(QStringLiteral("Speech transcription failed: %1").arg(errorText));
        }
        setStatus(errorText);
        scheduleWakeMonitorRestart();
        emit idleRequested();
    });

    connect(m_streamAssembler, &StreamAssembler::partialTextUpdated, this, [this](const QString &text) {
        m_responseText = text;
        emit responseTextChanged();
    });
    connect(m_streamAssembler, &StreamAssembler::sentenceReady, m_piperTtsEngine, &PiperTtsEngine::enqueueSentence);

    connect(m_piperTtsEngine, &PiperTtsEngine::playbackStarted, this, [this]() {
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("TTS playback started."));
        }
        emit speakingRequested();
    });
    connect(m_piperTtsEngine, &PiperTtsEngine::playbackFinished, this, [this]() {
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("TTS playback finished."));
        }
        if (m_followUpListeningAfterWakeAck) {
            m_followUpListeningAfterWakeAck = false;
            startAudioCapture(AudioCaptureMode::Direct, true);
            return;
        }
        scheduleWakeMonitorRestart();
        emit idleRequested();
    });
    connect(m_piperTtsEngine, &PiperTtsEngine::playbackFailed, this, [this](const QString &errorText) {
        setStatus(errorText);
        scheduleWakeMonitorRestart();
        emit idleRequested();
    });

    connect(m_lmStudioClient, &LmStudioClient::requestStarted, this, [this](quint64 requestId) {
        m_activeRequestId = requestId;
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("LM Studio request started. requestId=%1 kind=%2")
                .arg(requestId)
                .arg(m_activeRequestKind == RequestKind::CommandExtraction ? QStringLiteral("command") : QStringLiteral("conversation")));
        }
        emit processingRequested();
    });
    connect(m_lmStudioClient, &LmStudioClient::requestDelta, this, [this](quint64 requestId, const QString &delta) {
        if (requestId == m_activeRequestId && m_activeRequestKind == RequestKind::Conversation) {
            m_streamAssembler->appendChunk(delta);
        }
    });
    connect(m_lmStudioClient, &LmStudioClient::requestFinished, this, [this](quint64 requestId, const QString &fullText) {
        if (requestId != m_activeRequestId) {
            return;
        }

        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("LM Studio request finished. requestId=%1 chars=%2")
                .arg(requestId)
                .arg(fullText.size()));
        }

        if (m_activeRequestKind == RequestKind::CommandExtraction) {
            handleCommandFinished(fullText);
        } else {
            handleConversationFinished(fullText);
        }
    });
    connect(m_lmStudioClient, &LmStudioClient::requestFailed, this, [this](quint64 requestId, const QString &errorText) {
        if (requestId == m_activeRequestId) {
            if (m_loggingService) {
                m_loggingService->error(QStringLiteral("LM Studio request failed. requestId=%1 error=\"%2\"")
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
        scheduleWakeMonitorRestart(1500);
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
QString AssistantController::selectedModel() const { return m_settings->selectedModel(); }

void AssistantController::refreshModels()
{
    m_lmStudioClient->setEndpoint(m_settings->lmStudioEndpoint());
    m_modelCatalogService->refresh();
}

void AssistantController::submitText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    const QString wakeWord = m_settings->wakeWordPhrase().trimmed().isEmpty()
        ? QStringLiteral("Jarvis")
        : m_settings->wakeWordPhrase().trimmed();
    QString routedInput = trimmed;
    const bool wakeDetected = extractWakeWordPayload(trimmed, wakeWord, &routedInput);

    m_transcript = trimmed;
    m_responseText.clear();
    m_streamAssembler->reset();
    m_piperTtsEngine->clear();
    emit transcriptChanged();
    emit responseTextChanged();
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
    stopWakeMonitor();
    startAudioCapture(AudioCaptureMode::Direct, true);
}

void AssistantController::startWakeMonitor()
{
    m_wakeMonitorEnabled = true;
    if (!canStartWakeMonitor()) {
        return;
    }

    startAudioCapture(AudioCaptureMode::WakeMonitor, false);
}

void AssistantController::stopWakeMonitor()
{
    m_wakeMonitorEnabled = false;
    if (m_audioCaptureMode == AudioCaptureMode::WakeMonitor) {
        m_audioInputService->stop();
        m_audioCaptureMode = AudioCaptureMode::None;
        if (m_loggingService) {
            m_loggingService->info(QStringLiteral("Wake monitor stopped."));
        }
    }
}

void AssistantController::stopListening()
{
    const QByteArray pcm = m_audioInputService->recordedPcm();
    const AudioCaptureMode completedMode = m_audioCaptureMode;
    m_lastCompletedCaptureMode = completedMode;
    m_audioInputService->stop();
    m_audioCaptureMode = AudioCaptureMode::None;
    if (m_loggingService) {
        const QString mode = completedMode == AudioCaptureMode::WakeMonitor
            ? QStringLiteral("wake-monitor")
            : completedMode == AudioCaptureMode::Direct
                ? QStringLiteral("direct")
                : QStringLiteral("none");
        m_loggingService->info(QStringLiteral("Audio capture stopped. mode=%1 bytes=%2").arg(mode).arg(pcm.size()));
    }
    emit processingRequested();
    if (pcm.isEmpty()) {
        setStatus(QStringLiteral("No audio captured"));
        scheduleWakeMonitorRestart();
        emit idleRequested();
        return;
    }
    m_whisperSttEngine->transcribePcm(pcm);
}

void AssistantController::cancelActiveRequest()
{
    m_lmStudioClient->cancelActiveRequest();
    m_piperTtsEngine->clear();
    setStatus(QStringLiteral("Request cancelled"));
    scheduleWakeMonitorRestart();
    emit idleRequested();
}

void AssistantController::setSelectedModel(const QString &modelId)
{
    m_settings->setSelectedModel(modelId);
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
    const QString &whisperPath,
    const QString &whisperModelPath,
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
    m_settings->setLmStudioEndpoint(endpoint);
    m_settings->setSelectedModel(modelId);
    m_settings->setDefaultReasoningMode(static_cast<ReasoningMode>(defaultMode));
    m_settings->setAutoRoutingEnabled(autoRouting);
    m_settings->setStreamingEnabled(streaming);
    m_settings->setRequestTimeoutMs(timeoutMs);
    m_settings->setWhisperExecutable(whisperPath);
    m_settings->setWhisperModelPath(whisperModelPath);
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
    scheduleWakeMonitorRestart(1000);
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

void AssistantController::scheduleWakeMonitorRestart(int delayMs)
{
    if (!m_wakeMonitorEnabled && m_settings->initialSetupCompleted()) {
        m_wakeMonitorEnabled = true;
    }

    if (!m_wakeMonitorEnabled) {
        return;
    }

    QTimer::singleShot(delayMs, this, [this]() {
        if (canStartWakeMonitor()) {
            startWakeMonitor();
        }
    });
}

bool AssistantController::canStartWakeMonitor() const
{
    return m_wakeMonitorEnabled
        && m_currentState == AssistantState::Idle
        && !m_audioInputService->isActive()
        && !m_piperTtsEngine->isSpeaking()
        && !m_settings->whisperExecutable().isEmpty()
        && !m_settings->whisperModelPath().isEmpty();
}

bool AssistantController::startAudioCapture(AudioCaptureMode mode, bool announceListening)
{
    if (m_audioInputService->isActive()) {
        return false;
    }

    if (m_audioInputService->start(m_settings->micSensitivity(), m_settings->selectedAudioInputDeviceId())) {
        m_audioCaptureMode = mode;
        if (m_loggingService) {
            const QString modeText = mode == AudioCaptureMode::WakeMonitor
                ? QStringLiteral("wake-monitor")
                : QStringLiteral("direct");
            m_loggingService->info(QStringLiteral("Audio capture started. mode=%1 device=\"%2\" sensitivity=%3")
                .arg(modeText, m_settings->selectedAudioInputDeviceId())
                .arg(m_settings->micSensitivity(), 0, 'f', 3));
        }
        if (announceListening) {
            setStatus(QStringLiteral("Listening"));
            emit listeningRequested();
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
    setStatus(status);
    if (speak) {
        m_piperTtsEngine->enqueueSentence(text);
    } else {
        emit idleRequested();
    }
}

void AssistantController::startConversationRequest(const QString &input)
{
    const ReasoningMode mode = m_reasoningRouter->chooseMode(input, m_settings->autoRoutingEnabled(), m_settings->defaultReasoningMode());
    const QString modelId = m_settings->selectedModel().isEmpty() && !availableModelIds().isEmpty() ? availableModelIds().first() : m_settings->selectedModel();
    if (modelId.isEmpty()) {
        setStatus(QStringLiteral("No LM Studio model selected"));
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

    m_activeRequestId = m_lmStudioClient->sendChatRequest(messages, modelId, {
        .mode = mode,
        .kind = RequestKind::Conversation,
        .stream = m_settings->streamingEnabled(),
        .timeout = std::chrono::milliseconds(m_settings->requestTimeoutMs())
    });
}

void AssistantController::startCommandRequest(const QString &input)
{
    const QString modelId = m_settings->selectedModel().isEmpty() && !availableModelIds().isEmpty() ? availableModelIds().first() : m_settings->selectedModel();
    if (modelId.isEmpty()) {
        setStatus(QStringLiteral("No LM Studio model selected"));
        emit idleRequested();
        return;
    }

    m_activeRequestKind = RequestKind::CommandExtraction;
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Starting command extraction request. model=\"%1\" input=\"%2\"")
            .arg(modelId, input.left(240)));
    }
    m_activeRequestId = m_lmStudioClient->sendChatRequest(
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
    m_responseText = text;
    emit responseTextChanged();
    m_memoryStore->appendConversation(QStringLiteral("assistant"), text);
    const QString remainder = m_streamAssembler->drainRemainingText();
    if (!remainder.isEmpty()) {
        m_piperTtsEngine->enqueueSentence(remainder);
    }
    if (!m_settings->streamingEnabled()) {
        m_piperTtsEngine->enqueueSentence(text);
    } else if (!m_piperTtsEngine->isSpeaking()) {
        scheduleWakeMonitorRestart();
        emit idleRequested();
    }
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
    m_piperTtsEngine->enqueueSentence(message);
    setStatus(QStringLiteral("Command executed"));
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
