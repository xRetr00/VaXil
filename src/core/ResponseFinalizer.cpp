#include "core/ResponseFinalizer.h"

#include "ai/SpokenReply.h"
#include "logging/LoggingService.h"
#include "memory/MemoryStore.h"
#include "tts/TtsEngine.h"

namespace {
bool isDisplaySafeHint(const QString &hint)
{
    const QString lowered = hint.trimmed().toLower();
    return !lowered.contains(QStringLiteral("ground the answer"))
        && !lowered.contains(QStringLiteral("retrieved evidence"))
        && !lowered.contains(QStringLiteral("inspect and verify state"))
        && !lowered.contains(QStringLiteral("side-effecting action"))
        && !lowered.contains(QStringLiteral("smallest useful tool surface"))
        && !lowered.contains(QStringLiteral("request changes state"))
        && !lowered.contains(QStringLiteral("keep execution explicit"));
}

QString modeAwareStatus(const ActionSession &session, const QString &status)
{
    if (!status.trimmed().isEmpty() && status != QStringLiteral("Response ready")) {
        return status;
    }

    switch (session.responseMode) {
    case ResponseMode::ActWithProgress:
        return QStringLiteral("Task finished");
    case ResponseMode::Act:
        return QStringLiteral("Request handled");
    case ResponseMode::Recover:
        return QStringLiteral("Recovery response");
    case ResponseMode::Confirm:
        return QStringLiteral("Confirmation needed");
    case ResponseMode::Summarize:
        return QStringLiteral("Direct response");
    case ResponseMode::Clarify:
        return QStringLiteral("Clarification needed");
    case ResponseMode::Chat:
    default:
        return status.trimmed().isEmpty() ? QStringLiteral("Response ready") : status;
    }
}

bool shouldAppendHint(const ActionSession &session, const QString &text)
{
    if (session.responseMode == ResponseMode::Chat
        || session.nextStepHint.trimmed().isEmpty()
        || !isDisplaySafeHint(session.nextStepHint)) {
        return false;
    }

    const QString simplified = text.simplified();
    return simplified.isEmpty()
        || simplified == session.successSummary.simplified()
        || simplified == session.failureSummary.simplified()
        || session.responseMode == ResponseMode::Confirm
        || session.responseMode == ResponseMode::Recover;
}
}

ResponseFinalizer::ResponseFinalizer(MemoryStore *memoryStore,
                                     TtsEngine *ttsEngine,
                                     LoggingService *loggingService)
    : m_memoryStore(memoryStore)
    , m_ttsEngine(ttsEngine)
    , m_loggingService(loggingService)
{
}

bool ResponseFinalizer::willAppendHint(const ActionSession &session,
                                       const SpokenReply &reply) const
{
    return shouldAppendHint(session, reply.displayText);
}

bool ResponseFinalizer::finalizeResponse(const QString &source,
                                         const SpokenReply &reply,
                                         const ActionSession &session,
                                         QString *responseText,
                                         const std::function<void()> &emitResponseChanged,
                                         const std::function<void()> &refreshConversationSession,
                                         const std::function<void(const QString &, const QString &, const QString &)> &logPromptResponsePair,
                                         const QString &status,
                                         const std::function<void(const QString &)> &setStatus) const
{
    SpokenReply finalizedReply = reply;
    if (finalizedReply.displayText.trimmed().isEmpty()) {
        finalizedReply.displayText = session.successSummary.trimmed();
    }
    if (finalizedReply.spokenText.trimmed().isEmpty() && finalizedReply.shouldSpeak) {
        finalizedReply.spokenText = finalizedReply.displayText;
    }
    if (shouldAppendHint(session, finalizedReply.displayText)) {
        const QString suffix = session.nextStepHint.trimmed();
        finalizedReply.displayText = finalizedReply.displayText.trimmed().isEmpty()
            ? suffix
            : QStringLiteral("%1 %2").arg(finalizedReply.displayText.trimmed(), suffix);
        if (finalizedReply.shouldSpeak) {
            finalizedReply.spokenText = finalizedReply.spokenText.trimmed().isEmpty()
                ? suffix
                : QStringLiteral("%1 %2").arg(finalizedReply.spokenText.trimmed(), suffix);
        }
    }

    const QString effectiveStatus = modeAwareStatus(session, status);

    if (responseText) {
        *responseText = finalizedReply.displayText;
    }
    if (emitResponseChanged) {
        emitResponseChanged();
    }
    if (m_memoryStore) {
        m_memoryStore->appendConversation(QStringLiteral("assistant"), finalizedReply.displayText);
    }
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Response finalized. source=\"%1\" speak=%2 chars=%3")
                                   .arg(source, finalizedReply.shouldSpeak ? QStringLiteral("true") : QStringLiteral("false"))
                                   .arg(finalizedReply.displayText.size()));
    }
    if (logPromptResponsePair) {
        logPromptResponsePair(finalizedReply.displayText, source, effectiveStatus);
    }
    if (setStatus) {
        setStatus(effectiveStatus);
    }
    if (finalizedReply.shouldSpeak && !finalizedReply.spokenText.isEmpty() && m_ttsEngine) {
        if (refreshConversationSession) {
            refreshConversationSession();
        }
        m_ttsEngine->speakText(finalizedReply.spokenText);
        return true;
    }
    return false;
}
