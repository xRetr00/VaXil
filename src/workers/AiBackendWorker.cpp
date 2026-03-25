#include "workers/AiBackendWorker.h"

#include "ai/OpenAiCompatibleClient.h"

AiBackendWorker::AiBackendWorker(QObject *parent)
    : QObject(parent)
{
    m_client = new OpenAiCompatibleClient(this);
    connect(m_client, &OpenAiCompatibleClient::modelsReady, this, &AiBackendWorker::modelsReady);
    connect(m_client, &OpenAiCompatibleClient::availabilityChanged, this, &AiBackendWorker::availabilityChanged);
    connect(m_client, &OpenAiCompatibleClient::capabilitiesChanged, this, &AiBackendWorker::capabilitiesChanged);
    connect(m_client, &OpenAiCompatibleClient::requestStarted, this, [this](quint64) {
        emit requestStarted(m_lastRequestedGenerationId);
    });
    connect(m_client, &OpenAiCompatibleClient::requestDelta, this, [this](quint64 requestId, const QString &delta) {
        emit requestDelta(m_requestGenerationMap.value(requestId, requestId), delta);
    });
    connect(m_client, &OpenAiCompatibleClient::requestFinished, this, [this](quint64 requestId, const QString &text) {
        const quint64 generationId = m_requestGenerationMap.take(requestId);
        emit requestFinished(generationId == 0 ? requestId : generationId, text);
    });
    connect(m_client, &OpenAiCompatibleClient::agentResponseReady, this, [this](quint64 requestId, const AgentResponse &response) {
        const quint64 generationId = m_requestGenerationMap.take(requestId);
        emit agentResponseReady(generationId == 0 ? requestId : generationId, response);
    });
    connect(m_client, &OpenAiCompatibleClient::requestFailed, this, [this](quint64 requestId, const QString &errorText) {
        const quint64 generationId = m_requestGenerationMap.take(requestId);
        emit requestFailed(generationId == 0 ? requestId : generationId, errorText);
    });
}

void AiBackendWorker::setEndpoint(const QString &endpoint)
{
    m_client->setEndpoint(endpoint);
}

void AiBackendWorker::refreshModels()
{
    m_client->fetchModels();
}

void AiBackendWorker::sendRequest(quint64 generationId, const QList<AiMessage> &messages, const QString &model, const AiRequestOptions &options)
{
    m_lastRequestedGenerationId = generationId;
    const quint64 requestId = m_client->sendChatRequest(messages, model, options);
    m_requestGenerationMap.insert(requestId, generationId);
}

void AiBackendWorker::sendAgentRequest(quint64 generationId, const AgentRequest &request)
{
    m_lastRequestedGenerationId = generationId;
    const quint64 requestId = m_client->sendAgentRequest(request);
    m_requestGenerationMap.insert(requestId, generationId);
}

void AiBackendWorker::cancelActiveRequest()
{
    m_client->cancelActiveRequest();
}
