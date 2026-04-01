#pragma once

#include <functional>

#include <QString>

class LoggingService;
class MemoryStore;
class TtsEngine;

struct SpokenReply;

class ResponseFinalizer
{
public:
    ResponseFinalizer(MemoryStore *memoryStore,
                      TtsEngine *ttsEngine,
                      LoggingService *loggingService);

    bool finalizeResponse(const QString &source,
                          const SpokenReply &reply,
                          QString *responseText,
                          const std::function<void()> &emitResponseChanged,
                          const std::function<void()> &refreshConversationSession,
                          const std::function<void(const QString &, const QString &, const QString &)> &logPromptResponsePair,
                          const QString &status,
                          const std::function<void(const QString &)> &setStatus) const;

private:
    MemoryStore *m_memoryStore = nullptr;
    TtsEngine *m_ttsEngine = nullptr;
    LoggingService *m_loggingService = nullptr;
};
