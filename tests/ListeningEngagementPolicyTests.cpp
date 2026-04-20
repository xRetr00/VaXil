#include <QtTest>

#include "core/ListeningEngagementPolicy.h"

class ListeningEngagementPolicyTests : public QObject
{
    Q_OBJECT

private slots:
    void rejectsWeakAmbientCaptureBeforeTranscription();
    void rejectsFarFieldCapture();
    void acceptsStrongNearFieldFollowUp();
    void acceptsWakeDirectedCaptureAtTypicalMicLevels();
    void followUpWindowExpiresCleanly();
    void appliesPostTtsResidueGuard();
    void stateTransitionsAreDeterministic();
    void admitsStopKeywordBargeInDuringSpeech();
    void admitsAssistantNameBargeInDuringSpeech();
    void rejectsUnknownKeywordBargeIn();
    void suppressesRepairForWeakAmbiguousAttempt();
    void emitsRepairForStrongAmbiguousAttempt();
};

void ListeningEngagementPolicyTests::rejectsWeakAmbientCaptureBeforeTranscription()
{
    ListeningEngagementPolicy policy;

    SpeechCaptureEvidence evidence;
    evidence.hadSpeech = true;
    evidence.captureDurationMs = 900;
    evidence.voicedDurationMs = 120;
    evidence.voicedRatio = 0.08f;
    evidence.averageRms = 0.006f;
    evidence.peakLevel = 0.03f;

    const ListeningEngagementDecision decision = policy.evaluateSpeechAttempt(evidence, {});
    QVERIFY(!decision.allowRecognition);
    QCOMPARE(decision.reasonCode, QStringLiteral("engagement.reject.low_signal"));
}

void ListeningEngagementPolicyTests::rejectsFarFieldCapture()
{
    ListeningEngagementThresholdProfile profile;
    profile.minVoicedDurationMs = 150;
    profile.minVoicedRatio = 0.12f;
    profile.minAverageRms = 0.004f;
    profile.minPeakLevel = 0.03f;
    profile.minNearFieldConfidence = 0.90f;
    profile.minEngagementConfidence = 0.20f;
    ListeningEngagementPolicy policy(profile);

    SpeechCaptureEvidence evidence;
    evidence.hadSpeech = true;
    evidence.captureDurationMs = 1200;
    evidence.voicedDurationMs = 560;
    evidence.voicedRatio = 0.46f;
    evidence.averageRms = 0.015f;
    evidence.peakLevel = 0.075f;
    evidence.speechActivityTransitions = 6;

    ListeningEngagementContext context;
    context.followUpWindowOpen = true;
    const ListeningEngagementDecision decision = policy.evaluateSpeechAttempt(evidence, context);

    QVERIFY(!decision.allowRecognition);
    const QString debug = QStringLiteral("reason=%1 near=%2 nearConf=%3 conf=%4 residue=%5")
        .arg(decision.reasonCode,
             decision.nearField ? QStringLiteral("true") : QStringLiteral("false"),
             QString::number(decision.nearFieldConfidence, 'f', 3),
             QString::number(decision.engagementConfidence, 'f', 3),
             decision.rejectedAsEchoResidue ? QStringLiteral("true") : QStringLiteral("false"));
    QVERIFY2(decision.rejectedAsFarField, qPrintable(debug));
    QCOMPARE(decision.reasonCode, QStringLiteral("engagement.reject.far_field"));
}

void ListeningEngagementPolicyTests::acceptsStrongNearFieldFollowUp()
{
    ListeningEngagementPolicy policy;

    SpeechCaptureEvidence evidence;
    evidence.hadSpeech = true;
    evidence.captureDurationMs = 1300;
    evidence.voicedDurationMs = 860;
    evidence.voicedRatio = 0.66f;
    evidence.averageRms = 0.048f;
    evidence.peakLevel = 0.30f;
    evidence.speechActivityTransitions = 2;

    ListeningEngagementContext context;
    context.followUpWindowOpen = true;
    const ListeningEngagementDecision decision = policy.evaluateSpeechAttempt(evidence, context);

    QVERIFY(decision.allowRecognition);
    QVERIFY(decision.nearField);
    QVERIFY(decision.engagementConfidence >= policy.thresholds().minEngagementConfidence);
}

void ListeningEngagementPolicyTests::acceptsWakeDirectedCaptureAtTypicalMicLevels()
{
    ListeningEngagementPolicy policy;

    SpeechCaptureEvidence evidence;
    evidence.hadSpeech = true;
    evidence.captureDurationMs = 4600;
    evidence.voicedDurationMs = 2820;
    evidence.voicedRatio = 0.623f;
    evidence.averageRms = 0.0033f;
    evidence.peakLevel = 0.0320f;
    evidence.speechActivityTransitions = 3;

    ListeningEngagementContext context;
    context.followUpWindowOpen = true;
    context.wakeKeywordDetected = true;
    context.wakeKeyword = QStringLiteral("HEY_VAXIL");
    const ListeningEngagementDecision decision = policy.evaluateSpeechAttempt(evidence, context);

    const QString debug = QStringLiteral("reason=%1 near=%2 nearConf=%3 conf=%4")
        .arg(decision.reasonCode,
             decision.nearField ? QStringLiteral("true") : QStringLiteral("false"),
             QString::number(decision.nearFieldConfidence, 'f', 3),
             QString::number(decision.engagementConfidence, 'f', 3));
    QVERIFY2(decision.allowRecognition, qPrintable(debug));
}

void ListeningEngagementPolicyTests::followUpWindowExpiresCleanly()
{
    ListeningEngagementThresholdProfile profile;
    profile.followUpWindowMs = 200;
    ListeningEngagementPolicy policy(profile);

    const qint64 openedAt = 5000;
    policy.openFollowUpWindow(openedAt);
    QVERIFY(policy.isFollowUpWindowOpen(openedAt + 50));
    QVERIFY(!policy.isFollowUpWindowOpen(openedAt + 250));
}

void ListeningEngagementPolicyTests::appliesPostTtsResidueGuard()
{
    ListeningEngagementPolicy policy;
    const qint64 nowMs = 100000;
    policy.noteAssistantSpeechFinished(nowMs);

    SpeechCaptureEvidence evidence;
    evidence.hadSpeech = true;
    evidence.captureDurationMs = 700;
    evidence.voicedDurationMs = 300;
    evidence.voicedRatio = 0.30f;
    evidence.averageRms = 0.020f;
    evidence.peakLevel = 0.12f;

    ListeningEngagementContext context;
    context.followUpWindowOpen = true;
    context.postTtsResidueGuardActive = policy.isPostTtsResidueGuardActive(nowMs + 150);

    const ListeningEngagementDecision decision = policy.evaluateSpeechAttempt(evidence, context);
    QVERIFY(!decision.allowRecognition);
    QVERIFY(decision.rejectedAsEchoResidue);
    QCOMPARE(decision.reasonCode, QStringLiteral("engagement.reject.post_tts_residue"));
}

void ListeningEngagementPolicyTests::stateTransitionsAreDeterministic()
{
    ListeningEngagementPolicy policy;

    QCOMPARE(policy.state(), ListeningEngagementState::IdlePassive);
    policy.setState(ListeningEngagementState::AssistantSpeaking);
    QCOMPARE(policy.state(), ListeningEngagementState::AssistantSpeaking);
    policy.noteAssistantSpeechFinished(10000);
    QCOMPARE(policy.state(), ListeningEngagementState::PostTtsResidueGuard);
    QVERIFY(policy.isPostTtsResidueGuardActive(10100));
    QVERIFY(!policy.isPostTtsResidueGuardActive(11000));
    policy.setState(ListeningEngagementState::AwaitingFollowUp);
    QCOMPARE(policy.state(), ListeningEngagementState::AwaitingFollowUp);
}

void ListeningEngagementPolicyTests::admitsStopKeywordBargeInDuringSpeech()
{
    ListeningEngagementPolicy policy;
    const BargeInDecision decision = policy.evaluateBargeInKeyword(QStringLiteral("stop"), true);

    QVERIFY(decision.allow);
    QVERIFY(decision.stopIntent);
    QCOMPARE(decision.reasonCode, QStringLiteral("bargein.accept.stop_keyword"));
}

void ListeningEngagementPolicyTests::admitsAssistantNameBargeInDuringSpeech()
{
    ListeningEngagementPolicy policy;
    const BargeInDecision decision = policy.evaluateBargeInKeyword(QStringLiteral("hey vaxil"), true);

    QVERIFY(decision.allow);
    QVERIFY(!decision.stopIntent);
    QCOMPARE(decision.reasonCode, QStringLiteral("bargein.accept.assistant_name"));
}

void ListeningEngagementPolicyTests::rejectsUnknownKeywordBargeIn()
{
    ListeningEngagementPolicy policy;
    const BargeInDecision decision = policy.evaluateBargeInKeyword(QStringLiteral("background chatter"), true);

    QVERIFY(!decision.allow);
    QVERIFY(!decision.stopIntent);
}

void ListeningEngagementPolicyTests::suppressesRepairForWeakAmbiguousAttempt()
{
    ListeningEngagementPolicy policy;

    ListeningEngagementDecision decision;
    decision.allowRecognition = false;
    decision.nearField = false;
    decision.engagementConfidence = 0.31f;

    QVERIFY(!policy.shouldEmitRepair(decision, SpeechTranscriptDisposition::IgnoreAmbiguous));
}

void ListeningEngagementPolicyTests::emitsRepairForStrongAmbiguousAttempt()
{
    ListeningEngagementPolicy policy;

    ListeningEngagementDecision decision;
    decision.allowRecognition = true;
    decision.nearField = true;
    decision.engagementConfidence = 0.92f;

    QVERIFY(policy.shouldEmitRepair(decision, SpeechTranscriptDisposition::IgnoreAmbiguous));
}

QTEST_APPLESS_MAIN(ListeningEngagementPolicyTests)
#include "ListeningEngagementPolicyTests.moc"
