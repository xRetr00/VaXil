#include "settings/AppSettings.h"

#include <QtTest/QtTest>

class SmartHomeSettingsTests : public QObject
{
    Q_OBJECT

private slots:
    void smartHomeDefaultsAreSafe();
    void smartHomeSettersClampOperationalRanges();
    void smartHomeTokenEnvVarRejectsSecretValues();
    void smartHomeBleIdentityDefaultsAreSafe();
    void smartHomeBleIdentitySettersNormalizeAndClamp();
    void smartHomeHomeAssistantIdentitySettersNormalizeAndClamp();
    void smartHomeWelcomeTemplateDefaultsAndOverrides();
    void smartHomeWelcomeTogglesDefaultOnAndPersistInSettingsObject();
};

void SmartHomeSettingsTests::smartHomeDefaultsAreSafe()
{
    AppSettings settings;

    QVERIFY(!settings.smartHomeEnabled());
    QCOMPARE(settings.smartHomeProvider(), QStringLiteral("home_assistant"));
    QCOMPARE(settings.smartHomeHomeAssistantTokenEnvVar(), QStringLiteral("VAXIL_HOME_ASSISTANT_TOKEN"));
    QVERIFY(!settings.smartHomeSensorOnlyWelcomeEnabled());
    QCOMPARE(settings.smartHomeWelcomeCooldownMinutes(), 30);
    QCOMPARE(settings.smartHomeRoomAbsenceGraceMinutes(), 6);
}

void SmartHomeSettingsTests::smartHomeSettersClampOperationalRanges()
{
    AppSettings settings;

    settings.setSmartHomePollIntervalMs(50);
    settings.setSmartHomeRequestTimeoutMs(50);
    settings.setSmartHomeWelcomeCooldownMinutes(-1);
    settings.setSmartHomeRoomAbsenceGraceMinutes(100);

    QCOMPARE(settings.smartHomePollIntervalMs(), 1000);
    QCOMPARE(settings.smartHomeRequestTimeoutMs(), 500);
    QCOMPARE(settings.smartHomeWelcomeCooldownMinutes(), 0);
    QCOMPARE(settings.smartHomeRoomAbsenceGraceMinutes(), 30);
}

void SmartHomeSettingsTests::smartHomeTokenEnvVarRejectsSecretValues()
{
    AppSettings settings;

    settings.setSmartHomeHomeAssistantTokenEnvVar(QStringLiteral("not an env var name"));

    QCOMPARE(settings.smartHomeHomeAssistantTokenEnvVar(), QStringLiteral("VAXIL_HOME_ASSISTANT_TOKEN"));
}

void SmartHomeSettingsTests::smartHomeBleIdentityDefaultsAreSafe()
{
    AppSettings settings;

    QCOMPARE(settings.smartHomeIdentityMode(), QStringLiteral("none"));
    QVERIFY(settings.smartHomeBleBeaconUuid().isEmpty());
    QCOMPARE(settings.smartHomeBleMissingTimeoutMinutes(), 10);
    QCOMPARE(settings.smartHomeIdentityMissingTimeoutMinutes(), 10);
    QCOMPARE(settings.smartHomeBleScanIntervalMs(), 1000);
    QCOMPARE(settings.smartHomeBleRssiThreshold(), -127);
}

void SmartHomeSettingsTests::smartHomeBleIdentitySettersNormalizeAndClamp()
{
    AppSettings settings;

    settings.setSmartHomeIdentityMode(QStringLiteral(" DESKTOP_BLE_BEACON "));
    settings.setSmartHomeBleBeaconUuid(QStringLiteral("74278BDA-B644-4520-8F0C-720EAF059935"));
    settings.setSmartHomeBleMissingTimeoutMinutes(0);
    settings.setSmartHomeBleScanIntervalMs(100);
    settings.setSmartHomeBleRssiThreshold(-200);

    QCOMPARE(settings.smartHomeIdentityMode(), QStringLiteral("desktop_ble_beacon"));
    QCOMPARE(settings.smartHomeBleBeaconUuid(), QStringLiteral("74278bda-b644-4520-8f0c-720eaf059935"));
    QCOMPARE(settings.smartHomeBleMissingTimeoutMinutes(), 1);
    QCOMPARE(settings.smartHomeBleScanIntervalMs(), 500);
    QCOMPARE(settings.smartHomeBleRssiThreshold(), -127);

    settings.setSmartHomeBleMissingTimeoutMinutes(2000);
    settings.setSmartHomeBleScanIntervalMs(100000);
    settings.setSmartHomeBleRssiThreshold(5);

    QCOMPARE(settings.smartHomeBleMissingTimeoutMinutes(), 1440);
    QCOMPARE(settings.smartHomeBleScanIntervalMs(), 60000);
    QCOMPARE(settings.smartHomeBleRssiThreshold(), 0);
}

void SmartHomeSettingsTests::smartHomeHomeAssistantIdentitySettersNormalizeAndClamp()
{
    AppSettings settings;

    settings.setSmartHomeIdentityMode(QStringLiteral(" HOME_ASSISTANT_DEVICE_TRACKER "));
    settings.setSmartHomeHomeAssistantIdentityEntityId(QStringLiteral(" device_tracker.my_iphone "));
    settings.setSmartHomeIdentityMissingTimeoutMinutes(0);

    QCOMPARE(settings.smartHomeIdentityMode(), QStringLiteral("home_assistant_device_tracker"));
    QCOMPARE(settings.smartHomeHomeAssistantIdentityEntityId(), QStringLiteral("device_tracker.my_iphone"));
    QCOMPARE(settings.smartHomeIdentityMissingTimeoutMinutes(), 1);

    settings.setSmartHomeIdentityMode(QStringLiteral("espresense"));
    settings.setSmartHomeIdentityMissingTimeoutMinutes(9999);

    QCOMPARE(settings.smartHomeIdentityMode(), QStringLiteral("espresense"));
    QCOMPARE(settings.smartHomeIdentityMissingTimeoutMinutes(), 1440);
}

void SmartHomeSettingsTests::smartHomeWelcomeTemplateDefaultsAndOverrides()
{
    AppSettings settings;

    QCOMPARE(settings.smartHomePersonalWelcomeTemplate(), QStringLiteral("Welcome back, {user_name}."));
    QCOMPARE(settings.smartHomePersonalWelcomeWithAlertTemplate(),
             QStringLiteral("Welcome back, {user_name}. Someone entered your room at {event_time}."));
    QCOMPARE(settings.smartHomeUnknownOccupantMessageTemplate(), QStringLiteral("There appears to be someone in the room."));
    QCOMPARE(settings.smartHomeUnknownOccupantAlertResponseTemplate(), QStringLiteral("Someone was detected in your room at {event_time}."));
    QVERIFY(settings.smartHomePersonalWelcomeEnabled());
    QVERIFY(settings.smartHomeWelcomeEnabled());
    QVERIFY(settings.smartHomeWelcomeCooldownEnabled());
    QVERIFY(settings.smartHomeUnknownOccupantBlocksWelcomeEnabled());
    QVERIFY(settings.smartHomeUnknownOccupantSpokenAlertsEnabled());

    settings.setSmartHomePersonalWelcomeTemplate(QStringLiteral("Hi {user_name}"));
    settings.setSmartHomePersonalWelcomeWithAlertTemplate(QStringLiteral("Hi {user_name}, alert {event_time}"));
    settings.setSmartHomeUnknownOccupantMessageTemplate(QStringLiteral("Someone is here."));
    settings.setSmartHomeUnknownOccupantAlertResponseTemplate(QStringLiteral("Detected {event_time}."));
    settings.setSmartHomePersonalWelcomeEnabled(false);
    settings.setSmartHomeWelcomeEnabled(false);
    settings.setSmartHomeWelcomeCooldownEnabled(false);
    settings.setSmartHomeUnknownOccupantBlocksWelcomeEnabled(false);
    settings.setSmartHomeUnknownOccupantSpokenAlertsEnabled(false);

    QCOMPARE(settings.smartHomePersonalWelcomeTemplate(), QStringLiteral("Hi {user_name}"));
    QCOMPARE(settings.smartHomePersonalWelcomeWithAlertTemplate(), QStringLiteral("Hi {user_name}, alert {event_time}"));
    QCOMPARE(settings.smartHomeUnknownOccupantMessageTemplate(), QStringLiteral("Someone is here."));
    QCOMPARE(settings.smartHomeUnknownOccupantAlertResponseTemplate(), QStringLiteral("Detected {event_time}."));
    QVERIFY(!settings.smartHomePersonalWelcomeEnabled());
    QVERIFY(!settings.smartHomeWelcomeEnabled());
    QVERIFY(!settings.smartHomeWelcomeCooldownEnabled());
    QVERIFY(!settings.smartHomeUnknownOccupantBlocksWelcomeEnabled());
    QVERIFY(!settings.smartHomeUnknownOccupantSpokenAlertsEnabled());
}

void SmartHomeSettingsTests::smartHomeWelcomeTogglesDefaultOnAndPersistInSettingsObject()
{
    AppSettings settings;

    QVERIFY(settings.smartHomeWelcomeEnabled());
    QVERIFY(settings.smartHomeWelcomeCooldownEnabled());
    QVERIFY(settings.smartHomeUnknownOccupantBlocksWelcomeEnabled());

    settings.setSmartHomeWelcomeEnabled(false);
    settings.setSmartHomeWelcomeCooldownEnabled(false);
    settings.setSmartHomeUnknownOccupantBlocksWelcomeEnabled(false);

    QVERIFY(!settings.smartHomeWelcomeEnabled());
    QVERIFY(!settings.smartHomeWelcomeCooldownEnabled());
    QVERIFY(!settings.smartHomeUnknownOccupantBlocksWelcomeEnabled());
}

QTEST_APPLESS_MAIN(SmartHomeSettingsTests)
#include "SmartHomeSettingsTests.moc"
