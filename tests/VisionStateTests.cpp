#include <QtTest>

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

QTEST_APPLESS_MAIN(VisionStateTests)
#include "VisionStateTests.moc"
