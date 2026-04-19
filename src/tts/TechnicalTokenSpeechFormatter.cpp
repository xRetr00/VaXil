#include "tts/TechnicalTokenSpeechFormatter.h"

#include <QHash>
#include <QRegularExpression>

namespace {
QString collapseWhitespace(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}

QString splitCamelCase(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("(?<=[a-z0-9])(?=[A-Z])")), QStringLiteral(" "));
    return text;
}

QString normalizeTechnicalStem(QString stem)
{
    stem.replace(QRegularExpression(QStringLiteral("[_\\-]+")), QStringLiteral(" "));
    stem.replace(QRegularExpression(QStringLiteral("\\.+")), QStringLiteral(" dot "));
    stem = splitCamelCase(stem);
    stem = collapseWhitespace(stem);
    if (stem.isEmpty()) {
        return stem;
    }

    const QRegularExpression upperWordPattern(QStringLiteral("^[A-Z0-9]+$"));
    if (upperWordPattern.match(stem).hasMatch()) {
        return stem.toLower();
    }

    return stem;
}

const QHash<QString, QString> &extensionSpeechMap()
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("md"), QStringLiteral("dot M D")},
        {QStringLiteral("txt"), QStringLiteral("dot text")},
        {QStringLiteral("cpp"), QStringLiteral("dot C plus plus")},
        {QStringLiteral("h"), QStringLiteral("dot H")},
        {QStringLiteral("hpp"), QStringLiteral("dot H P P")},
        {QStringLiteral("json"), QStringLiteral("dot JSON")},
        {QStringLiteral("yaml"), QStringLiteral("dot YAML")},
        {QStringLiteral("yml"), QStringLiteral("dot YAML")},
        {QStringLiteral("dll"), QStringLiteral("dot D L L")},
        {QStringLiteral("exe"), QStringLiteral("dot E X E")},
        {QStringLiteral("bat"), QStringLiteral("dot bat")},
        {QStringLiteral("py"), QStringLiteral("dot pie")},
        {QStringLiteral("ts"), QStringLiteral("dot T S")},
        {QStringLiteral("js"), QStringLiteral("dot J S")},
        {QStringLiteral("html"), QStringLiteral("dot H T M L")},
        {QStringLiteral("css"), QStringLiteral("dot C S S")}
    };
    return map;
}

QString formatFileToken(const QString &token)
{
    const int dotIndex = token.lastIndexOf(QChar::fromLatin1('.'));
    if (dotIndex <= 0 || dotIndex >= token.size() - 1) {
        return token;
    }

    const QString stem = token.left(dotIndex);
    const QString extension = token.mid(dotIndex + 1).toLower();
    const QString spokenStem = normalizeTechnicalStem(stem);

    const auto map = extensionSpeechMap();
    const QString spokenExtension = map.value(extension, QStringLiteral("dot %1").arg(extension));
    if (spokenStem.isEmpty()) {
        return spokenExtension;
    }

    return QStringLiteral("%1 %2").arg(spokenStem, spokenExtension);
}

QString replaceFileTokens(const QString &text)
{
    const QRegularExpression filePattern(
        QStringLiteral("\\b[A-Za-z0-9_][A-Za-z0-9_\\-]*(?:\\.[A-Za-z0-9_\\-]+)+\\b"));

    QString rewritten;
    rewritten.reserve(text.size() + 24);
    int lastEnd = 0;

    auto it = filePattern.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString token = match.captured(0);
        rewritten += text.mid(lastEnd, match.capturedStart() - lastEnd);
        rewritten += formatFileToken(token);
        lastEnd = match.capturedEnd();
    }

    rewritten += text.mid(lastEnd);
    return rewritten;
}

QString replaceStandaloneExtensions(QString text)
{
    const auto map = extensionSpeechMap();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        text.replace(
            QRegularExpression(
                QStringLiteral("(?<![A-Za-z0-9_])\\.%1\\b").arg(QRegularExpression::escape(it.key())),
                QRegularExpression::CaseInsensitiveOption),
            it.value());
    }
    return text;
}
}

QString TechnicalTokenSpeechFormatter::formatTechnicalTokens(const QString &text) const
{
    QString rewritten = replaceFileTokens(text);
    rewritten = replaceStandaloneExtensions(rewritten);
    return collapseWhitespace(rewritten);
}
