#include <QtTest>

#include <QDateTime>

#include "logging/LoggingService.h"
#include "perception/FocusModeExpiryRuntime.h"
#include "settings/AppSettings.h"

class FocusModeExpiryRuntimeTests : public QObject
{
    Q_OBJECT

private slots:
    void expiresTimedFocusModeAndResetsSettingsWithTelemetry();
    void keepsTimedFocusModeWhenNotExpired();
};

void FocusModeExpiryRuntimeTests::expiresTimedFocusModeAndResetsSettingsWithTelemetry()
{
    AppSettings settings;
    settings.setFocusModeEnabled(true);
    settings.setFocusModeAllowCriticalAlerts(true);
    settings.setFocusModeDurationMinutes(30);
    settings.setFocusModeUntilEpochMs(
        QDateTime::currentMSecsSinceEpoch() - 5000);

    LoggingService logging;
    QVERIFY(logging.initialize());
    const int beforeCount = logging.recentBehaviorEvents(64).size();

    const bool expired = FocusModeExpiryRuntime::reconcile(
        &settings,
        &logging,
        QDateTime::currentMSecsSinceEpoch(),
        QStringLiteral("focus_runtime_test"));
    QVERIFY(expired);
    QVERIFY(!settings.focusModeEnabled());
    QCOMPARE(settings.focusModeDurationMinutes(), 0);
    QCOMPARE(settings.focusModeUntilEpochMs(), 0);

    const QVariantList events = logging.recentBehaviorEvents(64);
    QVERIFY(events.size() >= beforeCount + 1);
    bool foundExpiry = false;
    for (const QVariant &entry : events) {
        const QVariantMap row = entry.toMap();
        if (row.value(QStringLiteral("family")).toString() != QStringLiteral("focus_mode")) {
            continue;
        }
        if (row.value(QStringLiteral("reasonCode")).toString() != QStringLiteral("focus_mode.timed_expired")) {
            continue;
        }
        const QVariantMap payload = row.value(QStringLiteral("payload")).toMap();
        if (payload.value(QStringLiteral("source")).toString() != QStringLiteral("focus_runtime_test")) {
            continue;
        }
        QVERIFY(!payload.value(QStringLiteral("enabled")).toBool());
        QCOMPARE(payload.value(QStringLiteral("durationMinutes")).toInt(), 0);
        QCOMPARE(payload.value(QStringLiteral("untilEpochMs")).toLongLong(), 0);
        foundExpiry = true;
        break;
    }
    QVERIFY(foundExpiry);
}

void FocusModeExpiryRuntimeTests::keepsTimedFocusModeWhenNotExpired()
{
    AppSettings settings;
    settings.setFocusModeEnabled(true);
    settings.setFocusModeAllowCriticalAlerts(false);
    settings.setFocusModeDurationMinutes(45);
    settings.setFocusModeUntilEpochMs(
        QDateTime::currentMSecsSinceEpoch() + 120000);

    const bool expired = FocusModeExpiryRuntime::reconcile(
        &settings,
        nullptr,
        QDateTime::currentMSecsSinceEpoch(),
        QStringLiteral("focus_runtime_test"));
    QVERIFY(!expired);
    QVERIFY(settings.focusModeEnabled());
    QCOMPARE(settings.focusModeDurationMinutes(), 45);
    QVERIFY(settings.focusModeUntilEpochMs() > QDateTime::currentMSecsSinceEpoch());
}

QTEST_APPLESS_MAIN(FocusModeExpiryRuntimeTests)
#include "FocusModeExpiryRuntimeTests.moc"
