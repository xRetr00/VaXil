#pragma once

#include <QObject>
#include <QThread>

#include "core/AssistantTypes.h"

class AppSettings;
class LoggingService;

class VisionIngestService final : public QObject
{
    Q_OBJECT

public:
    VisionIngestService(AppSettings *settings, LoggingService *loggingService, QObject *parent = nullptr);
    ~VisionIngestService() override;

    void start();
    void shutdown();
    QString status() const;

signals:
    void visionSnapshotReceived(const VisionSnapshot &snapshot);
    void statusChanged(const QString &status, bool connected);

private slots:
    void reconfigureFromSettings();
    void handleWorkerStatus(const QString &status, bool connected);
    void handleWorkerDrop(const QString &reason, const QString &detail);

private:
    class Worker;

    AppSettings *m_settings = nullptr;
    LoggingService *m_loggingService = nullptr;
    QThread m_thread;
    Worker *m_worker = nullptr;
    QString m_status = QStringLiteral("Vision ingest disabled");
};
