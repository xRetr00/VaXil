#include "core/ResponseFinalizer.h"

#include "ai/SpokenReply.h"
#include "logging/LoggingService.h"
#include "memory/MemoryStore.h"
#include "tts/TtsEngine.h"

ResponseFinalizer::ResponseFinalizer(MemoryStore *memoryStore,
                                     TtsEngine *ttsEngine,
                                     LoggingService *loggingService)
    : m_memoryStore(memoryStore)
    , m_ttsEngine(ttsEngine)
    , m_loggingService(loggingService)
{
}

bool ResponseFinalizer::finalizeResponse(const QString &source,
                                         const SpokenReply &reply,
                                         QString *responseText,
                                         const std::function<void()> &emitResponseChanged,
                                         const std::function<void()> &refreshConversationSession,
                                         const std::function<void(const QString &, const QString &, const QString &)> &logPromptResponsePair,
                                         const QString &status,
                                         const std::function<void(const QString &)> &setStatus) const
{
    if (responseText) {
        *responseText = reply.displayText;
    }
    if (emitResponseChanged) {
        emitResponseChanged();
    }
    if (m_memoryStore) {
        m_memoryStore->appendConversation(QStringLiteral("assistant"), reply.displayText);
    }
    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("Response finalized. source=\"%1\" speak=%2 chars=%3")
                                   .arg(source, reply.shouldSpeak ? QStringLiteral("true") : QStringLiteral("false"))
                                   .arg(reply.displayText.size()));
    }
    if (logPromptResponsePair) {
        logPromptResponsePair(reply.displayText, source, status);
    }
    if (setStatus) {
        setStatus(status);
    }
    if (reply.shouldSpeak && !reply.spokenText.isEmpty() && m_ttsEngine) {
        if (refreshConversationSession) {
            refreshConversationSession();
        }
        m_ttsEngine->speakText(reply.spokenText);
        return true;
    }
    return false;
}
