#pragma once

#include <QList>
#include <QMutex>
#include <QString>

#include "companion/contracts/BehaviorTraceEvent.h"

class BehavioralEventLedger
{
public:
    explicit BehavioralEventLedger(QString rootPath = QString());

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool recordEvent(const BehaviorTraceEvent &event) const;
    [[nodiscard]] QList<BehaviorTraceEvent> recentEvents(int limit = 50) const;
    [[nodiscard]] QString databasePath() const;
    [[nodiscard]] QString ndjsonPath() const;

private:
    [[nodiscard]] bool ensureRootPathExists() const;
    [[nodiscard]] bool appendNdjsonLocked(const BehaviorTraceEvent &event) const;
    [[nodiscard]] bool recordSqliteLocked(const BehaviorTraceEvent &event) const;
    [[nodiscard]] QList<BehaviorTraceEvent> recentEventsSqliteLocked(int limit) const;
    [[nodiscard]] QString defaultRootPath() const;
    [[nodiscard]] static QString payloadToJson(const QVariantMap &payload);

    QString m_rootPath;
    QString m_databasePath;
    QString m_ndjsonPath;
    mutable QMutex m_mutex;
};
