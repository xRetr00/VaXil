#include "workers/AiBackendWorker.h"

#include "ai/OpenAiCompatibleClient.h"

AiBackendWorker::AiBackendWorker(QObject *parent)
    : QObject(parent)
{
    m_client = new OpenAiCompatibleClient(this);
    connect(m_client, &OpenAiCompatibleClient::modelsReady, this, &AiBackendWorker::modelsReady);
    connect(m_client, &OpenAiCompatibleClient::availabilityChanged, this, &AiBackendWorker::availabilityChanged);
    connect(m_client, &OpenAiCompatibleClient::requestStarted, this, [this](quint64) {
        emit requestStarted(m_generationId);
    });
    connect(m_client, &OpenAiCompatibleClient::requestDelta, this, [this](quint64, const QString &delta) {
        emit requestDelta(m_generationId, delta);
    });
    connect(m_client, &OpenAiCompatibleClient::requestFinished, this, [this](quint64, const QString &text) {
        emit requestFinished(m_generationId, text);
    });
    connect(m_client, &OpenAiCompatibleClient::requestFailed, this, [this](quint64, const QString &errorText) {
        emit requestFailed(m_generationId, errorText);
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
    m_generationId = generationId;
    m_client->sendChatRequest(messages, model, options);
}

void AiBackendWorker::cancelActiveRequest()
{
    ++m_generationId;
    m_client->cancelActiveRequest();
}
