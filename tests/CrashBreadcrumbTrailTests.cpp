#include <QtTest>

#include "diagnostics/CrashBreadcrumbTrail.h"

class CrashBreadcrumbTrailTests : public QObject
{
    Q_OBJECT

private slots:
    void retainsMostRecentEntriesWithinCapacity();
    void snapshotReturnsTailWhenLimited();
};

void CrashBreadcrumbTrailTests::retainsMostRecentEntriesWithinCapacity()
{
    CrashBreadcrumbTrail &trail = CrashBreadcrumbTrail::instance();
    trail.clear();
    trail.setCapacity(3);

    for (int i = 0; i < 5; ++i) {
        CrashBreadcrumb crumb;
        crumb.timestampUtc = QStringLiteral("2026-01-01T00:00:00.%1Z").arg(i);
        crumb.module = QStringLiteral("test");
        crumb.event = QStringLiteral("event_%1").arg(i);
        trail.add(crumb);
    }

    const QList<CrashBreadcrumb> snapshot = trail.snapshot();
    QCOMPARE(snapshot.size(), 3);
    QCOMPARE(snapshot.first().event, QStringLiteral("event_2"));
    QCOMPARE(snapshot.last().event, QStringLiteral("event_4"));
}

void CrashBreadcrumbTrailTests::snapshotReturnsTailWhenLimited()
{
    CrashBreadcrumbTrail &trail = CrashBreadcrumbTrail::instance();
    trail.clear();
    trail.setCapacity(8);

    for (int i = 0; i < 6; ++i) {
        CrashBreadcrumb crumb;
        crumb.timestampUtc = QStringLiteral("2026-01-01T00:00:00.%1Z").arg(i);
        crumb.module = QStringLiteral("test");
        crumb.event = QStringLiteral("tail_%1").arg(i);
        trail.add(crumb);
    }

    const QList<CrashBreadcrumb> tail = trail.snapshot(2);
    QCOMPARE(tail.size(), 2);
    QCOMPARE(tail.at(0).event, QStringLiteral("tail_4"));
    QCOMPARE(tail.at(1).event, QStringLiteral("tail_5"));
}

QTEST_APPLESS_MAIN(CrashBreadcrumbTrailTests)
#include "CrashBreadcrumbTrailTests.moc"
