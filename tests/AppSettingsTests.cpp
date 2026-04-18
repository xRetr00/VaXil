#include <QtTest>
#include <QSignalSpy>

#include "settings/AppSettings.h"

class AppSettingsTests : public QObject
{
    Q_OBJECT

private slots:
    void hasExpectedDefaults();
    void clampVoiceSpeedBelowMin();
    void clampVoiceSpeedAboveMax();
    void clampVoicePitchBelowMin();
    void clampVoicePitchAboveMax();
    void clampVadSensitivityBelowMin();
    void clampVadSensitivityAboveMax();
    void clampConversationTemperatureBelowMin();
    void clampConversationTemperatureAboveMax();
    void clampToolUseTemperatureBelowMin();
    void clampToolUseTemperatureAboveMax();
    void clampMaxOutputTokensBelowMin();
    void clampMaxOutputTokensAboveMax();
    void clampWakeTriggerThresholdBelowMin();
    void clampWakeTriggerThresholdAboveMax();
    void voiceSpeedPreservesValidValue();
    void conversationTemperaturePreservesValidValue();
    void maxOutputTokensPreservesValidValue();
    void visionEndpointDefaultsWhenEmpty();
    void agentProviderModeDefaultsWhenEmpty();
    void webSearchProviderDefaultsWhenEmpty();
    void wakeWordPhraseDefaultsWhenEmpty();
    void wakeEngineKindDefaultsWhenEmpty();
    void ttsEngineKindDefaultsWhenEmpty();
    void focusModeDefaultsDisabled();
    void focusModeDurationClamps();
    void privateModeDefaultsDisabled();
    void permissionOverridesSanitized();
    void settingsChangedEmittedOnSetter();
};

void AppSettingsTests::hasExpectedDefaults()
{
    AppSettings settings;
    QCOMPARE(settings.chatBackendKind(), QStringLiteral("openai_compatible_local"));
    QCOMPARE(settings.defaultReasoningMode(), ReasoningMode::Balanced);
    QVERIFY(settings.autoRoutingEnabled());
    QVERIFY(settings.streamingEnabled());
    QCOMPARE(settings.requestTimeoutMs(), 12000);
    QVERIFY(settings.agentEnabled());
    QCOMPARE(settings.agentProviderMode(), QStringLiteral("auto"));
    QCOMPARE(settings.maxOutputTokens(), 1024);
    QVERIFY(settings.memoryAutoWrite());
    QCOMPARE(settings.webSearchProvider(), QStringLiteral("brave"));
    QVERIFY(!settings.visionEnabled());
    QVERIFY(!settings.gestureEnabled());
    QCOMPARE(settings.wakeWordPhrase(), QStringLiteral("Hey Vaxil"));
    QVERIFY(settings.wakeWordEnabled());
    QCOMPARE(settings.wakeWordSensitivity(), 0.80);
    QCOMPARE(settings.wakeEngineKind(), QStringLiteral("sherpa-onnx"));
    QCOMPARE(settings.ttsEngineKind(), QStringLiteral("piper"));
    QVERIFY(settings.aecEnabled());
    QVERIFY(!settings.rnnoiseEnabled());
    QVERIFY(settings.tracePanelEnabled());
    QVERIFY(!settings.focusModeEnabled());
    QVERIFY(settings.focusModeAllowCriticalAlerts());
    QCOMPARE(settings.focusModeDurationMinutes(), 0);
    QCOMPARE(settings.focusModeUntilEpochMs(), 0);
    QVERIFY(!settings.privateModeEnabled());
    QVERIFY(!settings.initialSetupCompleted());
    QVERIFY(!settings.learningDataCollectionEnabled());
    QVERIFY(!settings.learningAudioCollectionEnabled());
    QVERIFY(!settings.learningTranscriptCollectionEnabled());
    QVERIFY(!settings.learningToolLoggingEnabled());
    QVERIFY(!settings.learningBehaviorLoggingEnabled());
    QVERIFY(!settings.learningMemoryLoggingEnabled());
    QCOMPARE(settings.learningMaxAudioStorageGb(), 4.0);
    QCOMPARE(settings.learningMaxDaysToKeepAudio(), 30);
    QCOMPARE(settings.learningMaxDaysToKeepStructuredLogs(), 90);
    QVERIFY(!settings.learningAllowPreparedDatasetExport());
}

void AppSettingsTests::clampVoiceSpeedBelowMin()
{
    AppSettings settings;
    settings.setVoiceSpeed(0.0);
    QCOMPARE(settings.voiceSpeed(), 0.85);
}

void AppSettingsTests::clampVoiceSpeedAboveMax()
{
    AppSettings settings;
    settings.setVoiceSpeed(9.9);
    QCOMPARE(settings.voiceSpeed(), 0.92);
}

void AppSettingsTests::clampVoicePitchBelowMin()
{
    AppSettings settings;
    settings.setVoicePitch(0.0);
    QCOMPARE(settings.voicePitch(), 0.90);
}

void AppSettingsTests::clampVoicePitchAboveMax()
{
    AppSettings settings;
    settings.setVoicePitch(9.9);
    QCOMPARE(settings.voicePitch(), 0.97);
}

void AppSettingsTests::clampVadSensitivityBelowMin()
{
    AppSettings settings;
    settings.setVadSensitivity(0.0);
    QCOMPARE(settings.vadSensitivity(), 0.05);
}

void AppSettingsTests::clampVadSensitivityAboveMax()
{
    AppSettings settings;
    settings.setVadSensitivity(1.5);
    QCOMPARE(settings.vadSensitivity(), 0.95);
}

void AppSettingsTests::clampConversationTemperatureBelowMin()
{
    AppSettings settings;
    settings.setConversationTemperature(-1.0);
    QCOMPARE(settings.conversationTemperature(), 0.0);
}

void AppSettingsTests::clampConversationTemperatureAboveMax()
{
    AppSettings settings;
    settings.setConversationTemperature(5.0);
    QCOMPARE(settings.conversationTemperature(), 2.0);
}

void AppSettingsTests::clampToolUseTemperatureBelowMin()
{
    AppSettings settings;
    settings.setToolUseTemperature(-0.5);
    QCOMPARE(settings.toolUseTemperature(), 0.0);
}

void AppSettingsTests::clampToolUseTemperatureAboveMax()
{
    AppSettings settings;
    settings.setToolUseTemperature(3.0);
    QCOMPARE(settings.toolUseTemperature(), 2.0);
}

void AppSettingsTests::clampMaxOutputTokensBelowMin()
{
    AppSettings settings;
    settings.setMaxOutputTokens(1);
    QCOMPARE(settings.maxOutputTokens(), 64);
}

void AppSettingsTests::clampMaxOutputTokensAboveMax()
{
    AppSettings settings;
    settings.setMaxOutputTokens(99999);
    QCOMPARE(settings.maxOutputTokens(), 8192);
}

void AppSettingsTests::clampWakeTriggerThresholdBelowMin()
{
    AppSettings settings;
    settings.setWakeTriggerThreshold(0.0);
    QCOMPARE(settings.wakeTriggerThreshold(), 0.50);
}

void AppSettingsTests::clampWakeTriggerThresholdAboveMax()
{
    AppSettings settings;
    settings.setWakeTriggerThreshold(1.5);
    QCOMPARE(settings.wakeTriggerThreshold(), 1.0);
}

void AppSettingsTests::voiceSpeedPreservesValidValue()
{
    AppSettings settings;
    settings.setVoiceSpeed(0.88);
    QCOMPARE(settings.voiceSpeed(), 0.88);
}

void AppSettingsTests::conversationTemperaturePreservesValidValue()
{
    AppSettings settings;
    settings.setConversationTemperature(1.0);
    QCOMPARE(settings.conversationTemperature(), 1.0);
}

void AppSettingsTests::maxOutputTokensPreservesValidValue()
{
    AppSettings settings;
    settings.setMaxOutputTokens(2048);
    QCOMPARE(settings.maxOutputTokens(), 2048);
}

void AppSettingsTests::visionEndpointDefaultsWhenEmpty()
{
    AppSettings settings;
    settings.setVisionEndpoint(QStringLiteral(""));
    QCOMPARE(settings.visionEndpoint(), QStringLiteral("ws://0.0.0.0:8765/vision"));
}

void AppSettingsTests::agentProviderModeDefaultsWhenEmpty()
{
    AppSettings settings;
    settings.setAgentProviderMode(QStringLiteral(""));
    QCOMPARE(settings.agentProviderMode(), QStringLiteral("auto"));
}

void AppSettingsTests::webSearchProviderDefaultsWhenEmpty()
{
    AppSettings settings;
    settings.setWebSearchProvider(QStringLiteral(""));
    QCOMPARE(settings.webSearchProvider(), QStringLiteral("brave"));
}

void AppSettingsTests::wakeWordPhraseDefaultsWhenEmpty()
{
    AppSettings settings;
    settings.setWakeWordPhrase(QStringLiteral(""));
    QCOMPARE(settings.wakeWordPhrase(), QStringLiteral("Hey Vaxil"));
}

void AppSettingsTests::wakeEngineKindDefaultsWhenEmpty()
{
    AppSettings settings;
    settings.setWakeEngineKind(QStringLiteral(""));
    QCOMPARE(settings.wakeEngineKind(), QStringLiteral("sherpa-onnx"));
}

void AppSettingsTests::ttsEngineKindDefaultsWhenEmpty()
{
    AppSettings settings;
    settings.setTtsEngineKind(QStringLiteral(""));
    QCOMPARE(settings.ttsEngineKind(), QStringLiteral("piper"));
}

void AppSettingsTests::focusModeDefaultsDisabled()
{
    AppSettings settings;
    QVERIFY(!settings.focusModeEnabled());
    QVERIFY(settings.focusModeAllowCriticalAlerts());
    QCOMPARE(settings.focusModeDurationMinutes(), 0);
}

void AppSettingsTests::focusModeDurationClamps()
{
    AppSettings settings;
    settings.setFocusModeDurationMinutes(-10);
    QCOMPARE(settings.focusModeDurationMinutes(), 0);

    settings.setFocusModeDurationMinutes(24 * 60 + 25);
    QCOMPARE(settings.focusModeDurationMinutes(), 24 * 60);
}

void AppSettingsTests::privateModeDefaultsDisabled()
{
    AppSettings settings;
    QVERIFY(!settings.privateModeEnabled());
}

void AppSettingsTests::permissionOverridesSanitized()
{
    AppSettings settings;
    QVariantMap allowed;
    allowed.insert(QStringLiteral("capabilityId"), QStringLiteral("filesystem_write"));
    allowed.insert(QStringLiteral("decision"), QStringLiteral("ALLOW"));
    allowed.insert(QStringLiteral("scope"), QStringLiteral("project_workspace"));
    allowed.insert(QStringLiteral("reasonCode"), QStringLiteral("user.whitelist"));

    QVariantMap duplicate;
    duplicate.insert(QStringLiteral("capabilityId"), QStringLiteral("filesystem_write"));
    duplicate.insert(QStringLiteral("decision"), QStringLiteral("deny"));

    QVariantMap invalid;
    invalid.insert(QStringLiteral("capabilityId"), QStringLiteral("desktop_automation"));
    invalid.insert(QStringLiteral("decision"), QStringLiteral("maybe"));

    QVariantMap unknownCapability;
    unknownCapability.insert(QStringLiteral("capabilityId"), QStringLiteral("unregistered_power"));
    unknownCapability.insert(QStringLiteral("decision"), QStringLiteral("allow"));

    settings.setPermissionOverrides({allowed, duplicate, invalid, unknownCapability});

    const QVariantList overrides = settings.permissionOverrides();
    QCOMPARE(overrides.size(), 1);
    const QVariantMap stored = overrides.first().toMap();
    QCOMPARE(stored.value(QStringLiteral("capabilityId")).toString(),
             QStringLiteral("filesystem_write"));
    QCOMPARE(stored.value(QStringLiteral("decision")).toString(), QStringLiteral("allow"));
    QCOMPARE(stored.value(QStringLiteral("scope")).toString(), QStringLiteral("project_workspace"));
}

void AppSettingsTests::settingsChangedEmittedOnSetter()
{
    AppSettings settings;
    QSignalSpy spy(&settings, &AppSettings::settingsChanged);
    settings.setFocusModeEnabled(true);
    QCOMPARE(spy.count(), 1);
}

QTEST_APPLESS_MAIN(AppSettingsTests)
#include "AppSettingsTests.moc"
