#pragma once

#include <optional>

#include <QObject>
#include <QReadWriteLock>
#include <QVector>

#include "core/AssistantTypes.h"

class WorldStateCache final : public QObject
{
    Q_OBJECT

public:
    explicit WorldStateCache(int historyWindowMs = 15000, int maxSnapshotAgeMs = 2000, QObject *parent = nullptr);

    void setHistoryWindowMs(int historyWindowMs);
    void setMaxSnapshotAgeMs(int maxSnapshotAgeMs);
    bool ingestSnapshot(const VisionSnapshot &snapshot);

    std::optional<VisionSnapshot> latestSnapshot() const;
    std::optional<VisionSnapshot> latestFreshSnapshot(int maxAgeMs = 2000) const;
    bool isFresh(int maxAgeMs = 2000) const;
    bool hasFreshSnapshot(int staleThresholdMs) const;
    QString filteredSummary(int staleThresholdMs) const;
    int size() const;

private:
    void trimLocked(qint64 nowMs);
    static qint64 snapshotAgeMs(const VisionSnapshot &snapshot, qint64 nowMs);

    mutable QReadWriteLock m_lock;
    QVector<VisionSnapshot> m_snapshots;
    int m_historyWindowMs = 15000;
    int m_maxSnapshotAgeMs = 2000;
};
