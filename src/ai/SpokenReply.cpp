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
    text.replace(QRegularExpression(QStringLiteral("(?is)<think>.*?</think>")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?im)^\\s*(reasoning|analysis|thought process)\\s*:\\s*.*$")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?im)^\\s*(assistant|system|developer)\\s*:\\s*")), QStringLiteral(" "));
    text.replace(QStringLiteral("/no_think"), QStringLiteral(" "));
    return text;
}

QString collapseWhitespace(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
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
}

QString sanitizeDisplayText(const QString &input)
{
    QString cleaned = input;
    cleaned = stripCodeFences(cleaned);
    cleaned = stripHiddenReasoning(cleaned);
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
    cleaned.replace(QRegularExpression(QStringLiteral("\\s*([,.;:!?])\\s*")), QStringLiteral("\\1 "));
    cleaned = collapseWhitespace(cleaned);
    if (!cleaned.isEmpty()
        && !cleaned.endsWith(QChar::fromLatin1('.'))
        && !cleaned.endsWith(QChar::fromLatin1('!'))
        && !cleaned.endsWith(QChar::fromLatin1('?'))) {
        cleaned += QChar::fromLatin1('.');
    }
    return cleaned;
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

    return reply;
}
