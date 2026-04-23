#include "core/tools/WebSearchQueryBuilder.h"

#include <QRegularExpression>

namespace {
bool needsFreshYear(const QString &lowered)
{
    return lowered.contains(QStringLiteral("latest"))
        || lowered.contains(QStringLiteral("current"))
        || lowered.contains(QStringLiteral("newest"))
        || lowered.contains(QStringLiteral("released"))
        || lowered.contains(QStringLiteral("release"))
        || lowered.contains(QStringLiteral("new model"));
}

QString stripFillers(QString text)
{
    text = text.trimmed();
    text.remove(QRegularExpression(QStringLiteral("^(yeah|yes|okay|ok|please|vaxil|jarvis)\\s*,?\\s*"),
                                   QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(QStringLiteral("^(can you|could you|would you|please)\\s+"),
                                   QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(QStringLiteral("\\b(search|browse)\\s+(the\\s+)?(web|internet)\\s*(for|about|on)?\\b"),
                                   QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(QStringLiteral("\\b(web|internet)\\s+search\\b"),
                                   QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(QStringLiteral("\\b(search|find|look up)\\s+(for\\s+)?"),
                                   QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(QStringLiteral("\\blook\\s+it\\s+up\\b"),
                                   QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(QStringLiteral("\\b(use|reach)\\s+(the\\s+)?(web|internet)\\b"),
                                   QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(QStringLiteral("\\b(instead of|rather than)\\s+guessing\\b"),
                                   QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(QStringLiteral("\\b(do not|don't|dont)\\s+guess\\b"),
                                   QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(QStringLiteral("\\babout\\b"),
                                   QRegularExpression::CaseInsensitiveOption));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    text.remove(QRegularExpression(QStringLiteral("^[\\s,.:;!?-]+|[\\s,.:;!?-]+$")));
    return text.trimmed();
}
}

QString WebSearchQueryBuilder::build(const QString &input, int currentYear)
{
    const QString lowered = input.toLower();
    QString query = stripFillers(input);
    if (query.isEmpty()) {
        query = input.trimmed();
    }

    const QString year = QString::number(currentYear);
    if (currentYear > 2000
        && needsFreshYear(lowered)
        && !query.contains(year)
        && !query.contains(QRegularExpression(QStringLiteral("\\b20\\d{2}\\b")))) {
        query = QStringLiteral("%1 %2").arg(query.trimmed(), year);
    }
    return query.simplified();
}
