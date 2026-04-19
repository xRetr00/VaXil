#include "tts/SpeechPreparationPipeline.h"

#include <algorithm>

#include <QRegularExpression>

namespace {
QString canonicalSpeechKey(const QString &text)
{
    QString key = text.toLower();
    key.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    return key;
}

QString summarizePauseHints(const QString &text)
{
    QStringList points;
    points.reserve(8);
    int count = 0;
    for (int i = 0; i < text.size() && points.size() < 8; ++i) {
        QString token;
        int width = 1;
        const QChar ch = text.at(i);
        if (ch == QChar::fromLatin1(',')) {
            token = QStringLiteral("comma");
        } else if (ch == QChar::fromLatin1(';')) {
            token = QStringLiteral("semicolon");
        } else if (ch == QChar::fromLatin1(':')) {
            token = QStringLiteral("colon");
        } else if (ch == QChar::fromLatin1('.')) {
            if (i + 2 < text.size()
                && text.at(i + 1) == QChar::fromLatin1('.')
                && text.at(i + 2) == QChar::fromLatin1('.')) {
                token = QStringLiteral("ellipsis");
                width = 3;
                i += 2;
            } else {
                token = QStringLiteral("period");
            }
        } else if (ch == QChar::fromLatin1('!')) {
            token = QStringLiteral("exclamation");
        } else if (ch == QChar::fromLatin1('?')) {
            token = QStringLiteral("question");
        }

        if (!token.isEmpty()) {
            ++count;
            const int start = std::max(0, i - 14);
            const int len = std::min(34, static_cast<int>(text.size()) - start);
            const QString context = text.mid(start, len).simplified();
            points.push_back(QStringLiteral("%1@%2\"%3\"").arg(token, QString::number(i), context));
        }
        i += width - 1;
    }

    return QStringLiteral("pause_points=%1 sample=[%2]")
        .arg(count)
        .arg(points.join(QStringLiteral(" | ")));
}
}

SpeechPreparationPipeline::SpeechPreparationPipeline(int dedupeWindowMs)
    : m_dedupeGuard(dedupeWindowMs)
{
}

void SpeechPreparationPipeline::setDedupeWindowMs(int dedupeWindowMs)
{
    m_dedupeGuard.setWindowMs(dedupeWindowMs);
}

int SpeechPreparationPipeline::dedupeWindowMs() const
{
    return m_dedupeGuard.windowMs();
}

SpeechPreparationTrace SpeechPreparationPipeline::prepare(const QString &text,
                                                          const TtsUtteranceContext &context,
                                                          qint64 nowMs)
{
    SpeechPreparationTrace trace;
    trace.rawInputText = text;
    trace.normalizedText = m_normalizer.normalize(text);
    trace.punctuationShapedText = m_punctuationShaper.shape(trace.normalizedText);
    trace.pauseHintSummary = summarizePauseHints(trace.punctuationShapedText);
    trace.finalSpokenText = trace.punctuationShapedText;

    if (trace.finalSpokenText.trimmed().isEmpty()) {
        trace.emptyAfterPreparation = true;
        trace.dedupeDecision.admitted = false;
        trace.dedupeDecision.reason = QStringLiteral("empty_after_preparation");
        return trace;
    }

    if (isStatusOnlyUtterance(trace.finalSpokenText)) {
        trace.statusOnly = true;
        trace.dedupeDecision.admitted = false;
        trace.dedupeDecision.reason = QStringLiteral("status_only_utterance");
        return trace;
    }

    UtteranceIdentity identity;
    identity.utteranceClass = context.utteranceClass;
    identity.source = context.source;
    identity.turnId = context.turnId;
    identity.semanticTarget = context.semanticTarget;
    trace.dedupeDecision = m_dedupeGuard.evaluate(trace.finalSpokenText, identity, nowMs);
    return trace;
}

bool SpeechPreparationPipeline::isStatusOnlyUtterance(const QString &text) const
{
    static const QStringList statusKeys = {
        QStringLiteral("listening"),
        QStringLiteral("processingrequest"),
        QStringLiteral("responseready"),
        QStringLiteral("standingby"),
        QStringLiteral("requestcancelled"),
        QStringLiteral("commandexecuted"),
        QStringLiteral("loadingservices"),
        QStringLiteral("settingssaved"),
        QStringLiteral("unabletostartlistening"),
        QStringLiteral("transcribedby")
    };

    const QString key = canonicalSpeechKey(text);
    return !key.isEmpty() && statusKeys.contains(key);
}
