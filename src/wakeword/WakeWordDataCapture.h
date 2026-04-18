#pragma once

#include <QByteArray>
#include <QString>

#include "audio/AudioProcessingTypes.h"

class AppSettings;

namespace LearningData {
class LearningDataCollector;
}

class WakeWordDataCapture
{
public:
    explicit WakeWordDataCapture(AppSettings *settings = nullptr);

    void appendWakeMonitorFrame(const AudioFrame &frame);

    void recordWakeDetected(LearningData::LearningDataCollector *collector,
                            const QString &sessionId,
                            const QString &captureSource,
                            const QString &wakeEngine,
                            const QString &keywordText,
                            double threshold,
                            bool usedToStartSession);

    void maybeRecordNegativeSample(LearningData::LearningDataCollector *collector,
                                   const QString &sessionId,
                                   const QString &captureSource,
                                   const QString &wakeEngine,
                                   const QString &keywordText,
                                   double threshold,
                                   bool speechDetected);

    void recordFalseAcceptFromLastDetection(LearningData::LearningDataCollector *collector,
                                            const QString &sessionId,
                                            const QString &turnId,
                                            const QString &reason);

    void recordAmbiguousFromLastDetection(LearningData::LearningDataCollector *collector,
                                          const QString &sessionId,
                                          const QString &turnId,
                                          const QString &reason);

    void recordFalseRejectRecovery(LearningData::LearningDataCollector *collector,
                                   const QString &sessionId,
                                   const QString &turnId,
                                   const QString &captureSource,
                                   const QString &wakeEngine,
                                   const QString &keywordText,
                                   double threshold,
                                   const QString &notes,
                                   bool userConfirmed);

private:
    QByteArray captureWindowMs(int windowMs) const;
    static QByteArray frameToPcm16(const AudioFrame &frame);
    void trimRingBuffer();

    AppSettings *m_settings = nullptr;
    QByteArray m_ringPcm;
    QByteArray m_lastDetectedClip;
    int m_sampleRate = 16000;
    int m_channels = 1;
    qint64 m_lastWakeDetectedAtMs = 0;
    qint64 m_lastNegativeSampleAtMs = 0;
    bool m_lastWakeFalseAcceptRecorded = false;
    bool m_lastWakeAmbiguousRecorded = false;
};
