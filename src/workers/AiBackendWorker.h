#pragma once

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
    void cancelActiveRequest();

signals:
    void modelsReady(const QList<ModelInfo> &models);
    void availabilityChanged(const AiAvailability &availability);
    void requestStarted(quint64 generationId);
    void requestDelta(quint64 generationId, const QString &delta);
    void requestFinished(quint64 generationId, const QString &text);
    void requestFailed(quint64 generationId, const QString &errorText);

private:
    OpenAiCompatibleClient *m_client = nullptr;
    quint64 m_generationId = 0;
};
