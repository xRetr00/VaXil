#include "smart_home/SmartRoomBehaviorPolicy.h"
#include "smart_home/SmartRoomStateMachine.h"

#include <QtTest/QtTest>

class SmartRoomStateMachineTests : public QObject
{
    Q_OBJECT

private slots:
    void sensorOnlyOccupancyDoesNotAllowWelcomeByDefault();
    void sensorOnlyWelcomeAllowsAwayOrUnknownToOccupiedWithCooldown();
    void briefSensorOffGapStaysOccupiedDuringGrace();
    void identityPresenceDrivesFutureHomeStates();
    void absentIdentityAndOccupiedSensorTracksUnknownOccupantWithoutWelcome();
    void welcomePolicyCanDisableAllWelcomes();
    void welcomePolicyCanDisableCooldown();
    void welcomePolicyAllowsRepeatedInRoomWhenCooldownDisabled();
    void unknownOccupantBlockCanBeDisabledForNonPersonalMessage();
    void awayToHomeNotRoomThenInRoomAllowsOnePersonalWelcome();
};

void SmartRoomStateMachineTests::sensorOnlyOccupancyDoesNotAllowWelcomeByDefault()
{
    SmartRoomStateMachine machine;
    SmartRoomStateMachineConfig config;
    config.sensorOnlyWelcomeEnabled = false;
    config.roomAbsenceGraceMinutes = 6;

    const qint64 nowMs = 100000;
    SmartPresenceSnapshot presence;
    presence.available = true;
    presence.occupied = true;
    presence.observedAtMs = nowMs;
    presence.source = QStringLiteral("home_assistant");

    const SmartRoomTransition transition = machine.evaluate({presence, std::nullopt, config, nowMs});

    QCOMPARE(transition.previousState, SmartRoomOccupancyState::UNKNOWN);
    QCOMPARE(transition.currentState, SmartRoomOccupancyState::ROOM_OCCUPIED_SENSOR_ONLY);
    QCOMPARE(transition.reasonCode, QStringLiteral("smart_room.sensor_only_occupied"));

    SmartRoomBehaviorPolicy policy;
    const SmartWelcomeDecision decision = policy.evaluateWelcome({
        .transition = transition,
        .sensorOnlyWelcomeEnabled = false,
        .welcomeCooldownMinutes = 30,
        .lastWelcomeAtMs = 0,
        .nowMs = nowMs
    });

    QVERIFY(!decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("welcome.blocked.sensor_only_disabled"));
}

void SmartRoomStateMachineTests::sensorOnlyWelcomeAllowsAwayOrUnknownToOccupiedWithCooldown()
{
    SmartRoomStateMachine machine;
    SmartRoomStateMachineConfig config;
    config.sensorOnlyWelcomeEnabled = true;
    config.roomAbsenceGraceMinutes = 6;

    SmartPresenceSnapshot occupied;
    occupied.available = true;
    occupied.occupied = true;
    occupied.observedAtMs = 200000;

    const SmartRoomTransition transition = machine.evaluate({occupied, std::nullopt, config, 200000});

    SmartRoomBehaviorPolicy policy;
    const SmartWelcomeDecision first = policy.evaluateWelcome({
        .transition = transition,
        .sensorOnlyWelcomeEnabled = true,
        .welcomeCooldownMinutes = 30,
        .lastWelcomeAtMs = 0,
        .nowMs = 200000
    });
    QVERIFY(first.allowed);
    QCOMPARE(first.reasonCode, QStringLiteral("welcome.allowed.sensor_only_test"));

    const SmartWelcomeDecision second = policy.evaluateWelcome({
        .transition = transition,
        .sensorOnlyWelcomeEnabled = true,
        .welcomeCooldownMinutes = 30,
        .lastWelcomeAtMs = 200000,
        .nowMs = 200000 + 5 * 60 * 1000
    });
    QVERIFY(!second.allowed);
    QCOMPARE(second.reasonCode, QStringLiteral("welcome.blocked.cooldown"));
}

void SmartRoomStateMachineTests::briefSensorOffGapStaysOccupiedDuringGrace()
{
    SmartRoomStateMachine machine;
    SmartRoomStateMachineConfig config;
    config.sensorOnlyWelcomeEnabled = true;
    config.roomAbsenceGraceMinutes = 6;

    SmartPresenceSnapshot occupied;
    occupied.available = true;
    occupied.occupied = true;
    occupied.observedAtMs = 300000;
    machine.evaluate({occupied, std::nullopt, config, 300000});

    SmartPresenceSnapshot clear;
    clear.available = true;
    clear.occupied = false;
    clear.observedAtMs = 300000 + 2 * 60 * 1000;
    const SmartRoomTransition withinGrace = machine.evaluate({clear, std::nullopt, config, clear.observedAtMs});

    QCOMPARE(withinGrace.currentState, SmartRoomOccupancyState::ROOM_OCCUPIED_SENSOR_ONLY);
    QCOMPARE(withinGrace.reasonCode, QStringLiteral("smart_room.sensor_absence_grace"));

    clear.observedAtMs = 300000 + 8 * 60 * 1000;
    const SmartRoomTransition afterGrace = machine.evaluate({clear, std::nullopt, config, clear.observedAtMs});

    QCOMPARE(afterGrace.currentState, SmartRoomOccupancyState::UNKNOWN);
    QCOMPARE(afterGrace.reasonCode, QStringLiteral("smart_room.sensor_clear_no_identity"));
}

void SmartRoomStateMachineTests::identityPresenceDrivesFutureHomeStates()
{
    SmartRoomStateMachine machine;
    SmartRoomStateMachineConfig config;
    config.bleAwayTimeoutMinutes = 10;

    SmartPresenceSnapshot occupied;
    occupied.available = true;
    occupied.occupied = true;
    occupied.observedAtMs = 500000;

    BleIdentitySnapshot present;
    present.available = true;
    present.present = true;
    present.observedAtMs = 500000;

    const SmartRoomTransition inRoom = machine.evaluate({occupied, present, config, 500000});
    QCOMPARE(inRoom.currentState, SmartRoomOccupancyState::IN_ROOM);
    QCOMPARE(inRoom.reasonCode, QStringLiteral("smart_room.identity_and_sensor_present"));

    SmartPresenceSnapshot clear;
    clear.available = true;
    clear.occupied = false;
    clear.observedAtMs = 500000 + 60 * 1000;
    const SmartRoomTransition homeNotRoom = machine.evaluate({clear, present, config, clear.observedAtMs});
    QCOMPARE(homeNotRoom.currentState, SmartRoomOccupancyState::HOME_NOT_ROOM);

    BleIdentitySnapshot absent;
    absent.available = true;
    absent.present = false;
    absent.observedAtMs = 500000;
    const SmartRoomTransition away = machine.evaluate({clear, absent, config, 500000 + 11 * 60 * 1000});
    QCOMPARE(away.currentState, SmartRoomOccupancyState::AWAY);
    QCOMPARE(away.reasonCode, QStringLiteral("smart_room.identity_absent_timeout"));
}

void SmartRoomStateMachineTests::absentIdentityAndOccupiedSensorTracksUnknownOccupantWithoutWelcome()
{
    SmartRoomStateMachine machine;
    SmartRoomStateMachineConfig config;
    config.identityMissingTimeoutMinutes = 10;

    SmartPresenceSnapshot occupied;
    occupied.available = true;
    occupied.occupied = true;
    occupied.observedAtMs = 600000;

    BleIdentitySnapshot absent;
    absent.available = true;
    absent.present = false;
    absent.observedAtMs = 600000;
    absent.source = QStringLiteral("home_assistant_device_tracker");

    const SmartRoomTransition transition = machine.evaluate({occupied, absent, config, 600000});
    QCOMPARE(transition.currentState, SmartRoomOccupancyState::UNKNOWN_OCCUPANT_IN_ROOM);
    QCOMPARE(transition.reasonCode, QStringLiteral("smart_room.unknown_occupant_identity_absent"));
    QVERIFY(transition.unknownOccupant);

    SmartRoomBehaviorPolicy policy;
    const SmartWelcomeDecision decision = policy.evaluateWelcome({
        .transition = transition,
        .sensorOnlyWelcomeEnabled = true,
        .welcomeCooldownMinutes = 30,
        .lastWelcomeAtMs = 0,
        .nowMs = 600000
    });
    QVERIFY(!decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("welcome.blocked.unknown_occupant"));
}

void SmartRoomStateMachineTests::welcomePolicyCanDisableAllWelcomes()
{
    SmartRoomTransition transition;
    transition.previousState = SmartRoomOccupancyState::AWAY;
    transition.currentState = SmartRoomOccupancyState::IN_ROOM;
    transition.occurredAtMs = 800000;

    SmartRoomBehaviorPolicy policy;
    const SmartWelcomeDecision decision = policy.evaluateWelcome({
        .transition = transition,
        .welcomeEnabled = false,
        .sensorOnlyWelcomeEnabled = false,
        .welcomeCooldownMinutes = 30,
        .lastWelcomeAtMs = 0,
        .nowMs = transition.occurredAtMs
    });

    QVERIFY(!decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("welcome.blocked.disabled"));
}

void SmartRoomStateMachineTests::welcomePolicyCanDisableCooldown()
{
    SmartRoomTransition transition;
    transition.previousState = SmartRoomOccupancyState::AWAY;
    transition.currentState = SmartRoomOccupancyState::IN_ROOM;
    transition.occurredAtMs = 900000;

    SmartRoomBehaviorPolicy policy;
    const SmartWelcomeDecision decision = policy.evaluateWelcome({
        .transition = transition,
        .welcomeEnabled = true,
        .welcomeCooldownEnabled = false,
        .sensorOnlyWelcomeEnabled = false,
        .welcomeCooldownMinutes = 30,
        .lastWelcomeAtMs = transition.occurredAtMs - 60 * 1000,
        .nowMs = transition.occurredAtMs
    });

    QVERIFY(decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("welcome.allowed.identity_away_to_in_room"));
}

void SmartRoomStateMachineTests::welcomePolicyAllowsRepeatedInRoomWhenCooldownDisabled()
{
    SmartRoomTransition transition;
    transition.previousState = SmartRoomOccupancyState::IN_ROOM;
    transition.currentState = SmartRoomOccupancyState::IN_ROOM;
    transition.occurredAtMs = 925000;

    SmartRoomBehaviorPolicy policy;
    const SmartWelcomeDecision decision = policy.evaluateWelcome({
        .transition = transition,
        .welcomeEnabled = true,
        .welcomeCooldownEnabled = false,
        .sensorOnlyWelcomeEnabled = false,
        .welcomeCooldownMinutes = 1,
        .lastWelcomeAtMs = transition.occurredAtMs - 1000,
        .nowMs = transition.occurredAtMs
    });

    QVERIFY(decision.allowed);
    QVERIFY(decision.personal);
    QCOMPARE(decision.reasonCode, QStringLiteral("welcome.allowed.in_room_cooldown_disabled"));
}

void SmartRoomStateMachineTests::unknownOccupantBlockCanBeDisabledForNonPersonalMessage()
{
    SmartRoomTransition transition;
    transition.previousState = SmartRoomOccupancyState::AWAY;
    transition.currentState = SmartRoomOccupancyState::UNKNOWN_OCCUPANT_IN_ROOM;
    transition.unknownOccupant = true;
    transition.occurredAtMs = 950000;

    SmartRoomBehaviorPolicy policy;
    const SmartWelcomeDecision decision = policy.evaluateWelcome({
        .transition = transition,
        .welcomeEnabled = true,
        .welcomeCooldownEnabled = true,
        .unknownOccupantBlocksWelcomeEnabled = false,
        .sensorOnlyWelcomeEnabled = false,
        .welcomeCooldownMinutes = 30,
        .lastWelcomeAtMs = 0,
        .nowMs = transition.occurredAtMs
    });

    QVERIFY(decision.allowed);
    QVERIFY(decision.unknownOccupant);
    QVERIFY(!decision.personal);
    QCOMPARE(decision.reasonCode, QStringLiteral("welcome.allowed.unknown_occupant_override"));
}

void SmartRoomStateMachineTests::awayToHomeNotRoomThenInRoomAllowsOnePersonalWelcome()
{
    SmartRoomStateMachine machine;
    SmartRoomStateMachineConfig config;
    config.identityMissingTimeoutMinutes = 10;

    SmartPresenceSnapshot clear;
    clear.available = true;
    clear.occupied = false;
    clear.observedAtMs = 700000;

    BleIdentitySnapshot absent;
    absent.available = true;
    absent.present = false;
    absent.observedAtMs = 700000;
    machine.evaluate({clear, absent, config, 700000 + 11 * 60 * 1000});

    BleIdentitySnapshot present;
    present.available = true;
    present.present = true;
    present.observedAtMs = 700000 + 12 * 60 * 1000;
    const SmartRoomTransition home = machine.evaluate({clear, present, config, present.observedAtMs});
    QCOMPARE(home.currentState, SmartRoomOccupancyState::HOME_NOT_ROOM);
    QVERIFY(home.returnedFromAway);

    SmartPresenceSnapshot occupied = clear;
    occupied.occupied = true;
    occupied.observedAtMs = present.observedAtMs + 1000;
    const SmartRoomTransition inRoom = machine.evaluate({occupied, present, config, occupied.observedAtMs});
    QCOMPARE(inRoom.currentState, SmartRoomOccupancyState::IN_ROOM);
    QVERIFY(inRoom.returnedFromAway);

    SmartRoomBehaviorPolicy policy;
    const SmartWelcomeDecision decision = policy.evaluateWelcome({
        .transition = inRoom,
        .sensorOnlyWelcomeEnabled = false,
        .welcomeCooldownMinutes = 30,
        .lastWelcomeAtMs = 0,
        .nowMs = inRoom.occurredAtMs
    });
    QVERIFY(decision.allowed);
    QCOMPARE(decision.reasonCode, QStringLiteral("welcome.allowed.identity_returned_home_then_in_room"));
}

QTEST_APPLESS_MAIN(SmartRoomStateMachineTests)
#include "SmartRoomStateMachineTests.moc"
