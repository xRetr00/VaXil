#include "ai/SpokenReply.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {
QString stripCodeFences(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("(?is)^\\s*```(?:json|text|markdown)?\\s*|\\s*```\\s*$")), QStringLiteral(" "));
    return text;
}

QString stripHiddenReasoning(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("(?is)<think>.*?(?=\\b(?:assistant|final)\\s*:|</think>|$)")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?is)</?think>")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?is)<\\s*(identity|behavior_mode|mode|task_state|constraints|tools|workspace|memory_context|execution_loop|response_contract|agent_mode)\\b[^>]*>.*?<\\s*/\\s*\\1\\s*>")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?im)^\\s*(reasoning|analysis|thought process)\\s*:\\s*.*$")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?im)^\\s*(assistant|system|developer)\\s*:\\s*")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?im)^\\s*(tone|addressing style|user name|user preferences|runtime|session_goal|policy|mode|reasoning mode)\\s*:\\s*.*$")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?im)^\\s*[-*]\\s*(mode|policy|session_goal|reasoning mode)\\s*:\\s*.*$")), QStringLiteral(" "));
    text.replace(QStringLiteral("/no_think"), QStringLiteral(" "));
    return text;
}

QString stripModelWrapperTags(QString text)
{
    text.replace(QRegularExpression(
                     QStringLiteral("</?\\s*(answer|final|response|assistant_response|assistant|message|output)\\s*>"),
                     QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral(" "));
    text.replace(QRegularExpression(
                     QStringLiteral("</?\\s*[a-zA-Z][a-zA-Z0-9_-]{0,32}(?:\\s+[^<>]*)?>")),
                 QStringLiteral(" "));
    return text;
}

QString collapseWhitespace(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}

QString ensureTerminalPunctuation(QString text)
{
    text = collapseWhitespace(text);
    if (!text.isEmpty()
        && !text.endsWith(QChar::fromLatin1('.'))
        && !text.endsWith(QChar::fromLatin1('!'))
        && !text.endsWith(QChar::fromLatin1('?'))) {
        text += QChar::fromLatin1('.');
    }
    return text;
}

QString canonicalSpeechKey(const QString &text)
{
    QString key = text.toLower();
    key.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    return key;
}

bool isStatusOnlyText(const QString &text)
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

QString trimJsonPayload(const QString &input)
{
    const int start = input.indexOf(QChar::fromLatin1('{'));
    const int end = input.lastIndexOf(QChar::fromLatin1('}'));
    if (start < 0 || end < start) {
        return {};
    }
    return input.mid(start, end - start + 1);
}

QString limitSpokenLength(const QString &input)
{
    constexpr int kMaxSpokenSentences = 3;
    constexpr int kMaxSpokenChars = 280;
    const QStringList sentences = input.split(QRegularExpression(QStringLiteral("(?<=[.!?])\\s+")), Qt::SkipEmptyParts);
    QStringList limited;
    int totalChars = 0;
    bool truncated = false;

    for (const QString &rawSentence : sentences) {
        QString sentence = ensureTerminalPunctuation(rawSentence.trimmed());
        if (sentence.isEmpty()) {
            continue;
        }
        if (limited.size() >= kMaxSpokenSentences) {
            truncated = true;
            break;
        }

        const int nextChars = totalChars + (limited.isEmpty() ? 0 : 1) + sentence.size();
        if (nextChars > kMaxSpokenChars) {
            if (limited.isEmpty()) {
                sentence = ensureTerminalPunctuation(sentence.left(kMaxSpokenChars).trimmed());
                if (!sentence.isEmpty()) {
                    limited.push_back(sentence);
                }
            }
            truncated = true;
            break;
        }

        limited.push_back(sentence);
        totalChars = nextChars;
    }

    QString result = limited.join(QStringLiteral(" "));
    if (result.isEmpty()) {
        result = ensureTerminalPunctuation(input.left(kMaxSpokenChars).trimmed());
        truncated = input.size() > result.size();
    }

    if (truncated && !result.isEmpty()) {
        result = ensureTerminalPunctuation(result);
        result += QStringLiteral(" The rest is on screen.");
    }

    return collapseWhitespace(result);
}
}

QString sanitizeDisplayText(const QString &input)
{
    QString cleaned = input;
    cleaned = stripCodeFences(cleaned);
    cleaned = stripHiddenReasoning(cleaned);
    cleaned = stripModelWrapperTags(cleaned);
    cleaned.replace(QRegularExpression(QStringLiteral("https?://\\S+")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("[\\x{1F300}-\\x{1FAFF}\\x{2600}-\\x{27BF}]")), QStringLiteral(" "));
    cleaned = collapseWhitespace(cleaned);
    return cleaned;
}

QString sanitizeSpokenText(const QString &input)
{
    QString cleaned = sanitizeDisplayText(input);
    cleaned.replace(QRegularExpression(QStringLiteral("[`*_#~]+")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("[\\[\\]{}<>|]+")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("[\\x{1F300}-\\x{1FAFF}\\x{2600}-\\x{27BF}]")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("\\b[A-Z_]{2,}\\b(?=\\s|$)")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("\\b\\d{1,2}:\\d{2}(?::\\d{2})?\\b")), QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("\\s*([,.;:!?])\\s*")), QStringLiteral("\\1 "));
    cleaned = collapseWhitespace(cleaned);
    cleaned = ensureTerminalPunctuation(cleaned);
    return limitSpokenLength(cleaned);
}

SpokenReply parseSpokenReply(const QString &input)
{
    SpokenReply reply;

    const QString trimmed = input.trimmed();
    const QString jsonCandidate = trimJsonPayload(trimmed);
    if (!jsonCandidate.isEmpty()) {
        const QJsonDocument document = QJsonDocument::fromJson(jsonCandidate.toUtf8());
        if (document.isObject()) {
            const QJsonObject object = document.object();
            reply.displayText = sanitizeDisplayText(object.value(QStringLiteral("display_text")).toString());
            reply.spokenText = sanitizeSpokenText(object.value(QStringLiteral("spoken_text")).toString());
            if (object.contains(QStringLiteral("should_speak"))) {
                reply.shouldSpeak = object.value(QStringLiteral("should_speak")).toBool(true);
            }
        }
    }

    if (reply.displayText.isEmpty()) {
        reply.displayText = sanitizeDisplayText(trimmed);
    }
    if (reply.spokenText.isEmpty()) {
        reply.spokenText = sanitizeSpokenText(reply.displayText);
    }
    if (reply.displayText.isEmpty()) {
        reply.shouldSpeak = false;
    }

    if (isStatusOnlyText(reply.spokenText) || isStatusOnlyText(reply.displayText)) {
        reply.shouldSpeak = false;
        reply.spokenText.clear();
    }

    return reply;
}
