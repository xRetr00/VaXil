#include "smart_home/DesktopBleIdentityAdapter.h"

#include <algorithm>
#include <utility>

#include <QBluetoothUuid>
#include <QDateTime>
#include <QMultiHash>
#include <QUuid>

namespace {
constexpr int kDisabledRssiThreshold = -127;
constexpr int kMinimumBleScanWindowMs = 3000;
constexpr qint64 kMinuteMs = 60 * 1000;

int effectiveScanWindowMs(const SmartHomeConfig &config)
{
    return std::clamp(std::max(config.bleScanIntervalMs, kMinimumBleScanWindowMs), kMinimumBleScanWindowMs, 60000);
}

QByteArray uuidBytesFromString(const QString &uuid)
{
    const QUuid parsed(uuid.trimmed());
    if (parsed.isNull()) {
        return {};
    }
    return parsed.toRfc4122();
}

QString uuidFromIBeaconPayload(const QByteArray &data)
{
    if (data.size() < 18
        || static_cast<quint8>(data.at(0)) != 0x02
        || static_cast<quint8>(data.at(1)) != 0x15) {
        return {};
    }

    const QByteArray hex = data.mid(2, 16).toHex();
    if (hex.size() != 32) {
        return {};
    }
    return QStringLiteral("%1-%2-%3-%4-%5")
        .arg(QString::fromLatin1(hex.mid(0, 8)),
             QString::fromLatin1(hex.mid(8, 4)),
             QString::fromLatin1(hex.mid(12, 4)),
             QString::fromLatin1(hex.mid(16, 4)),
             QString::fromLatin1(hex.mid(20, 12)))
        .toLower();
}

bool byteArrayContains(const QByteArray &haystack, const QByteArray &needle)
{
    return !needle.isEmpty() && haystack.indexOf(needle) >= 0;
}

bool manufacturerPayloadContainsBeacon(const QByteArray &data, const QString &target)
{
    if (uuidFromIBeaconPayload(data) == target) {
        return true;
    }
    return byteArrayContains(data, uuidBytesFromString(target));
}

bool rssiAllowed(qint16 rssi, int threshold)
{
    return threshold <= kDisabledRssiThreshold || rssi >= threshold;
}

bool advertisementContainsBeaconUuid(const QBluetoothDeviceInfo &info, const QString &target)
{
    if (target.isEmpty()) {
        return false;
    }

    for (const QBluetoothUuid &uuid : info.serviceUuids()) {
        if (DesktopBleIdentityAdapter::normalizedBeaconUuid(uuid.toString()) == target) {
            return true;
        }
    }
    for (const QBluetoothUuid &uuid : info.serviceIds()) {
        if (DesktopBleIdentityAdapter::normalizedBeaconUuid(uuid.toString()) == target) {
            return true;
        }
    }

    const QMultiHash<QBluetoothUuid, QByteArray> serviceData = info.serviceData();
    for (auto it = serviceData.cbegin(); it != serviceData.cend(); ++it) {
        if (DesktopBleIdentityAdapter::normalizedBeaconUuid(it.key().toString()) == target
            || byteArrayContains(it.value(), uuidBytesFromString(target))) {
            return true;
        }
    }

    const QMultiHash<quint16, QByteArray> manufacturerData = info.manufacturerData();
    for (auto it = manufacturerData.cbegin(); it != manufacturerData.cend(); ++it) {
        if (manufacturerPayloadContainsBeacon(it.value(), target)) {
            return true;
        }
    }

    return false;
}
}

DesktopBleIdentityAdapter::DesktopBleIdentityAdapter(const SmartHomeConfig &config,
                                                     LogSink logSink,
                                                     QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_logSink(std::move(logSink))
    , m_scanTimer(new QTimer(this))
{
    m_snapshot.source = QStringLiteral("desktop_ble_beacon");
    connect(m_scanTimer, &QTimer::timeout, this, &DesktopBleIdentityAdapter::scanTick);
    restartAgent();
}

void DesktopBleIdentityAdapter::setConfig(const SmartHomeConfig &config)
{
    const QString previousUuid = normalizedBeaconUuid(m_config.bleBeaconUuid);
    m_config = config;
    m_scanTimer->setInterval(std::clamp(m_config.bleScanIntervalMs, 500, 60000));
    if (previousUuid != normalizedBeaconUuid(m_config.bleBeaconUuid)) {
        m_lastSeenAtMs = 0;
        m_latestRssi = 0;
        m_matchCount = 0;
        m_scanDevicesSeen = 0;
        m_scanUuidMatches = 0;
        m_scanRssiRejected = 0;
        publishPresence(false, QDateTime::currentMSecsSinceEpoch(), 0, QStringLiteral("ble_identity.uuid_changed"));
    }
    restartAgent();
    if (m_started) {
        start();
    }
}

void DesktopBleIdentityAdapter::start()
{
    m_started = true;
    m_scanTimer->setInterval(std::clamp(m_config.bleScanIntervalMs, 500, 60000));
    QString detail;
    if (!hasUsableConfig(&detail)) {
        m_snapshot.available = false;
        m_snapshot.stale = true;
        logScan(QStringLiteral("disabled"), detail);
        return;
    }
    m_snapshot.available = true;
    m_snapshot.stale = false;
    if (!m_scanTimer->isActive()) {
        m_scanTimer->start();
    }
    startScan();
}

void DesktopBleIdentityAdapter::stop()
{
    m_started = false;
    if (m_scanTimer) {
        m_scanTimer->stop();
    }
    if (m_agent && m_agent->isActive()) {
        m_agent->stop();
    }
    logScan(QStringLiteral("stopped"));
}

std::optional<BleIdentitySnapshot> DesktopBleIdentityAdapter::latestIdentityPresence() const
{
    if (!m_started) {
        return std::nullopt;
    }
    return m_snapshot;
}

QString DesktopBleIdentityAdapter::normalizedBeaconUuid(const QString &uuid)
{
    const QUuid parsed(uuid.trimmed());
    if (parsed.isNull()) {
        return {};
    }
    return parsed.toString(QUuid::WithoutBraces).toLower();
}

bool DesktopBleIdentityAdapter::advertisementMatchesBeacon(const QBluetoothDeviceInfo &info,
                                                           const QString &beaconUuid,
                                                           int rssiThreshold)
{
    const QString target = normalizedBeaconUuid(beaconUuid);
    if (target.isEmpty() || !rssiAllowed(info.rssi(), rssiThreshold)) {
        return false;
    }

    return advertisementContainsBeaconUuid(info, target);
}

void DesktopBleIdentityAdapter::scanTick()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    refreshPresenceFromClock(nowMs);
    startScan();
}

void DesktopBleIdentityAdapter::handleDeviceDiscovered(const QBluetoothDeviceInfo &info)
{
    handleDeviceUpdated(info, QBluetoothDeviceInfo::Field::All);
}

void DesktopBleIdentityAdapter::handleDeviceUpdated(const QBluetoothDeviceInfo &info, QBluetoothDeviceInfo::Fields fields)
{
    Q_UNUSED(fields)

    ++m_scanDevicesSeen;
    const QString target = normalizedBeaconUuid(m_config.bleBeaconUuid);
    if (!advertisementContainsBeaconUuid(info, target)) {
        return;
    }

    ++m_scanUuidMatches;
    if (!rssiAllowed(info.rssi(), m_config.bleRssiThreshold)) {
        ++m_scanRssiRejected;
        return;
    }

    ++m_matchCount;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_lastSeenAtMs = nowMs;
    m_latestRssi = info.rssi();
    publishPresence(true, nowMs, m_latestRssi, QStringLiteral("ble_identity.beacon_seen"));
}

void DesktopBleIdentityAdapter::handleScanFinished()
{
    logScan(QStringLiteral("finished"),
            QStringLiteral("devicesSeen=%1 uuidMatches=%2 rssiRejected=%3 totalMatches=%4 supported=%5")
                .arg(QString::number(m_scanDevicesSeen),
                     QString::number(m_scanUuidMatches),
                     QString::number(m_scanRssiRejected),
                     QString::number(m_matchCount),
                     m_supported ? QStringLiteral("true") : QStringLiteral("false")));
    refreshPresenceFromClock(QDateTime::currentMSecsSinceEpoch());
}

void DesktopBleIdentityAdapter::handleScanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    const bool changed = m_snapshot.available || m_snapshot.present;
    m_snapshot.available = false;
    m_snapshot.present = false;
    m_snapshot.stale = true;
    m_snapshot.observedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_snapshot.source = QStringLiteral("desktop_ble_beacon");
    if (changed) {
        logPresence(m_snapshot, QStringLiteral("ble_identity.scan_error"));
    }
    logScan(QStringLiteral("error"),
            QStringLiteral("code=%1 message=%2").arg(static_cast<int>(error)).arg(m_agent ? m_agent->errorString() : QString{}));
}

bool DesktopBleIdentityAdapter::hasUsableConfig(QString *detail) const
{
    if (m_config.identityMode != QStringLiteral("desktop_ble_beacon")) {
        if (detail) {
            *detail = QStringLiteral("identity mode disabled");
        }
        return false;
    }
    if (normalizedBeaconUuid(m_config.bleBeaconUuid).isEmpty()) {
        if (detail) {
            *detail = QStringLiteral("beacon uuid missing or invalid");
        }
        return false;
    }
    if (!m_supported) {
        if (detail) {
            *detail = QStringLiteral("low energy bluetooth scanning unsupported");
        }
        return false;
    }
    return true;
}

void DesktopBleIdentityAdapter::restartAgent()
{
    if (m_agent) {
        if (m_agent->isActive()) {
            m_agent->stop();
        }
        m_agent->deleteLater();
        m_agent = nullptr;
    }

    m_supported = QBluetoothDeviceDiscoveryAgent::supportedDiscoveryMethods()
        .testFlag(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    m_agent = new QBluetoothDeviceDiscoveryAgent(this);
    m_agent->setLowEnergyDiscoveryTimeout(effectiveScanWindowMs(m_config));
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &DesktopBleIdentityAdapter::handleDeviceDiscovered);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceUpdated,
            this, &DesktopBleIdentityAdapter::handleDeviceUpdated);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &DesktopBleIdentityAdapter::handleScanFinished);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, &DesktopBleIdentityAdapter::handleScanError);
}

void DesktopBleIdentityAdapter::startScan()
{
    if (!m_agent || m_agent->isActive()) {
        return;
    }

    QString detail;
    if (!hasUsableConfig(&detail)) {
        m_snapshot.available = false;
        m_snapshot.stale = true;
        logScan(QStringLiteral("skipped"), detail);
        return;
    }

    logScan(QStringLiteral("started"),
            QStringLiteral("intervalMs=%1 scanWindowMs=%2 rssiThreshold=%3")
                .arg(QString::number(m_config.bleScanIntervalMs),
                     QString::number(effectiveScanWindowMs(m_config)),
                     QString::number(m_config.bleRssiThreshold)));
    m_scanDevicesSeen = 0;
    m_scanUuidMatches = 0;
    m_scanRssiRejected = 0;
    m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void DesktopBleIdentityAdapter::refreshPresenceFromClock(qint64 nowMs)
{
    if (!m_snapshot.available || m_lastSeenAtMs <= 0) {
        return;
    }
    const qint64 timeoutMs = std::max(1, m_config.bleAwayTimeoutMinutes) * kMinuteMs;
    if (nowMs - m_lastSeenAtMs >= timeoutMs) {
        publishPresence(false, nowMs, m_latestRssi, QStringLiteral("ble_identity.missing_timeout"));
    }
}

void DesktopBleIdentityAdapter::publishPresence(bool present, qint64 nowMs, int rssi, const QString &reasonCode)
{
    const bool changed = m_snapshot.present != present || m_snapshot.available == false;
    m_snapshot.available = true;
    m_snapshot.present = present;
    m_snapshot.identityId = normalizedBeaconUuid(m_config.bleBeaconUuid);
    m_snapshot.rssi = rssi;
    m_snapshot.observedAtMs = present ? nowMs : (m_lastSeenAtMs > 0 ? m_lastSeenAtMs : nowMs);
    m_snapshot.source = QStringLiteral("desktop_ble_beacon");
    m_snapshot.stale = false;
    if (changed) {
        logPresence(m_snapshot, reasonCode);
    }
}

void DesktopBleIdentityAdapter::logScan(const QString &event, const QString &detail) const
{
    if (!m_logSink) {
        return;
    }
    m_logSink(QStringLiteral("[ble_identity.scan] event=%1 mode=%2 uuidConfigured=%3 active=%4 detail=%5")
            .arg(event,
                 m_config.identityMode,
                 normalizedBeaconUuid(m_config.bleBeaconUuid).isEmpty() ? QStringLiteral("false") : QStringLiteral("true"),
                 m_agent && m_agent->isActive() ? QStringLiteral("true") : QStringLiteral("false"),
                 detail));
}

void DesktopBleIdentityAdapter::logPresence(const BleIdentitySnapshot &snapshot, const QString &reasonCode) const
{
    if (!m_logSink) {
        return;
    }
    m_logSink(QStringLiteral("[ble_identity.present] present=%1 reason=%2 rssi=%3 source=%4")
            .arg(snapshot.present ? QStringLiteral("true") : QStringLiteral("false"),
                 reasonCode,
                 QString::number(snapshot.rssi),
                 snapshot.source));
}
