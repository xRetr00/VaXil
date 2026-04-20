#pragma once

#include <QSet>
#include <QString>

#include "core/AssistantTypes.h"
#include "core/SpeechTranscriptGuard.h"

enum class ListeningEngagementState {
    IdlePassive,
    AwaitingFollowUp,
    UserSpeaking,
    AssistantSpeaking,
    BargeInEligible,
    PostTtsResidueGuard
};

struct ListeningEngagementThresholdProfile
{
    int followUpWindowMs = 6000;
    int postTtsResidueGuardMs = 420;
    int minVoicedDurationMs = 220;
    float minVoicedRatio = 0.18f;
    float minAverageRms = 0.0035f;
    float minPeakLevel = 0.02f;
    float minEngagementConfidence = 0.56f;
    float minNearFieldConfidence = 0.12f;
    float minBargeInConfidence = 0.80f;
    float minRepairConfidence = 0.72f;

    static ListeningEngagementThresholdProfile fromEnvironment();
};

struct ListeningEngagementContext
{
    bool conversationSessionActive = false;
    bool followUpWindowOpen = false;
    bool assistantSpeaking = false;
    bool postTtsResidueGuardActive = false;
    bool wakeKeywordDetected = false;
    bool stopKeywordDetected = false;
    QString wakeKeyword;
};

struct ListeningEngagementDecision
{
    bool allowRecognition = false;
    bool nearField = false;
    bool rejectedAsEchoResidue = false;
    bool rejectedAsFarField = false;
    float engagementConfidence = 0.0f;
    float nearFieldConfidence = 0.0f;
    QString reasonCode = QStringLiteral("engagement.reject.uncertain");
};

struct BargeInDecision
{
    bool allow = false;
    bool stopIntent = false;
    float confidence = 0.0f;
    QString reasonCode = QStringLiteral("bargein.reject.unknown_signal");
};

class FollowUpWindowManager
{
public:
    explicit FollowUpWindowManager(int windowMs)
        : m_windowMs(windowMs)
    {
    }

    void open(qint64 nowMs);
    void refresh(qint64 nowMs);
    void clear();
    [[nodiscard]] bool isOpen(qint64 nowMs) const;

private:
    int m_windowMs = 0;
    qint64 m_expiresAtMs = 0;
};

class ListeningEngagementPolicy
{
public:
    explicit ListeningEngagementPolicy(
        const ListeningEngagementThresholdProfile &thresholds = ListeningEngagementThresholdProfile::fromEnvironment());

    [[nodiscard]] const ListeningEngagementThresholdProfile &thresholds() const { return m_thresholds; }
    [[nodiscard]] ListeningEngagementState state() const { return m_state; }

    void setState(ListeningEngagementState state);
    void openFollowUpWindow(qint64 nowMs);
    void refreshFollowUpWindow(qint64 nowMs);
    void clearFollowUpWindow();
    [[nodiscard]] bool isFollowUpWindowOpen(qint64 nowMs) const;

    void noteAssistantSpeechStarted(qint64 nowMs);
    void noteAssistantSpeechFinished(qint64 nowMs);
    [[nodiscard]] bool isPostTtsResidueGuardActive(qint64 nowMs) const;

    [[nodiscard]] ListeningEngagementDecision evaluateSpeechAttempt(
        const SpeechCaptureEvidence &evidence,
        const ListeningEngagementContext &context) const;
    [[nodiscard]] BargeInDecision evaluateBargeInKeyword(const QString &keyword, bool assistantSpeaking) const;
    [[nodiscard]] bool shouldEmitRepair(const ListeningEngagementDecision &decision,
                                        SpeechTranscriptDisposition disposition) const;
    [[nodiscard]] bool isInterruptionKeyword(const QString &keyword) const;

private:
    [[nodiscard]] float computeSpeechEvidenceConfidence(const SpeechCaptureEvidence &evidence) const;
    [[nodiscard]] float computeNearFieldConfidence(const SpeechCaptureEvidence &evidence) const;
    [[nodiscard]] bool isLikelyAssistantNameKeyword(const QString &keyword) const;

    ListeningEngagementThresholdProfile m_thresholds;
    FollowUpWindowManager m_followUpWindow;
    ListeningEngagementState m_state = ListeningEngagementState::IdlePassive;
    qint64 m_lastAssistantSpeechFinishedAtMs = 0;
    QSet<QString> m_interruptionKeywords;
};
