#pragma once

#include <QList>
#include <QMutex>
#include <QString>

struct CrashBreadcrumb
{
    QString timestampUtc;
    QString module;
    QString event;
    QString detail;
    QString traceId;
    QString sessionId;
    QString threadId;
    qint64 uptimeMs = 0;
};

class CrashBreadcrumbTrail
{
public:
    static CrashBreadcrumbTrail &instance();

    void setCapacity(int capacity);
    int capacity() const;

    void add(const CrashBreadcrumb &crumb);
    QList<CrashBreadcrumb> snapshot(int maxEntries = 0) const;
    void clear();

private:
    CrashBreadcrumbTrail() = default;

    mutable QMutex m_mutex;
    QList<CrashBreadcrumb> m_items;
    int m_capacity = 200;
};
