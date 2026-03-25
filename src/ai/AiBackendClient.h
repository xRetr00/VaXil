#pragma once

#include <QObject>

#include "core/AssistantTypes.h"

class AiBackendClient : public QObject
{
    Q_OBJECT

public:
    explicit AiBackendClient(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~AiBackendClient() override = default;

    virtual void setEndpoint(const QString &endpoint) = 0;
    virtual QString endpoint() const = 0;
    virtual void fetchModels() = 0;
    virtual AgentCapabilitySet capabilities() const = 0;
    virtual quint64 sendChatRequest(const QList<AiMessage> &messages, const QString &model, const AiRequestOptions &options) = 0;
    virtual quint64 sendAgentRequest(const AgentRequest &request) = 0;
    virtual void cancelActiveRequest() = 0;

signals:
    void modelsReady(const QList<ModelInfo> &models);
    void availabilityChanged(const AiAvailability &availability);
    void capabilitiesChanged(const AgentCapabilitySet &capabilities);
    void requestStarted(quint64 requestId);
    void requestDelta(quint64 requestId, const QString &delta);
    void requestFinished(quint64 requestId, const QString &fullText);
    void agentResponseReady(quint64 requestId, const AgentResponse &response);
    void requestFailed(quint64 requestId, const QString &errorText);
};
