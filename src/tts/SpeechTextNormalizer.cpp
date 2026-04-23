#include "tts/SpeechTextNormalizer.h"

#include <QRegularExpression>

#include "tts/TechnicalTokenSpeechFormatter.h"

namespace {
QString collapseWhitespace(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}

QString stripHiddenReasoning(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("(?is)<think>.*?(?=\\b(?:assistant|final)\\s*:|</think>|$)")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?is)</?think>")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?is)<\\s*(identity|behavior_mode|mode|task_state|constraints|tools|workspace|memory_context|execution_loop|response_contract|agent_mode)\\b[^>]*>.*?<\\s*/\\s*\\1\\s*>")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?im)^\\s*(reasoning|analysis|thought process)\\s*:\\s*.*$")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?im)^\\s*```(?:json|text|markdown)?\\s*$")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?im)^\\s*```\\s*$")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?im)^\\s*(assistant|system|developer)\\s*:\\s*")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?im)^\\s*(tone|addressing style|user name|user preferences|runtime|session_goal|policy|mode|reasoning mode)\\s*:\\s*.*$")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?im)^\\s*[-*]\\s*(mode|policy|session_goal|reasoning mode)\\s*:\\s*.*$")), QStringLiteral(" "));
    text.replace(QStringLiteral("/no_think"), QStringLiteral(" "));
    return text;
}

QString removeNonSpeechArtifacts(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("https?://\\S+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("[`*_#~]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("[\\[\\]{}<>|]+")), QStringLiteral(" "));
    return text;
}

QString normalizeCommonAcronyms(QString text)
{
    static const QList<QPair<QRegularExpression, QString>> replacements = {
        {QRegularExpression(QStringLiteral("\\bai\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("AI")},
        {QRegularExpression(QStringLiteral("\\bapi\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("API")},
        {QRegularExpression(QStringLiteral("\\bcpu\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("CPU")},
        {QRegularExpression(QStringLiteral("\\bgpu\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("GPU")},
        {QRegularExpression(QStringLiteral("\\bui\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("UI")},
        {QRegularExpression(QStringLiteral("\\btts\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("TTS")},
        {QRegularExpression(QStringLiteral("\\bstt\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("STT")},
        {QRegularExpression(QStringLiteral("\\bjson\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("JSON")},
        {QRegularExpression(QStringLiteral("\\bhtml\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("HTML")},
        {QRegularExpression(QStringLiteral("\\bcss\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("CSS")},
        {QRegularExpression(QStringLiteral("\\burl\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("URL")},
        {QRegularExpression(QStringLiteral("\\bvs\\s+code\\b"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("VS Code")}
    };

    for (const auto &entry : replacements) {
        text.replace(entry.first, entry.second);
    }
    return text;
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
}

QString SpeechTextNormalizer::normalize(const QString &input) const
{
    QString normalized = stripHiddenReasoning(input);
    normalized.replace(QRegularExpression(QStringLiteral("\\s*\\n+\\s*")), QStringLiteral(". "));
    normalized = removeNonSpeechArtifacts(normalized);

    TechnicalTokenSpeechFormatter formatter;
    normalized = formatter.formatTechnicalTokens(normalized);

    normalized.replace(QRegularExpression(QStringLiteral("(?<=[A-Za-z0-9])[_/](?=[A-Za-z0-9])")), QStringLiteral(" "));
    normalized.replace(QRegularExpression(QStringLiteral("(?<=[A-Za-z])-(?=[A-Za-z0-9])")), QStringLiteral(" "));
    normalized.replace(QRegularExpression(QStringLiteral("(?<=[0-9])-(?=[A-Za-z])")), QStringLiteral(" "));
    normalized = normalizeCommonAcronyms(normalized);

    normalized.replace(QRegularExpression(QStringLiteral("\\s*([,.;:!?])\\s*")), QStringLiteral("\\1 "));
    normalized = collapseWhitespace(normalized);
    if (normalized.isEmpty()) {
        return {};
    }

    return ensureTerminalPunctuation(normalized);
}
