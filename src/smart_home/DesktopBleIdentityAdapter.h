#pragma once

#include <functional>
#include <optional>

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QObject>
#include <QString>
#include <QTimer>

#include "smart_home/SmartHomePorts.h"

class DesktopBleIdentityAdapter final : public QObject,
                                        public IdentityPresencePort
{
    Q_OBJECT

public:
    using LogSink = std::function<void(const QString &)>;

    explicit DesktopBleIdentityAdapter(const SmartHomeConfig &config,
                                       LogSink logSink = {},
                                       QObject *parent = nullptr);

    void setConfig(const SmartHomeConfig &config);
    void start();
    void stop();

    std::optional<BleIdentitySnapshot> latestIdentityPresence() const override;

    static QString normalizedBeaconUuid(const QString &uuid);
    static bool advertisementMatchesBeacon(const QBluetoothDeviceInfo &info,
                                           const QString &beaconUuid,
                                           int rssiThreshold);

private slots:
    void scanTick();
    void handleDeviceDiscovered(const QBluetoothDeviceInfo &info);
    void handleDeviceUpdated(const QBluetoothDeviceInfo &info, QBluetoothDeviceInfo::Fields fields);
    void handleScanFinished();
    void handleScanError(QBluetoothDeviceDiscoveryAgent::Error error);

private:
    bool hasUsableConfig(QString *detail = nullptr) const;
    void restartAgent();
    void startScan();
    void refreshPresenceFromClock(qint64 nowMs);
    void publishPresence(bool present, qint64 nowMs, int rssi, const QString &reasonCode);
    void logScan(const QString &event, const QString &detail = QString()) const;
    void logPresence(const BleIdentitySnapshot &snapshot, const QString &reasonCode) const;

    SmartHomeConfig m_config;
    LogSink m_logSink;
    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    QTimer *m_scanTimer = nullptr;
    BleIdentitySnapshot m_snapshot;
    qint64 m_lastSeenAtMs = 0;
    int m_latestRssi = 0;
    bool m_started = false;
    bool m_supported = false;
    int m_matchCount = 0;
    int m_scanDevicesSeen = 0;
    int m_scanUuidMatches = 0;
    int m_scanRssiRejected = 0;
};
