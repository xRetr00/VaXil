#include "vision/WorldStateCache.h"

#include <algorithm>
#include <limits>

#include <QDateTime>
#include <QSet>
#include <QStringList>
#include <QWriteLocker>
#include <QReadLocker>

namespace {
QString deriveSummaryFromSnapshot(const VisionSnapshot &snapshot)
{
    if (!snapshot.summary.trimmed().isEmpty()) {
        return snapshot.summary.trimmed();
    }

    QStringList parts;
    if (!snapshot.objects.isEmpty()) {
        QStringList objectNames;
        for (const auto &object : snapshot.objects) {
            if (!object.className.trimmed().isEmpty()) {
                objectNames.push_back(object.className.trimmed());
            }
        }
        objectNames.removeDuplicates();
        if (!objectNames.isEmpty()) {
            parts.push_back(QStringLiteral("objects: %1").arg(objectNames.join(QStringLiteral(", "))));
        }
    }

    if (!snapshot.gestures.isEmpty()) {
        QStringList gestureNames;
        for (const auto &gesture : snapshot.gestures) {
            if (!gesture.name.trimmed().isEmpty()) {
                gestureNames.push_back(gesture.name.trimmed());
            }
        }
        gestureNames.removeDuplicates();
        if (!gestureNames.isEmpty()) {
            parts.push_back(QStringLiteral("gestures: %1").arg(gestureNames.join(QStringLiteral(", "))));
        }
    }

    if (snapshot.fingerCount >= 0) {
        parts.push_back(QStringLiteral("finger count: %1").arg(snapshot.fingerCount));
    }

    return parts.join(QStringLiteral("; "));
}
}

WorldStateCache::WorldStateCache(int historyWindowMs, int maxSnapshotAgeMs, QObject *parent)
    : QObject(parent)
    , m_historyWindowMs(std::max(1000, historyWindowMs))
    , m_maxSnapshotAgeMs(std::max(100, maxSnapshotAgeMs))
{
}

void WorldStateCache::setHistoryWindowMs(int historyWindowMs)
{
    QWriteLocker locker(&m_lock);
    m_historyWindowMs = std::max(1000, historyWindowMs);
    trimLocked(QDateTime::currentMSecsSinceEpoch());
}

void WorldStateCache::setMaxSnapshotAgeMs(int maxSnapshotAgeMs)
{
    QWriteLocker locker(&m_lock);
    m_maxSnapshotAgeMs = std::max(100, maxSnapshotAgeMs);
    trimLocked(QDateTime::currentMSecsSinceEpoch());
}

bool WorldStateCache::ingestSnapshot(const VisionSnapshot &snapshot)
{
    VisionSnapshot stored = snapshot;
    if (!stored.timestamp.isValid()) {
        return false;
    }
    stored.timestamp = stored.timestamp.toUTC();
    stored.summary = deriveSummaryFromSnapshot(stored);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (snapshotAgeMs(stored, nowMs) > std::max(100, m_maxSnapshotAgeMs)) {
        return false;
    }

    QWriteLocker locker(&m_lock);
    m_snapshots.push_back(stored);
    trimLocked(nowMs);
    return true;
}

std::optional<VisionSnapshot> WorldStateCache::latestSnapshot() const
{
    QReadLocker locker(&m_lock);
    if (m_snapshots.isEmpty()) {
        return std::nullopt;
    }
    return m_snapshots.constLast();
}

std::optional<VisionSnapshot> WorldStateCache::latestFreshSnapshot(int maxAgeMs) const
{
    const auto latest = latestSnapshot();
    if (!latest.has_value()) {
        return std::nullopt;
    }
    if (snapshotAgeMs(*latest, QDateTime::currentMSecsSinceEpoch()) > std::max(100, maxAgeMs)) {
        return std::nullopt;
    }
    return latest;
}

bool WorldStateCache::isFresh(int maxAgeMs) const
{
    return latestFreshSnapshot(maxAgeMs).has_value();
}

bool WorldStateCache::hasFreshSnapshot(int staleThresholdMs) const
{
    return isFresh(staleThresholdMs);
}

QString WorldStateCache::filteredSummary(int staleThresholdMs) const
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 maxAgeMs = std::max(100, staleThresholdMs);

    QReadLocker locker(&m_lock);
    if (m_snapshots.isEmpty()) {
        return {};
    }

    const VisionSnapshot &latest = m_snapshots.constLast();
    if (snapshotAgeMs(latest, nowMs) <= maxAgeMs && !latest.summary.trimmed().isEmpty()) {
        return latest.summary.trimmed();
    }

    QSet<QString> objectNames;
    QSet<QString> gestureNames;
    int latestFingerCount = -1;
    for (auto it = m_snapshots.crbegin(); it != m_snapshots.crend(); ++it) {
        if (snapshotAgeMs(*it, nowMs) > maxAgeMs) {
            continue;
        }
        for (const auto &object : it->objects) {
            if (!object.className.trimmed().isEmpty()) {
                objectNames.insert(object.className.trimmed());
            }
        }
        for (const auto &gesture : it->gestures) {
            if (!gesture.name.trimmed().isEmpty()) {
                gestureNames.insert(gesture.name.trimmed());
            }
        }
        if (latestFingerCount < 0 && it->fingerCount >= 0) {
            latestFingerCount = it->fingerCount;
        }
        if (objectNames.size() >= 4 && gestureNames.size() >= 3) {
            break;
        }
    }

    QStringList parts;
    if (!objectNames.isEmpty()) {
        QStringList objectList = objectNames.values();
        std::sort(objectList.begin(), objectList.end());
        parts.push_back(QStringLiteral("Visible objects: %1").arg(objectList.join(QStringLiteral(", "))));
    }
    if (!gestureNames.isEmpty()) {
        QStringList gestureList = gestureNames.values();
        std::sort(gestureList.begin(), gestureList.end());
        parts.push_back(QStringLiteral("Recent gestures: %1").arg(gestureList.join(QStringLiteral(", "))));
    }
    if (latestFingerCount >= 0) {
        parts.push_back(QStringLiteral("Finger count: %1").arg(latestFingerCount));
    }
    return parts.join(QStringLiteral(". "));
}

int WorldStateCache::size() const
{
    QReadLocker locker(&m_lock);
    return m_snapshots.size();
}

void WorldStateCache::trimLocked(qint64 nowMs)
{
    const qint64 cutoffMs = nowMs - std::max(1000, m_historyWindowMs);
    while (!m_snapshots.isEmpty() && m_snapshots.first().timestamp.toUTC().toMSecsSinceEpoch() < cutoffMs) {
        m_snapshots.removeFirst();
    }
}

qint64 WorldStateCache::snapshotAgeMs(const VisionSnapshot &snapshot, qint64 nowMs)
{
    if (!snapshot.timestamp.isValid()) {
        return std::numeric_limits<qint64>::max();
    }
    return std::max<qint64>(0, nowMs - snapshot.timestamp.toUTC().toMSecsSinceEpoch());
}
