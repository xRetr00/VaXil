#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>

#include "ai/AiBackendClient.h"

class OpenAiCompatibleClient : public AiBackendClient
{
    Q_OBJECT

public:
    explicit OpenAiCompatibleClient(QObject *parent = nullptr);

    void setEndpoint(const QString &endpoint) override;
    void setProviderConfig(const QString &providerKind, const QString &apiKey) override;
    QString endpoint() const override;
    void fetchModels() override;
    AgentCapabilitySet capabilities() const override;
    quint64 sendChatRequest(const QList<AiMessage> &messages, const QString &model, const AiRequestOptions &options) override;
    quint64 sendAgentRequest(const AgentRequest &request) override;
    void cancelActiveRequest() override;

private:
    void finishWithFailure(quint64 requestId, const QString &errorText);
    QNetworkRequest buildJsonRequest(const QString &path) const;
    QString parseErrorMessage(QNetworkReply *reply) const;
    void handleStreamingReply(quint64 requestId, QNetworkReply *reply);
    void probeCapabilities();
    QString providerDisplayName() const;
    AgentResponse parseAgentResponse(const QByteArray &payload) const;
    static QString reasoningEffortForMode(ReasoningMode mode);
    static bool modelLooksToolCapable(const QString &modelId);

    QNetworkAccessManager *m_networkAccessManager = nullptr;
    QString m_endpoint = QStringLiteral("http://localhost:1234");
    QString m_providerKind = QStringLiteral("openai_compatible_local");
    QString m_apiKey;
    QPointer<QNetworkReply> m_activeReply;
    QTimer *m_timeoutTimer = nullptr;
    quint64 m_requestCounter = 0;
    quint64 m_activeRequestId = 0;
    QByteArray m_streamBuffer;
    QString m_streamedContent;
    AgentCapabilitySet m_capabilities;
};
