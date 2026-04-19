#include "CrashBreadcrumbTrail.h"

#include <algorithm>

#include <QMutexLocker>

CrashBreadcrumbTrail &CrashBreadcrumbTrail::instance()
{
    static CrashBreadcrumbTrail trail;
    return trail;
}

void CrashBreadcrumbTrail::setCapacity(int capacity)
{
    if (capacity < 1) {
        capacity = 1;
    }

    QMutexLocker locker(&m_mutex);
    m_capacity = capacity;
    while (m_items.size() > m_capacity) {
        m_items.removeFirst();
    }
}

int CrashBreadcrumbTrail::capacity() const
{
    QMutexLocker locker(&m_mutex);
    return m_capacity;
}

void CrashBreadcrumbTrail::add(const CrashBreadcrumb &crumb)
{
    QMutexLocker locker(&m_mutex);
    if (m_items.size() >= m_capacity) {
        m_items.removeFirst();
    }
    m_items.push_back(crumb);
}

QList<CrashBreadcrumb> CrashBreadcrumbTrail::snapshot(int maxEntries) const
{
    QMutexLocker locker(&m_mutex);
    if (maxEntries <= 0 || maxEntries >= m_items.size()) {
        return m_items;
    }

    const qsizetype startIndex = m_items.size() - static_cast<qsizetype>(maxEntries);
    return m_items.mid(startIndex);
}

void CrashBreadcrumbTrail::clear()
{
    QMutexLocker locker(&m_mutex);
    m_items.clear();
}
