#include "wakeword/WakeWordDataCapture.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QDateTime>

#include "learning_data/LearningDataCollector.h"
#include "learning_data/LearningDataTypes.h"
#include "settings/AppSettings.h"

namespace {

constexpr int kPcmBytesPerSample = 2;
constexpr int kRingBufferWindowMs = 6000;
constexpr int kPositiveWindowMs = 1400;
constexpr int kNegativeWindowMs = 1400;
constexpr int kFalseRejectWindowMs = 1800;
constexpr int kNegativeSampleGapMs = 7000;
constexpr int kNegativeSamplePostWakeGuardMs = 1800;
constexpr int kDetectionContextValidityMs = 20000;

int bytesForMs(int sampleRate, int channels, int windowMs)
{
    const qint64 safeRate = std::max(1, sampleRate);
    const qint64 safeChannels = std::max(1, channels);
    const qint64 bytes = (safeRate * safeChannels * kPcmBytesPerSample * std::max(1, windowMs)) / 1000;
    return static_cast<int>(std::clamp<qint64>(bytes, 0, std::numeric_limits<int>::max()));
}

QString resolvedCaptureSource(const QString &captureSource, const AppSettings *settings)
{
    if (!captureSource.trimmed().isEmpty()) {
        return captureSource;
    }
    if (settings) {
        return settings->selectedAudioInputDeviceId();
    }
    return QString();
}

} // namespace

WakeWordDataCapture::WakeWordDataCapture(AppSettings *settings)
    : m_settings(settings)
{
}

void WakeWordDataCapture::appendWakeMonitorFrame(const AudioFrame &frame)
{
    if (frame.sampleCount <= 0) {
        return;
    }

    m_sampleRate = frame.sampleRate > 0 ? frame.sampleRate : 16000;
    m_channels = frame.channels > 0 ? frame.channels : 1;
    m_ringPcm.append(frameToPcm16(frame));
    trimRingBuffer();
}

void WakeWordDataCapture::recordWakeDetected(LearningData::LearningDataCollector *collector,
                                             const QString &sessionId,
                                             const QString &captureSource,
                                             const QString &wakeEngine,
                                             const QString &keywordText,
                                             double threshold,
                                             bool usedToStartSession)
{
    if (!collector) {
        return;
    }

    const QByteArray clip = captureWindowMs(kPositiveWindowMs);
    if (clip.isEmpty()) {
        return;
    }

    LearningData::WakeWordEvent event;
    event.eventId = LearningData::LearningDataCollector::createEventId(QStringLiteral("wakeword"));
    event.sessionId = sessionId;
    event.timestamp = LearningData::toIsoUtcNow();
    event.clipRole = LearningData::WakeWordClipRole::Positive;
    event.labelStatus = LearningData::WakeWordLabelStatus::AutoLabeled;
    event.wakeEngine = wakeEngine;
    event.keywordText = keywordText;
    event.detected = true;
    event.thresholdAvailable = threshold > 0.0;
    event.threshold = threshold;
    event.sampleRate = m_sampleRate;
    event.channels = m_channels;
    event.captureSource = resolvedCaptureSource(captureSource, m_settings);
    event.collectionReason = QStringLiteral("wakeword_detected_trigger");
    event.wasUsedToStartSession = usedToStartSession;
    event.cameFromFalseTrigger = false;
    event.cameFromMissedTriggerRecovery = false;
    event.notes = QStringLiteral("bounded_pretrigger_clip");

    collector->recordWakeWordEvent(event, clip);

    m_lastDetectedClip = clip;
    m_lastWakeDetectedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_lastWakeFalseAcceptRecorded = false;
    m_lastWakeAmbiguousRecorded = false;
}

void WakeWordDataCapture::maybeRecordNegativeSample(LearningData::LearningDataCollector *collector,
                                                    const QString &sessionId,
                                                    const QString &captureSource,
                                                    const QString &wakeEngine,
                                                    const QString &keywordText,
                                                    double threshold,
                                                    bool speechDetected)
{
    if (!collector || !speechDetected) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if ((nowMs - m_lastNegativeSampleAtMs) < kNegativeSampleGapMs) {
        return;
    }
    if (m_lastWakeDetectedAtMs > 0 && (nowMs - m_lastWakeDetectedAtMs) < kNegativeSamplePostWakeGuardMs) {
        return;
    }

    const QByteArray clip = captureWindowMs(kNegativeWindowMs);
    if (clip.isEmpty()) {
        return;
    }

    LearningData::WakeWordEvent event;
    event.eventId = LearningData::LearningDataCollector::createEventId(QStringLiteral("wakeword"));
    event.sessionId = sessionId;
    event.timestamp = LearningData::toIsoUtcNow();
    event.clipRole = LearningData::WakeWordClipRole::Negative;
    event.labelStatus = LearningData::WakeWordLabelStatus::Assumed;
    event.wakeEngine = wakeEngine;
    event.keywordText = keywordText;
    event.detected = false;
    event.thresholdAvailable = threshold > 0.0;
    event.threshold = threshold;
    event.sampleRate = m_sampleRate;
    event.channels = m_channels;
    event.captureSource = resolvedCaptureSource(captureSource, m_settings);
    event.collectionReason = QStringLiteral("non_trigger_speech_sample");
    event.notes = QStringLiteral("wake_monitor_negative_window");

    collector->recordWakeWordEvent(event, clip);
    m_lastNegativeSampleAtMs = nowMs;
}

void WakeWordDataCapture::recordFalseAcceptFromLastDetection(LearningData::LearningDataCollector *collector,
                                                             const QString &sessionId,
                                                             const QString &turnId,
                                                             const QString &reason)
{
    if (!collector || m_lastDetectedClip.isEmpty() || m_lastWakeFalseAcceptRecorded) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastWakeDetectedAtMs <= 0 || (nowMs - m_lastWakeDetectedAtMs) > kDetectionContextValidityMs) {
        return;
    }

    LearningData::WakeWordEvent event;
    event.eventId = LearningData::LearningDataCollector::createEventId(QStringLiteral("wakeword"));
    event.sessionId = sessionId;
    event.turnId = turnId;
    event.timestamp = LearningData::toIsoUtcNow();
    event.clipRole = LearningData::WakeWordClipRole::FalseAccept;
    event.labelStatus = LearningData::WakeWordLabelStatus::Assumed;
    event.detected = true;
    event.sampleRate = m_sampleRate;
    event.channels = m_channels;
    event.collectionReason = reason.trimmed().isEmpty()
        ? QStringLiteral("false_trigger_no_speech")
        : reason.trimmed();
    event.wasUsedToStartSession = true;
    event.cameFromFalseTrigger = true;
    event.notes = QStringLiteral("derived_from_last_wake_detection");

    collector->recordWakeWordEvent(event, m_lastDetectedClip);
    m_lastWakeFalseAcceptRecorded = true;
}

void WakeWordDataCapture::recordAmbiguousFromLastDetection(LearningData::LearningDataCollector *collector,
                                                           const QString &sessionId,
                                                           const QString &turnId,
                                                           const QString &reason)
{
    if (!collector || m_lastDetectedClip.isEmpty() || m_lastWakeAmbiguousRecorded) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastWakeDetectedAtMs <= 0 || (nowMs - m_lastWakeDetectedAtMs) > kDetectionContextValidityMs) {
        return;
    }

    LearningData::WakeWordEvent event;
    event.eventId = LearningData::LearningDataCollector::createEventId(QStringLiteral("wakeword"));
    event.sessionId = sessionId;
    event.turnId = turnId;
    event.timestamp = LearningData::toIsoUtcNow();
    event.clipRole = LearningData::WakeWordClipRole::Ambiguous;
    event.labelStatus = LearningData::WakeWordLabelStatus::Assumed;
    event.detected = true;
    event.sampleRate = m_sampleRate;
    event.channels = m_channels;
    event.collectionReason = reason.trimmed().isEmpty()
        ? QStringLiteral("ambiguous_follow_up_after_trigger")
        : reason.trimmed();
    event.wasUsedToStartSession = true;
    event.cameFromFalseTrigger = true;
    event.notes = QStringLiteral("ambiguous_after_wake_trigger");

    collector->recordWakeWordEvent(event, m_lastDetectedClip);
    m_lastWakeAmbiguousRecorded = true;
}

void WakeWordDataCapture::recordFalseRejectRecovery(LearningData::LearningDataCollector *collector,
                                                    const QString &sessionId,
                                                    const QString &turnId,
                                                    const QString &captureSource,
                                                    const QString &wakeEngine,
                                                    const QString &keywordText,
                                                    double threshold,
                                                    const QString &notes,
                                                    bool userConfirmed)
{
    if (!collector) {
        return;
    }

    const QByteArray clip = captureWindowMs(kFalseRejectWindowMs);
    if (clip.isEmpty()) {
        return;
    }

    LearningData::WakeWordEvent event;
    event.eventId = LearningData::LearningDataCollector::createEventId(QStringLiteral("wakeword"));
    event.sessionId = sessionId;
    event.turnId = turnId;
    event.timestamp = LearningData::toIsoUtcNow();
    event.clipRole = LearningData::WakeWordClipRole::FalseReject;
    event.labelStatus = userConfirmed
        ? LearningData::WakeWordLabelStatus::UserConfirmed
        : LearningData::WakeWordLabelStatus::Assumed;
    event.wakeEngine = wakeEngine;
    event.keywordText = keywordText;
    event.detected = false;
    event.thresholdAvailable = threshold > 0.0;
    event.threshold = threshold;
    event.sampleRate = m_sampleRate;
    event.channels = m_channels;
    event.captureSource = resolvedCaptureSource(captureSource, m_settings);
    event.collectionReason = QStringLiteral("missed_trigger_recovery");
    event.wasUsedToStartSession = false;
    event.cameFromFalseTrigger = false;
    event.cameFromMissedTriggerRecovery = true;
    event.notes = notes.trimmed().isEmpty()
        ? QStringLiteral("manual_false_reject_capture")
        : notes.trimmed();

    collector->recordWakeWordEvent(event, clip);
}

QByteArray WakeWordDataCapture::captureWindowMs(int windowMs) const
{
    if (m_ringPcm.isEmpty()) {
        return QByteArray();
    }

    const int bytes = bytesForMs(m_sampleRate, m_channels, windowMs);
    if (bytes <= 0) {
        return QByteArray();
    }
    if (m_ringPcm.size() <= bytes) {
        return m_ringPcm;
    }
    return m_ringPcm.right(bytes);
}

QByteArray WakeWordDataCapture::frameToPcm16(const AudioFrame &frame)
{
    QByteArray pcm;
    pcm.reserve(frame.sampleCount * kPcmBytesPerSample);
    for (int i = 0; i < frame.sampleCount; ++i) {
        const float sample = std::clamp(frame.samples[static_cast<size_t>(i)], -1.0f, 1.0f);
        const qint16 value = static_cast<qint16>(std::lround(sample * 32767.0f));
        pcm.append(reinterpret_cast<const char *>(&value), static_cast<int>(sizeof(value)));
    }
    return pcm;
}

void WakeWordDataCapture::trimRingBuffer()
{
    const int maxBytes = bytesForMs(m_sampleRate, m_channels, kRingBufferWindowMs);
    if (maxBytes <= 0 || m_ringPcm.size() <= maxBytes) {
        return;
    }
    m_ringPcm.remove(0, m_ringPcm.size() - maxBytes);
}
