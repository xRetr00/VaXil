#include <QtTest>

#include "vision/GestureActionRouter.h"
#include "vision/GestureStateMachine.h"
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
    void gestureStateMachineEmitsStartHoldAndEnd();
    void gestureStateMachineRespectsCooldownLock();
    void gestureActionRouterTriggersOnlyOnStart();
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

void VisionStateTests::gestureStateMachineEmitsStartHoldAndEnd()
{
    qRegisterMetaType<GestureEvent>("GestureEvent");
    GestureStateMachine machine(nullptr);
    machine.configure(true, 0.70, 180, 600);

    QSignalSpy eventSpy(&machine, &GestureStateMachine::gestureEventReady);
    QVERIFY(eventSpy.isValid());

    const QList<GestureObservation> observations = {
        GestureObservation{.actionName = QStringLiteral("cancel"),
                           .sourceGesture = QStringLiteral("open_hand"),
                           .confidence = 0.95}
    };

    machine.ingestObservations(observations, 1000, QStringLiteral("trace-a"));
    machine.ingestObservations(observations, 1100, QStringLiteral("trace-a"));
    QCOMPARE(eventSpy.count(), 0);

    machine.ingestObservations(observations, 1200, QStringLiteral("trace-a"));
    QCOMPARE(eventSpy.count(), 1);
    const GestureEvent startEvent = qvariant_cast<GestureEvent>(eventSpy.at(0).at(0));
    QCOMPARE(startEvent.type, GestureEventType::Start);
    QCOMPARE(startEvent.lifecycleState, GestureLifecycleState::Active);
    QCOMPARE(startEvent.stableFrameCount, 3);

    machine.ingestObservations(observations, 1280, QStringLiteral("trace-a"));
    QCOMPARE(eventSpy.count(), 2);
    const GestureEvent holdEvent = qvariant_cast<GestureEvent>(eventSpy.at(1).at(0));
    QCOMPARE(holdEvent.type, GestureEventType::Hold);

    machine.ingestObservations({}, 1450, QStringLiteral("trace-a"));
    QCOMPARE(eventSpy.count(), 3);
    const GestureEvent endEvent = qvariant_cast<GestureEvent>(eventSpy.at(2).at(0));
    QCOMPARE(endEvent.type, GestureEventType::End);
}

void VisionStateTests::gestureStateMachineRespectsCooldownLock()
{
    qRegisterMetaType<GestureEvent>("GestureEvent");
    GestureStateMachine machine(nullptr);
    machine.configure(true, 0.70, 180, 600);

    QSignalSpy eventSpy(&machine, &GestureStateMachine::gestureEventReady);
    QVERIFY(eventSpy.isValid());

    const QList<GestureObservation> observations = {
        GestureObservation{.actionName = QStringLiteral("cancel"),
                           .sourceGesture = QStringLiteral("open_hand"),
                           .confidence = 0.90}
    };

    machine.ingestObservations(observations, 1000, QStringLiteral("trace-a"));
    machine.ingestObservations(observations, 1100, QStringLiteral("trace-a"));
    machine.ingestObservations(observations, 1200, QStringLiteral("trace-a"));
    QCOMPARE(eventSpy.count(), 1);

    machine.ingestObservations({}, 1350, QStringLiteral("trace-a"));
    QCOMPARE(eventSpy.count(), 2);

    machine.ingestObservations(observations, 1500, QStringLiteral("trace-b"));
    machine.ingestObservations(observations, 1600, QStringLiteral("trace-b"));
    machine.ingestObservations(observations, 1700, QStringLiteral("trace-b"));
    QCOMPARE(eventSpy.count(), 2);

    machine.ingestObservations({}, 1810, QStringLiteral("trace-b"));
    machine.ingestObservations(observations, 2050, QStringLiteral("trace-c"));
    machine.ingestObservations(observations, 2170, QStringLiteral("trace-c"));
    machine.ingestObservations(observations, 2290, QStringLiteral("trace-c"));
    QCOMPARE(eventSpy.count(), 3);
    const GestureEvent restarted = qvariant_cast<GestureEvent>(eventSpy.at(2).at(0));
    QCOMPARE(restarted.type, GestureEventType::Start);
}

void VisionStateTests::gestureActionRouterTriggersOnlyOnStart()
{
    GestureActionRouter router(nullptr);
    router.configure(true);

    QSignalSpy stopSpy(&router, &GestureActionRouter::stopSpeakingRequested);
    QSignalSpy cancelSpy(&router, &GestureActionRouter::cancelCurrentRequestRequested);
    QVERIFY(stopSpy.isValid());
    QVERIFY(cancelSpy.isValid());

    router.routeGestureEvent({
        .type = GestureEventType::Hold,
        .lifecycleState = GestureLifecycleState::Active,
        .actionName = QStringLiteral("cancel"),
        .sourceGesture = QStringLiteral("open_hand"),
        .confidence = 0.95,
        .timestampMs = 1000,
        .stableForMs = 200,
        .stableFrameCount = 3,
        .traceId = QStringLiteral("trace-a")
    });
    router.routeGestureEvent({
        .type = GestureEventType::Start,
        .lifecycleState = GestureLifecycleState::Active,
        .actionName = QStringLiteral("cancel"),
        .sourceGesture = QStringLiteral("open_hand"),
        .confidence = 0.95,
        .timestampMs = 1100,
        .stableForMs = 300,
        .stableFrameCount = 4,
        .traceId = QStringLiteral("trace-a")
    });
    router.routeGestureEvent({
        .type = GestureEventType::End,
        .lifecycleState = GestureLifecycleState::Active,
        .actionName = QStringLiteral("cancel"),
        .sourceGesture = QStringLiteral("open_hand"),
        .confidence = 0.95,
        .timestampMs = 1300,
        .stableForMs = 300,
        .stableFrameCount = 4,
        .traceId = QStringLiteral("trace-a")
    });

    QCOMPARE(stopSpy.count(), 1);
    QCOMPARE(cancelSpy.count(), 1);
}

QTEST_APPLESS_MAIN(VisionStateTests)
#include "VisionStateTests.moc"
