#pragma once

#include <QHash>
#include <QObject>

#include "core/AssistantTypes.h"

class OpenAiCompatibleClient;

class AiBackendWorker : public QObject
{
    Q_OBJECT

public:
    explicit AiBackendWorker(QObject *parent = nullptr);

public slots:
    void setEndpoint(const QString &endpoint);
    void refreshModels();
    void sendRequest(quint64 generationId, const QList<AiMessage> &messages, const QString &model, const AiRequestOptions &options);
    void sendAgentRequest(quint64 generationId, const AgentRequest &request);
    void cancelActiveRequest();

signals:
    void modelsReady(const QList<ModelInfo> &models);
    void availabilityChanged(const AiAvailability &availability);
    void capabilitiesChanged(const AgentCapabilitySet &capabilities);
    void requestStarted(quint64 generationId);
    void requestDelta(quint64 generationId, const QString &delta);
    void requestFinished(quint64 generationId, const QString &text);
    void agentResponseReady(quint64 generationId, const AgentResponse &response);
    void requestFailed(quint64 generationId, const QString &errorText);

private:
    OpenAiCompatibleClient *m_client = nullptr;
    QHash<quint64, quint64> m_requestGenerationMap;
    quint64 m_lastRequestedGenerationId = 0;
};
