#include "tts/SpeechPunctuationShaper.h"

#include <QRegularExpression>

namespace {
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
}

QString SpeechPunctuationShaper::shape(const QString &input) const
{
    QString shaped = input;
    shaped.replace(QRegularExpression(QStringLiteral("[\"“”]+([^\"“”]+)[\"“”]+")), QStringLiteral("\\1"));
    shaped.replace(QRegularExpression(QStringLiteral("\\?(?=\\s+[A-Za-z])")), QStringLiteral(""));
    shaped.replace(QRegularExpression(QStringLiteral("\\s*;\\s*")), QStringLiteral(". "));
    shaped.replace(QRegularExpression(QStringLiteral("\\s*([,.:!?])\\s*")), QStringLiteral("\\1 "));
    shaped.replace(QRegularExpression(QStringLiteral("([.!?]){2,}")), QStringLiteral("\\1"));
    shaped.replace(QRegularExpression(QStringLiteral("\\s+([,.:!?])")), QStringLiteral("\\1"));
    shaped = collapseWhitespace(shaped);

    if (shaped.isEmpty()) {
        return {};
    }
    return ensureTerminalPunctuation(shaped);
}
