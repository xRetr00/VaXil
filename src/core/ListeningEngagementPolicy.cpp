#include "core/ListeningEngagementPolicy.h"

#include <algorithm>

#include <QRegularExpression>

namespace {
float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

int envInt(const char *name, int fallback, int minValue, int maxValue)
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue(name, &ok);
    if (!ok) {
        return fallback;
    }
    return std::clamp(value, minValue, maxValue);
}

float envFloat(const char *name, float fallback, float minValue, float maxValue)
{
    bool ok = false;
    const float value = qEnvironmentVariable(name).toFloat(&ok);
    if (!ok) {
        return fallback;
    }
    return std::clamp(value, minValue, maxValue);
}

QString normalizeKeyword(QString keyword)
{
    keyword = keyword.trimmed().toLower();
    keyword.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    return keyword.simplified();
}
}

ListeningEngagementThresholdProfile ListeningEngagementThresholdProfile::fromEnvironment()
{
    ListeningEngagementThresholdProfile profile;
    profile.followUpWindowMs = envInt("JARVIS_ENGAGEMENT_FOLLOWUP_WINDOW_MS", profile.followUpWindowMs, 2000, 12000);
    profile.postTtsResidueGuardMs = envInt("JARVIS_ENGAGEMENT_POST_TTS_RESIDUE_GUARD_MS", profile.postTtsResidueGuardMs, 120, 1600);
    profile.minVoicedDurationMs = envInt("JARVIS_ENGAGEMENT_MIN_VOICED_MS", profile.minVoicedDurationMs, 120, 1600);
    profile.minVoicedRatio = envFloat("JARVIS_ENGAGEMENT_MIN_VOICED_RATIO", profile.minVoicedRatio, 0.05f, 0.95f);
    profile.minAverageRms = envFloat("JARVIS_ENGAGEMENT_MIN_AVERAGE_RMS", profile.minAverageRms, 0.003f, 0.20f);
    profile.minPeakLevel = envFloat("JARVIS_ENGAGEMENT_MIN_PEAK", profile.minPeakLevel, 0.02f, 0.9f);
    profile.minEngagementConfidence = envFloat("JARVIS_ENGAGEMENT_MIN_CONFIDENCE", profile.minEngagementConfidence, 0.20f, 0.98f);
    profile.minNearFieldConfidence = envFloat("JARVIS_ENGAGEMENT_MIN_NEAR_FIELD", profile.minNearFieldConfidence, 0.05f, 0.98f);
    profile.minBargeInConfidence = envFloat("JARVIS_ENGAGEMENT_MIN_BARGEIN_CONF", profile.minBargeInConfidence, 0.35f, 0.99f);
    profile.minRepairConfidence = envFloat("JARVIS_ENGAGEMENT_MIN_REPAIR_CONF", profile.minRepairConfidence, 0.35f, 0.99f);
    return profile;
}

void FollowUpWindowManager::open(qint64 nowMs)
{
    m_expiresAtMs = nowMs + std::max(0, m_windowMs);
}

void FollowUpWindowManager::refresh(qint64 nowMs)
{
    if (m_expiresAtMs <= 0) {
        return;
    }
    open(nowMs);
}

void FollowUpWindowManager::clear()
{
    m_expiresAtMs = 0;
}

bool FollowUpWindowManager::isOpen(qint64 nowMs) const
{
    return m_expiresAtMs > 0 && nowMs < m_expiresAtMs;
}

ListeningEngagementPolicy::ListeningEngagementPolicy(const ListeningEngagementThresholdProfile &thresholds)
    : m_thresholds(thresholds)
    , m_followUpWindow(thresholds.followUpWindowMs)
    , m_interruptionKeywords({
          QStringLiteral("stop"),
          QStringLiteral("wait"),
          QStringLiteral("no"),
          QStringLiteral("listen"),
          QStringLiteral("pause"),
          QStringLiteral("quiet"),
          QStringLiteral("cancel")
      })
{
}

void ListeningEngagementPolicy::setState(ListeningEngagementState state)
{
    m_state = state;
}

void ListeningEngagementPolicy::openFollowUpWindow(qint64 nowMs)
{
    m_followUpWindow.open(nowMs);
}

void ListeningEngagementPolicy::refreshFollowUpWindow(qint64 nowMs)
{
    m_followUpWindow.refresh(nowMs);
}

void ListeningEngagementPolicy::clearFollowUpWindow()
{
    m_followUpWindow.clear();
}

bool ListeningEngagementPolicy::isFollowUpWindowOpen(qint64 nowMs) const
{
    return m_followUpWindow.isOpen(nowMs);
}

void ListeningEngagementPolicy::noteAssistantSpeechStarted(qint64 nowMs)
{
    Q_UNUSED(nowMs);
    m_state = ListeningEngagementState::AssistantSpeaking;
}

void ListeningEngagementPolicy::noteAssistantSpeechFinished(qint64 nowMs)
{
    m_lastAssistantSpeechFinishedAtMs = nowMs;
    m_state = ListeningEngagementState::PostTtsResidueGuard;
}

bool ListeningEngagementPolicy::isPostTtsResidueGuardActive(qint64 nowMs) const
{
    if (m_lastAssistantSpeechFinishedAtMs <= 0) {
        return false;
    }
    return nowMs < (m_lastAssistantSpeechFinishedAtMs + m_thresholds.postTtsResidueGuardMs);
}

ListeningEngagementDecision ListeningEngagementPolicy::evaluateSpeechAttempt(
    const SpeechCaptureEvidence &evidence,
    const ListeningEngagementContext &context) const
{
    ListeningEngagementDecision decision;
    if (!evidence.hadSpeech || evidence.captureDurationMs <= 0) {
        decision.reasonCode = QStringLiteral("engagement.reject.no_speech");
        return decision;
    }

    const float speechEvidence = computeSpeechEvidenceConfidence(evidence);
    const float nearFieldConfidence = computeNearFieldConfidence(evidence);
    const float followUpBoost = context.followUpWindowOpen ? 0.14f : 0.0f;
    const float wakeBoost = context.wakeKeywordDetected ? 0.14f : 0.0f;
    const float stopBoost = context.stopKeywordDetected ? 0.10f : 0.0f;

    decision.nearFieldConfidence = nearFieldConfidence;
    decision.nearField = nearFieldConfidence >= m_thresholds.minNearFieldConfidence;
    decision.engagementConfidence = clamp01((speechEvidence * 0.62f) + (nearFieldConfidence * 0.30f) + followUpBoost + wakeBoost + stopBoost);

    if (context.postTtsResidueGuardActive && !context.wakeKeywordDetected && decision.engagementConfidence < 0.90f) {
        decision.rejectedAsEchoResidue = true;
        decision.reasonCode = QStringLiteral("engagement.reject.post_tts_residue");
        return decision;
    }

    if (evidence.voicedDurationMs < m_thresholds.minVoicedDurationMs
        || evidence.voicedRatio < m_thresholds.minVoicedRatio
        || (evidence.averageRms < m_thresholds.minAverageRms && evidence.peakLevel < m_thresholds.minPeakLevel)) {
        decision.reasonCode = QStringLiteral("engagement.reject.low_signal");
        return decision;
    }

    if (!decision.nearField && !context.wakeKeywordDetected) {
        decision.rejectedAsFarField = true;
        decision.reasonCode = QStringLiteral("engagement.reject.far_field");
        return decision;
    }

    float requiredConfidence = m_thresholds.minEngagementConfidence;
    if (!context.followUpWindowOpen && !context.wakeKeywordDetected) {
        requiredConfidence = std::min(0.95f, requiredConfidence + 0.10f);
    }

    if (decision.engagementConfidence < requiredConfidence) {
        decision.reasonCode = QStringLiteral("engagement.reject.uncertain");
        return decision;
    }

    decision.allowRecognition = true;
    decision.reasonCode = context.followUpWindowOpen
        ? QStringLiteral("engagement.accept.follow_up")
        : QStringLiteral("engagement.accept.directed");
    return decision;
}

BargeInDecision ListeningEngagementPolicy::evaluateBargeInKeyword(const QString &keyword, bool assistantSpeaking) const
{
    BargeInDecision decision;
    if (!assistantSpeaking) {
        decision.reasonCode = QStringLiteral("bargein.reject.not_speaking");
        return decision;
    }

    const QString normalized = normalizeKeyword(keyword);
    if (normalized.isEmpty()) {
        decision.reasonCode = QStringLiteral("bargein.reject.empty_keyword");
        return decision;
    }

    if (isLikelyAssistantNameKeyword(normalized)) {
        decision.confidence = 0.92f;
        decision.reasonCode = QStringLiteral("bargein.accept.assistant_name");
    } else if (isInterruptionKeyword(normalized)) {
        decision.stopIntent = true;
        decision.confidence = 0.86f;
        decision.reasonCode = QStringLiteral("bargein.accept.stop_keyword");
    } else {
        decision.confidence = 0.20f;
        decision.reasonCode = QStringLiteral("bargein.reject.unknown_keyword");
    }

    decision.allow = decision.confidence >= m_thresholds.minBargeInConfidence;
    if (!decision.allow && decision.reasonCode.startsWith(QStringLiteral("bargein.accept."))) {
        decision.reasonCode = QStringLiteral("bargein.reject.below_threshold");
    }
    return decision;
}

bool ListeningEngagementPolicy::shouldEmitRepair(const ListeningEngagementDecision &decision,
                                                 SpeechTranscriptDisposition disposition) const
{
    if (disposition != SpeechTranscriptDisposition::IgnoreAmbiguous) {
        return false;
    }

    return decision.allowRecognition
        && decision.nearField
        && decision.engagementConfidence >= m_thresholds.minRepairConfidence;
}

bool ListeningEngagementPolicy::isInterruptionKeyword(const QString &keyword) const
{
    return m_interruptionKeywords.contains(normalizeKeyword(keyword));
}

float ListeningEngagementPolicy::computeSpeechEvidenceConfidence(const SpeechCaptureEvidence &evidence) const
{
    const float durationConfidence = clamp01(static_cast<float>(evidence.voicedDurationMs) / std::max(1, m_thresholds.minVoicedDurationMs * 3));
    const float ratioConfidence = clamp01(evidence.voicedRatio);
    const float energyConfidence = clamp01((evidence.averageRms - 0.004f) / 0.08f);
    const float stabilityConfidence = clamp01(1.0f - (static_cast<float>(std::max(0, evidence.speechActivityTransitions - 2)) / 10.0f));

    return clamp01((durationConfidence * 0.38f)
        + (ratioConfidence * 0.24f)
        + (energyConfidence * 0.24f)
        + (stabilityConfidence * 0.14f));
}

float ListeningEngagementPolicy::computeNearFieldConfidence(const SpeechCaptureEvidence &evidence) const
{
    const float avgFloor = std::max(0.0001f, m_thresholds.minAverageRms);
    const float peakFloor = std::max(0.0001f, m_thresholds.minPeakLevel);
    const float voicedFloor = clamp01(m_thresholds.minVoicedRatio);

    const float energyConfidence = clamp01((evidence.averageRms - (avgFloor * 0.7f)) / (avgFloor * 6.0f));
    const float peakConfidence = clamp01((evidence.peakLevel - (peakFloor * 0.7f)) / (peakFloor * 6.0f));
    const float burstConfidence = clamp01(
        (evidence.peakLevel - std::max(0.0f, evidence.averageRms * 1.1f)) / std::max(0.001f, peakFloor * 8.0f));
    const float voicedConfidence = clamp01((evidence.voicedRatio - voicedFloor) / std::max(0.001f, (1.0f - voicedFloor)));

    return clamp01((energyConfidence * 0.45f)
        + (peakConfidence * 0.35f)
        + (burstConfidence * 0.15f)
        + (voicedConfidence * 0.05f));
}

bool ListeningEngagementPolicy::isLikelyAssistantNameKeyword(const QString &keyword) const
{
    return keyword.contains(QStringLiteral("vaxil"))
        || keyword.contains(QStringLiteral("vaksil"))
        || keyword.contains(QStringLiteral("vaxel"));
}
