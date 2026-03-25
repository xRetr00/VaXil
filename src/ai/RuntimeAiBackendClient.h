#pragma once

#include "ai/AiBackendClient.h"

class VoicePipelineRuntime;

class RuntimeAiBackendClient : public AiBackendClient
{
    Q_OBJECT

public:
    explicit RuntimeAiBackendClient(VoicePipelineRuntime *runtime, QObject *parent = nullptr);

    void setEndpoint(const QString &endpoint) override;
    QString endpoint() const override;
    void fetchModels() override;
    quint64 sendChatRequest(const QList<AiMessage> &messages, const QString &model, const AiRequestOptions &options) override;
    void cancelActiveRequest() override;

private:
    VoicePipelineRuntime *m_runtime = nullptr;
    QString m_endpoint;
    quint64 m_requestCounter = 0;
    quint64 m_activeRequestId = 0;
};
