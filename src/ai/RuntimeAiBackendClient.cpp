#include "ai/RuntimeAiBackendClient.h"

#include "workers/VoicePipelineRuntime.h"

RuntimeAiBackendClient::RuntimeAiBackendClient(VoicePipelineRuntime *runtime, QObject *parent)
    : AiBackendClient(parent)
    , m_runtime(runtime)
{
    connect(m_runtime, &VoicePipelineRuntime::modelsReady, this, &RuntimeAiBackendClient::modelsReady);
    connect(m_runtime, &VoicePipelineRuntime::availabilityChanged, this, &RuntimeAiBackendClient::availabilityChanged);
    connect(m_runtime, &VoicePipelineRuntime::capabilitiesChanged, this, [this](const AgentCapabilitySet &capabilities) {
        m_capabilities = capabilities;
        emit capabilitiesChanged(capabilities);
    });
    connect(m_runtime, &VoicePipelineRuntime::requestStarted, this, [this](quint64 requestId) {
        if (requestId == m_activeRequestId) {
            emit requestStarted(requestId);
        }
    });
    connect(m_runtime, &VoicePipelineRuntime::requestDelta, this, [this](quint64 requestId, const QString &delta) {
        if (requestId == m_activeRequestId) {
            emit requestDelta(requestId, delta);
        }
    });
    connect(m_runtime, &VoicePipelineRuntime::requestFinished, this, [this](quint64 requestId, const QString &text) {
        if (requestId == m_activeRequestId) {
            emit requestFinished(requestId, text);
        }
    });
    connect(m_runtime, &VoicePipelineRuntime::agentResponseReady, this, [this](quint64 requestId, const AgentResponse &response) {
        if (requestId == m_activeRequestId) {
            emit agentResponseReady(requestId, response);
        }
    });
    connect(m_runtime, &VoicePipelineRuntime::requestFailed, this, [this](quint64 requestId, const QString &errorText) {
        if (requestId == m_activeRequestId) {
            emit requestFailed(requestId, errorText);
        }
    });
}

void RuntimeAiBackendClient::setEndpoint(const QString &endpoint)
{
    m_endpoint = endpoint;
    m_runtime->setBackendEndpoint(endpoint);
}

QString RuntimeAiBackendClient::endpoint() const
{
    return m_endpoint;
}

void RuntimeAiBackendClient::fetchModels()
{
    m_runtime->refreshModels();
}

AgentCapabilitySet RuntimeAiBackendClient::capabilities() const
{
    return m_capabilities;
}

quint64 RuntimeAiBackendClient::sendChatRequest(const QList<AiMessage> &messages, const QString &model, const AiRequestOptions &options)
{
    m_activeRequestId = ++m_requestCounter;
    m_runtime->sendAiRequest(m_activeRequestId, messages, model, options);
    return m_activeRequestId;
}

quint64 RuntimeAiBackendClient::sendAgentRequest(const AgentRequest &request)
{
    m_activeRequestId = ++m_requestCounter;
    m_runtime->sendAgentRequest(m_activeRequestId, request);
    return m_activeRequestId;
}

void RuntimeAiBackendClient::cancelActiveRequest()
{
    ++m_requestCounter;
    m_activeRequestId = 0;
    m_runtime->cancelAiRequest();
}
