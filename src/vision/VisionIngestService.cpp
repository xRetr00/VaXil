#include "vision/VisionIngestService.h"

#include <optional>

#include <QAbstractSocket>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketServer>

#include "logging/LoggingService.h"
#include "settings/AppSettings.h"

namespace {
constexpr int kDefaultVisionPort = 8765;
constexpr int kDispatchIntervalMs = 75;

struct ParseSnapshotResult {
    std::optional<VisionSnapshot> snapshot;
    QString dropReason;
    QString detail;
    int filteredObjects = 0;
    int filteredGestures = 0;
};

QHostAddress hostAddressForUrl(const QUrl &url)
{
    const QString host = url.host().trimmed();
    if (host.isEmpty() || host == QStringLiteral("0.0.0.0") || host == QStringLiteral("*")) {
        return QHostAddress::Any;
    }

    QHostAddress address;
    if (address.setAddress(host)) {
        return address;
    }
    return QHostAddress::Any;
}

QString normalizeEndpoint(QString endpoint)
{
    endpoint = endpoint.trimmed();
    if (endpoint.isEmpty()) {
        return QStringLiteral("ws://0.0.0.0:8765/vision");
    }
    if (!endpoint.contains(QStringLiteral("://"))) {
        endpoint.prepend(QStringLiteral("ws://"));
    }
    return endpoint;
}

QString deriveSummary(const VisionSnapshot &snapshot)
{
    if (!snapshot.summary.trimmed().isEmpty()) {
        return snapshot.summary.trimmed();
    }

    QStringList parts;
    if (!snapshot.objects.isEmpty()) {
        QStringList values;
        for (const auto &object : snapshot.objects) {
            if (!object.className.trimmed().isEmpty()) {
                values.push_back(object.className.trimmed());
            }
        }
        values.removeDuplicates();
        if (!values.isEmpty()) {
            parts.push_back(QStringLiteral("Objects: %1").arg(values.join(QStringLiteral(", "))));
        }
    }

    if (!snapshot.gestures.isEmpty()) {
        QStringList values;
        for (const auto &gesture : snapshot.gestures) {
            if (!gesture.name.trimmed().isEmpty()) {
                values.push_back(gesture.name.trimmed());
            }
        }
        values.removeDuplicates();
        if (!values.isEmpty()) {
            parts.push_back(QStringLiteral("Gestures: %1").arg(values.join(QStringLiteral(", "))));
        }
    }

    if (snapshot.fingerCount >= 0) {
        parts.push_back(QStringLiteral("Finger count: %1").arg(snapshot.fingerCount));
    }

    if (parts.isEmpty()) {
        return QStringLiteral("No strong visual event detected");
    }
    return parts.join(QStringLiteral(". "));
}

ParseSnapshotResult parseSnapshotMessage(const QString &payload,
                                         int staleThresholdMs,
                                         double objectsMinConfidence,
                                         double gesturesMinConfidence)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parseError);
    if (document.isNull() || !document.isObject()) {
        return {.dropReason = QStringLiteral("invalid_json"),
                .detail = parseError.errorString().isEmpty() ? QStringLiteral("Invalid JSON payload.") : parseError.errorString()};
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("type")).toString() != QStringLiteral("vision.snapshot")) {
        return {.dropReason = QStringLiteral("unsupported_type"),
                .detail = QStringLiteral("Unsupported message type.")};
    }

    VisionSnapshot snapshot;
    snapshot.type = object.value(QStringLiteral("type")).toString();
    snapshot.schemaVersion = object.value(QStringLiteral("version")).toString(QStringLiteral("1.0"));
    if (snapshot.schemaVersion != QStringLiteral("1.0")) {
        return {.dropReason = QStringLiteral("unsupported_schema"),
                .detail = QStringLiteral("Unsupported schema version.")};
    }

    snapshot.nodeId = object.value(QStringLiteral("node_id")).toString().trimmed();
    snapshot.traceId = object.value(QStringLiteral("trace_id")).toString().trimmed();
    snapshot.summary = object.value(QStringLiteral("summary")).toString().trimmed();
    if (object.contains(QStringLiteral("finger_count")) && object.value(QStringLiteral("finger_count")).isDouble()) {
        snapshot.fingerCount = object.value(QStringLiteral("finger_count")).toInt(-1);
    }

    const QString timestampText = object.value(QStringLiteral("ts")).toString().trimmed();
    snapshot.timestamp = QDateTime::fromString(timestampText, Qt::ISODateWithMs);
    if (!snapshot.timestamp.isValid()) {
        snapshot.timestamp = QDateTime::fromString(timestampText, Qt::ISODate);
    }
    if (!snapshot.timestamp.isValid()) {
        return {.dropReason = QStringLiteral("invalid_timestamp"),
                .detail = QStringLiteral("Snapshot timestamp is invalid.")};
    }
    snapshot.timestamp = snapshot.timestamp.toUTC();

    const qint64 ageMs = std::max<qint64>(0, snapshot.timestamp.msecsTo(QDateTime::currentDateTimeUtc()));
    if (ageMs > std::max(100, staleThresholdMs)) {
        return {.dropReason = QStringLiteral("stale_rejected"),
                .detail = QStringLiteral("Snapshot age %1 ms exceeded stale threshold.").arg(ageMs)};
    }

    ParseSnapshotResult result;
    const QJsonArray objects = object.value(QStringLiteral("objects")).toArray();
    for (const QJsonValue &entry : objects) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject item = entry.toObject();
        VisionObjectDetection detectedObject;
        detectedObject.className = item.value(QStringLiteral("class")).toString().trimmed();
        detectedObject.confidence = item.value(QStringLiteral("confidence")).toDouble();
        if (detectedObject.className.isEmpty()) {
            continue;
        }
        if (detectedObject.confidence < objectsMinConfidence) {
            ++result.filteredObjects;
            continue;
        }
        snapshot.objects.push_back(detectedObject);
    }

    const QJsonArray gestures = object.value(QStringLiteral("gestures")).toArray();
    for (const QJsonValue &entry : gestures) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject item = entry.toObject();
        VisionGestureDetection gesture;
        gesture.name = item.value(QStringLiteral("name")).toString().trimmed();
        gesture.confidence = item.value(QStringLiteral("confidence")).toDouble();
        if (gesture.name.isEmpty()) {
            continue;
        }
        if (gesture.confidence < gesturesMinConfidence) {
            ++result.filteredGestures;
            continue;
        }
        snapshot.gestures.push_back(gesture);
    }

    snapshot.summary = deriveSummary(snapshot);
    if (snapshot.nodeId.isEmpty()) {
        snapshot.nodeId = QStringLiteral("vision-node");
    }
    result.snapshot = snapshot;
    return result;
}
}

class VisionIngestService::Worker final : public QObject
{
    Q_OBJECT

public:
    explicit Worker(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_dispatchTimer = new QTimer(this);
        m_dispatchTimer->setInterval(kDispatchIntervalMs);
        connect(m_dispatchTimer, &QTimer::timeout, this, &Worker::dispatchLatestSnapshot);
    }

public slots:
    void applyConfiguration(bool enabled,
                            const QString &endpoint,
                            int timeoutMs,
                            int staleThresholdMs,
                            double objectsMinConfidence,
                            double gesturesMinConfidence)
    {
        m_enabled = enabled;
        m_endpoint = normalizeEndpoint(endpoint);
        m_timeoutMs = timeoutMs;
        m_staleThresholdMs = staleThresholdMs;
        m_objectsMinConfidence = objectsMinConfidence;
        m_gesturesMinConfidence = gesturesMinConfidence;

        if (!m_enabled) {
            stopServer();
            emit statusReady(QStringLiteral("Vision ingest disabled"), false);
            return;
        }

        openServer();
    }

    void shutdown()
    {
        stopServer();
    }

signals:
    void snapshotReady(const VisionSnapshot &snapshot);
    void statusReady(const QString &status, bool connected);
    void dropReady(const QString &reason, const QString &detail);

private slots:
    void handleNewConnection()
    {
        if (!m_server) {
            return;
        }

        while (m_server->hasPendingConnections()) {
            QWebSocket *socket = m_server->nextPendingConnection();
            if (!socket) {
                continue;
            }

            m_clients.push_back(socket);
            connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &payload) {
                handleTextMessage(socket, payload);
            });
            connect(socket, &QWebSocket::disconnected, this, [this, socket]() {
                m_clients.removeAll(socket);
                socket->deleteLater();
                emit statusReady(QStringLiteral("Vision node disconnected"), !m_clients.isEmpty());
            });
            connect(socket, &QWebSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
                emit statusReady(QStringLiteral("Vision socket error from %1").arg(socket->peerAddress().toString()), !m_clients.isEmpty());
            });

            emit statusReady(QStringLiteral("Vision node connected from %1").arg(socket->peerAddress().toString()), true);
        }
    }

    void dispatchLatestSnapshot()
    {
        if (!m_latestSnapshot.has_value() || m_latestSequence == m_dispatchedSequence) {
            return;
        }
        m_dispatchedSequence = m_latestSequence;
        emit snapshotReady(*m_latestSnapshot);
    }

private:
    void openServer()
    {
        stopServer();

        const QUrl url(m_endpoint);
        const QHostAddress bindAddress = hostAddressForUrl(url);
        const quint16 port = static_cast<quint16>(url.port(kDefaultVisionPort));

        m_server = new QWebSocketServer(QStringLiteral("JARVIS Vision Ingest"),
                                        QWebSocketServer::NonSecureMode,
                                        this);
        connect(m_server, &QWebSocketServer::newConnection, this, &Worker::handleNewConnection);
        connect(m_server, &QWebSocketServer::acceptError, this, [this](QAbstractSocket::SocketError) {
            emit statusReady(QStringLiteral("Vision ingest accept error"), false);
        });

        if (!m_server->listen(bindAddress, port)) {
            emit statusReady(QStringLiteral("Vision ingest failed to bind %1").arg(m_endpoint), false);
            m_server->deleteLater();
            m_server = nullptr;
            return;
        }

        if (m_dispatchTimer != nullptr) {
            m_dispatchTimer->start();
        }
        emit statusReady(QStringLiteral("Vision ingest listening on %1").arg(m_endpoint), false);
    }

    void stopServer()
    {
        if (m_dispatchTimer != nullptr) {
            m_dispatchTimer->stop();
        }
        for (QPointer<QWebSocket> &client : m_clients) {
            if (client) {
                client->close();
                client->deleteLater();
            }
        }
        m_clients.clear();

        if (m_server) {
            m_server->close();
            m_server->deleteLater();
            m_server = nullptr;
        }
    }

    void handleTextMessage(QWebSocket *socket, const QString &payload)
    {
        const ParseSnapshotResult parsed = parseSnapshotMessage(payload,
                                                               m_staleThresholdMs,
                                                               m_objectsMinConfidence,
                                                               m_gesturesMinConfidence);
        if (!parsed.snapshot.has_value()) {
            emit dropReady(parsed.dropReason,
                           QStringLiteral("peer=\"%1\" %2")
                               .arg(socket != nullptr ? socket->peerAddress().toString() : QStringLiteral("unknown"),
                                    parsed.detail));
            return;
        }

        if (parsed.filteredObjects > 0 || parsed.filteredGestures > 0) {
            emit dropReady(QStringLiteral("confidence_filtered"),
                           QStringLiteral("peer=\"%1\" objects_filtered=%2 gestures_filtered=%3")
                               .arg(socket != nullptr ? socket->peerAddress().toString() : QStringLiteral("unknown"))
                               .arg(parsed.filteredObjects)
                               .arg(parsed.filteredGestures));
        }

        m_latestSnapshot = parsed.snapshot;
        ++m_latestSequence;
        if (m_dispatchTimer != nullptr && !m_dispatchTimer->isActive()) {
            m_dispatchTimer->start();
        }
    }

    bool m_enabled = false;
    QString m_endpoint;
    int m_timeoutMs = 5000;
    int m_staleThresholdMs = 2000;
    double m_objectsMinConfidence = 0.60;
    double m_gesturesMinConfidence = 0.70;
    QWebSocketServer *m_server = nullptr;
    QList<QPointer<QWebSocket>> m_clients;
    QTimer *m_dispatchTimer = nullptr;
    std::optional<VisionSnapshot> m_latestSnapshot;
    quint64 m_latestSequence = 0;
    quint64 m_dispatchedSequence = 0;
};

VisionIngestService::VisionIngestService(AppSettings *settings, LoggingService *loggingService, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_loggingService(loggingService)
{
    m_thread.setObjectName(QStringLiteral("VisionIngestThread"));
    m_worker = new Worker();
    m_worker->moveToThread(&m_thread);

    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &Worker::snapshotReady, this, &VisionIngestService::visionSnapshotReceived, Qt::QueuedConnection);
    connect(m_worker, &Worker::statusReady, this, &VisionIngestService::handleWorkerStatus, Qt::QueuedConnection);
    connect(m_worker, &Worker::dropReady, this, &VisionIngestService::handleWorkerDrop, Qt::QueuedConnection);

    if (m_settings) {
        connect(m_settings, &AppSettings::settingsChanged, this, &VisionIngestService::reconfigureFromSettings);
    }
}

VisionIngestService::~VisionIngestService()
{
    shutdown();
}

void VisionIngestService::start()
{
    if (!m_thread.isRunning()) {
        m_thread.start();
    }
    reconfigureFromSettings();
}

void VisionIngestService::shutdown()
{
    if (m_thread.isRunning()) {
        QMetaObject::invokeMethod(m_worker, "shutdown", Qt::QueuedConnection);
        m_thread.quit();
        m_thread.wait();
    }
}

QString VisionIngestService::status() const
{
    return m_status;
}

void VisionIngestService::reconfigureFromSettings()
{
    if (!m_thread.isRunning() || m_worker == nullptr || m_settings == nullptr) {
        return;
    }

    QMetaObject::invokeMethod(
        m_worker,
        "applyConfiguration",
        Qt::QueuedConnection,
        Q_ARG(bool, m_settings->visionEnabled()),
        Q_ARG(QString, m_settings->visionEndpoint()),
        Q_ARG(int, m_settings->visionTimeoutMs()),
        Q_ARG(int, m_settings->visionStaleThresholdMs()),
        Q_ARG(double, m_settings->visionObjectsMinConfidence()),
        Q_ARG(double, m_settings->visionGesturesMinConfidence()));
}

void VisionIngestService::handleWorkerStatus(const QString &status, bool connected)
{
    m_status = status;
    if (m_loggingService) {
        m_loggingService->logVisionStatus(status, QStringLiteral("vision_status_%1").arg(status.left(96)), connected ? 1000 : 2500);
    }
    emit statusChanged(status, connected);
}

void VisionIngestService::handleWorkerDrop(const QString &reason, const QString &detail)
{
    if (!m_loggingService) {
        return;
    }

    const QString rateKey = QStringLiteral("vision_drop_%1_%2").arg(reason, detail.left(48));
    m_loggingService->logVisionDrop(reason,
                                    detail,
                                    rateKey,
                                    reason == QStringLiteral("stale_rejected") ? 1000 : 2000);
}

#include "VisionIngestService.moc"
