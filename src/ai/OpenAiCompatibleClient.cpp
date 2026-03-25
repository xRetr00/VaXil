#include "ai/OpenAiCompatibleClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QScopeGuard>
#include <QUrl>

namespace {
QString normalizeEndpoint(QString endpoint)
{
    endpoint = endpoint.trimmed();
    if (endpoint.isEmpty()) {
        return QStringLiteral("http://localhost:1234");
    }

    while (endpoint.endsWith('/')) {
        endpoint.chop(1);
    }

    if (endpoint.endsWith(QStringLiteral("/v1"), Qt::CaseInsensitive)) {
        endpoint.chop(3);
        while (endpoint.endsWith('/')) {
            endpoint.chop(1);
        }
    }

    return endpoint;
}
}

OpenAiCompatibleClient::OpenAiCompatibleClient(QObject *parent)
    : AiBackendClient(parent)
{
    m_timeoutTimer.setSingleShot(true);
    connect(&m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_activeReply) {
            m_activeReply->abort();
        }
        finishWithFailure(m_activeRequestId, QStringLiteral("Local AI backend request timed out"));
    });
}

void OpenAiCompatibleClient::setEndpoint(const QString &endpoint)
{
    m_endpoint = normalizeEndpoint(endpoint);
}

QString OpenAiCompatibleClient::endpoint() const
{
    return m_endpoint;
}

void OpenAiCompatibleClient::fetchModels()
{
    auto *reply = m_networkAccessManager.get(buildJsonRequest(QStringLiteral("/v1/models")));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto cleanup = qScopeGuard([reply]() { reply->deleteLater(); });

        if (reply->error() != QNetworkReply::NoError) {
            emit availabilityChanged({false, false, QStringLiteral("Local AI backend offline")});
            emit modelsReady({});
            return;
        }

        const auto document = QJsonDocument::fromJson(reply->readAll());
        QList<ModelInfo> models;
        for (const auto &value : document.object().value(QStringLiteral("data")).toArray()) {
            const auto object = value.toObject();
            models.push_back({
                .id = object.value(QStringLiteral("id")).toString(),
                .ownedBy = object.value(QStringLiteral("owned_by")).toString()
            });
        }

        emit availabilityChanged({true, !models.isEmpty(), QStringLiteral("Local AI backend connected")});
        emit modelsReady(models);
    });
}

quint64 OpenAiCompatibleClient::sendChatRequest(const QList<AiMessage> &messages, const QString &model, const AiRequestOptions &options)
{
    cancelActiveRequest();

    QJsonArray jsonMessages;
    for (const auto &message : messages) {
        jsonMessages.push_back(QJsonObject{
            {QStringLiteral("role"), message.role},
            {QStringLiteral("content"), message.content}
        });
    }

    QJsonObject body{
        {QStringLiteral("model"), model},
        {QStringLiteral("messages"), jsonMessages},
        {QStringLiteral("temperature"), options.temperature},
        {QStringLiteral("stream"), options.stream}
    };

    if (options.topP.has_value()) {
        body.insert(QStringLiteral("top_p"), options.topP.value());
    }
    if (options.maxTokens.has_value()) {
        body.insert(QStringLiteral("max_tokens"), options.maxTokens.value());
    }
    if (options.seed.has_value()) {
        body.insert(QStringLiteral("seed"), options.seed.value());
    }

    m_activeRequestId = ++m_requestCounter;
    m_streamBuffer.clear();
    m_streamedContent.clear();

    QNetworkRequest request = buildJsonRequest(QStringLiteral("/v1/chat/completions"));
    m_activeReply = m_networkAccessManager.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    const quint64 requestId = m_activeRequestId;
    QPointer<QNetworkReply> reply = m_activeReply;
    emit requestStarted(requestId);
    m_timeoutTimer.start(static_cast<int>(options.timeout.count()));

    if (options.stream) {
        connect(reply, &QIODevice::readyRead, this, [this, requestId, reply]() {
            if (!reply || reply != m_activeReply || requestId != m_activeRequestId) {
                return;
            }
            handleStreamingReply(requestId, reply);
        });
    }

    connect(reply, &QNetworkReply::finished, this, [this, options, requestId, reply]() {
        if (!reply) {
            return;
        }

        const bool staleReply = reply != m_activeReply || requestId != m_activeRequestId;
        if (staleReply) {
            reply->deleteLater();
            return;
        }

        m_timeoutTimer.stop();
        const auto cleanup = qScopeGuard([this, reply]() {
            reply->deleteLater();
            m_activeReply = nullptr;
        });

        if (reply->error() != QNetworkReply::NoError) {
            finishWithFailure(m_activeRequestId, parseErrorMessage(reply));
            return;
        }

        if (options.stream) {
            handleStreamingReply(requestId, reply);
            emit requestFinished(requestId, m_streamedContent);
            return;
        }

        const auto document = QJsonDocument::fromJson(reply->readAll());
        const auto choices = document.object().value(QStringLiteral("choices")).toArray();
        const auto content = choices.isEmpty()
            ? QString{}
            : choices.first().toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
        emit requestFinished(requestId, content);
    });

    return m_activeRequestId;
}

void OpenAiCompatibleClient::cancelActiveRequest()
{
    m_timeoutTimer.stop();
    if (m_activeReply) {
        m_activeReply->abort();
        m_activeReply->deleteLater();
        m_activeReply = nullptr;
    }
    m_activeRequestId = 0;
}

void OpenAiCompatibleClient::finishWithFailure(quint64 requestId, const QString &errorText)
{
    emit requestFailed(requestId, errorText);
}

QNetworkRequest OpenAiCompatibleClient::buildJsonRequest(const QString &path) const
{
    QNetworkRequest request(QUrl(m_endpoint + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return request;
}

QString OpenAiCompatibleClient::parseErrorMessage(QNetworkReply *reply) const
{
    const QByteArray body = reply != nullptr ? reply->readAll() : QByteArray{};
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        const QJsonObject root = document.object();
        if (root.contains(QStringLiteral("error"))) {
            const QJsonValue error = root.value(QStringLiteral("error"));
            if (error.isObject()) {
                const QString message = error.toObject().value(QStringLiteral("message")).toString();
                if (!message.isEmpty()) {
                    return message;
                }
            } else if (error.isString()) {
                return error.toString();
            }
        }

        const QString message = root.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            return message;
        }
    }

    if (!body.trimmed().isEmpty()) {
        return QString::fromUtf8(body).trimmed();
    }

    return reply != nullptr ? reply->errorString() : QStringLiteral("Local AI backend request failed");
}

void OpenAiCompatibleClient::handleStreamingReply(quint64 requestId, QNetworkReply *reply)
{
    m_streamBuffer += reply->readAll();

    if (reply != nullptr && reply->isFinished() && !m_streamBuffer.endsWith("\n\n") && !m_streamBuffer.endsWith("\r\n\r\n")) {
        m_streamBuffer.append("\n\n");
    }

    int separatorSize = 0;
    int separatorIndex = m_streamBuffer.indexOf("\r\n\r\n");
    if (separatorIndex >= 0) {
        separatorSize = 4;
    } else {
        separatorIndex = m_streamBuffer.indexOf("\n\n");
        if (separatorIndex >= 0) {
            separatorSize = 2;
        }
    }

    while (separatorIndex >= 0) {
        QByteArray rawEvent = m_streamBuffer.left(separatorIndex);
        m_streamBuffer.remove(0, separatorIndex + separatorSize);

        rawEvent.replace("\r", "");
        const QList<QByteArray> lines = rawEvent.split('\n');

        QByteArray payload;
        for (const QByteArray &lineRaw : lines) {
            const QByteArray line = lineRaw.trimmed();
            if (line.startsWith("data:")) {
                QByteArray segment = line.mid(5);
                if (segment.startsWith(' ')) {
                    segment.remove(0, 1);
                }
                if (!payload.isEmpty()) {
                    payload.append('\n');
                }
                payload.append(segment);
            }
        }

        if (payload.isEmpty() || payload == "[DONE]") {
            separatorIndex = m_streamBuffer.indexOf("\n\n");
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            separatorIndex = m_streamBuffer.indexOf("\n\n");
            continue;
        }

        const QJsonArray choices = document.object().value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            separatorIndex = m_streamBuffer.indexOf("\n\n");
            continue;
        }

        const QJsonObject choice = choices.first().toObject();
        const QJsonObject deltaObject = choice.value(QStringLiteral("delta")).toObject();
        const QString delta = deltaObject.value(QStringLiteral("content")).toString();
        if (!delta.isEmpty()) {
            m_streamedContent += delta;
            emit requestDelta(requestId, delta);
        }

        separatorIndex = m_streamBuffer.indexOf("\r\n\r\n");
        if (separatorIndex >= 0) {
            separatorSize = 4;
        } else {
            separatorIndex = m_streamBuffer.indexOf("\n\n");
            separatorSize = separatorIndex >= 0 ? 2 : 0;
        }
    }
}
