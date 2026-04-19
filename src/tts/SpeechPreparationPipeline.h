#pragma once

#include <QString>

#include "tts/SpeechPunctuationShaper.h"
#include "tts/SpeechTextNormalizer.h"
#include "tts/TtsEngine.h"
#include "tts/UtteranceDedupeGuard.h"

struct SpeechPreparationTrace
{
    QString rawInputText;
    QString normalizedText;
    QString punctuationShapedText;
    QString pauseHintSummary;
    QString finalSpokenText;
    bool emptyAfterPreparation = false;
    bool statusOnly = false;
    UtteranceDedupeDecision dedupeDecision;
};

class SpeechPreparationPipeline
{
public:
    explicit SpeechPreparationPipeline(int dedupeWindowMs = 7000);

    void setDedupeWindowMs(int dedupeWindowMs);
    int dedupeWindowMs() const;

    SpeechPreparationTrace prepare(const QString &text,
                                   const TtsUtteranceContext &context,
                                   qint64 nowMs = 0);

private:
    bool isStatusOnlyUtterance(const QString &text) const;

    SpeechTextNormalizer m_normalizer;
    SpeechPunctuationShaper m_punctuationShaper;
    UtteranceDedupeGuard m_dedupeGuard;
};
