#include <QtTest>

#include "vision/GestureActionRouter.h"
#include "vision/VisionContextGate.h"
#include "vision/WorldStateCache.h"

class VisionStateTests : public QObject
{
    Q_OBJECT

private slots:
    void worldStateCacheTracksFreshLatestSnapshot();
    void worldStateCacheDropsStaleSummary();
    void worldStateCacheRejectsStaleSnapshotOnIngest();
    void visionContextGateMatchesRelevantQueries();
    void visionContextGateBlocksIrrelevantQueries();
    void gestureActionRouterTriggersOnlyOnStart();
    void gestureActionRouterRespectsCooldown();
};

void VisionStateTests::worldStateCacheTracksFreshLatestSnapshot()
{
    WorldStateCache cache(15000);
    VisionSnapshot snapshot;
    snapshot.timestamp = QDateTime::currentDateTimeUtc();
    snapshot.nodeId = QStringLiteral("laptop-node");
    snapshot.summary = QStringLiteral("User holding a can");
    snapshot.objects = {VisionObjectDetection{.className = QStringLiteral("can"), .confidence = 0.91}};

    cache.ingestSnapshot(snapshot);

    QVERIFY(cache.isFresh(2000));
    QVERIFY(cache.hasFreshSnapshot(2000));
    QCOMPARE(cache.filteredSummary(2000), QStringLiteral("User holding a can"));
}

void VisionStateTests::worldStateCacheDropsStaleSummary()
{
    WorldStateCache cache(15000);
    VisionSnapshot snapshot;
    snapshot.timestamp = QDateTime::currentDateTimeUtc().addMSecs(-5000);
    snapshot.nodeId = QStringLiteral("laptop-node");
    snapshot.summary = QStringLiteral("Old frame");
    snapshot.objects = {VisionObjectDetection{.className = QStringLiteral("cup"), .confidence = 0.75}};

    QVERIFY(!cache.ingestSnapshot(snapshot));

    QVERIFY(!cache.hasFreshSnapshot(1000));
    QVERIFY(cache.filteredSummary(1000).isEmpty());
}

void VisionStateTests::worldStateCacheRejectsStaleSnapshotOnIngest()
{
    WorldStateCache cache(15000);
    VisionSnapshot snapshot;
    snapshot.timestamp = QDateTime::currentDateTimeUtc().addMSecs(-3000);
    snapshot.nodeId = QStringLiteral("laptop-node");
    snapshot.summary = QStringLiteral("Too old");

    QVERIFY(!cache.ingestSnapshot(snapshot));
    QVERIFY(!cache.isFresh(2000));
    QCOMPARE(cache.size(), 0);
}

void VisionStateTests::visionContextGateMatchesRelevantQueries()
{
    QVERIFY(VisionContextGate::shouldInject(QStringLiteral("What am I holding?"),
                                            IntentType::GENERAL_CHAT,
                                            true,
                                            false,
                                            false));
}

void VisionStateTests::visionContextGateBlocksIrrelevantQueries()
{
    QVERIFY(!VisionContextGate::shouldInject(QStringLiteral("Tell me a joke"),
                                             IntentType::GENERAL_CHAT,
                                             true,
                                             false,
                                             false));
}

void VisionStateTests::gestureActionRouterTriggersOnlyOnStart()
{
    GestureActionRouter router(nullptr);
    router.configure(true, 500);

    QSignalSpy stopSpy(&router, &GestureActionRouter::stopSpeakingRequested);
    QSignalSpy cancelSpy(&router, &GestureActionRouter::cancelCurrentRequestRequested);

    router.routeGesture(QStringLiteral("cancel"), QStringLiteral("open_hand"), 0.95, 1000, QStringLiteral("trace-a"));
    router.routeGesture(QStringLiteral("cancel"), QStringLiteral("open_hand"), 0.95, 1050, QStringLiteral("trace-a"));

    QCOMPARE(stopSpy.count(), 1);
    QCOMPARE(cancelSpy.count(), 1);
}

void VisionStateTests::gestureActionRouterRespectsCooldown()
{
    GestureActionRouter router(nullptr);
    router.configure(true, 500);

    QSignalSpy stopSpy(&router, &GestureActionRouter::stopSpeakingRequested);
    QSignalSpy cancelSpy(&router, &GestureActionRouter::cancelCurrentRequestRequested);

    router.routeGesture(QStringLiteral("cancel"), QStringLiteral("open_hand"), 0.90, 1000, QStringLiteral("trace-a"));
    QTest::qWait(260);
    router.routeGesture(QStringLiteral("cancel"), QStringLiteral("open_hand"), 0.90, 1200, QStringLiteral("trace-b"));
    router.routeGesture(QStringLiteral("cancel"), QStringLiteral("open_hand"), 0.90, 1700, QStringLiteral("trace-c"));

    QCOMPARE(stopSpy.count(), 2);
    QCOMPARE(cancelSpy.count(), 2);
}

QTEST_APPLESS_MAIN(VisionStateTests)
#include "VisionStateTests.moc"
