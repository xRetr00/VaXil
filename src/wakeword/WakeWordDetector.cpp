#include "WakeWordDetector.h"

#include <algorithm>

#include <QRegularExpression>
#include <QStringList>

namespace {
QStringList normalizedTokens(const QString &transcript)
{
    const QString normalized = WakeWordDetector::normalizeTranscript(transcript);
    if (normalized.isEmpty()) {
        return {};
    }

    return normalized.split(QChar::fromLatin1(' '), Qt::SkipEmptyParts);
}

int editDistance(const QString &left, const QString &right)
{
    const int leftSize = left.size();
    const int rightSize = right.size();
    QVector<int> costs(rightSize + 1);
    for (int j = 0; j <= rightSize; ++j) {
        costs[j] = j;
    }

    for (int i = 1; i <= leftSize; ++i) {
        int previousDiagonal = costs[0];
        costs[0] = i;
        for (int j = 1; j <= rightSize; ++j) {
            const int temp = costs[j];
            const int substitution = previousDiagonal + (left.at(i - 1) == right.at(j - 1) ? 0 : 1);
            const int insertion = costs[j] + 1;
            const int deletion = costs[j - 1] + 1;
            costs[j] = std::min({substitution, insertion, deletion});
            previousDiagonal = temp;
        }
    }

    return costs[rightSize];
}

bool isWakeTokenVariant(const QString &token)
{
    static const QStringList variants = {
        QStringLiteral("vaxil"),
        QStringLiteral("vaksil"),
        QStringLiteral("vaxel")
    };

    if (variants.contains(token)) {
        return true;
    }

    if (!token.startsWith(QStringLiteral("va")) || token.size() < 4 || token.size() > 7) {
        return false;
    }

    for (const QString &variant : variants) {
        if (editDistance(token, variant) <= 1) {
            return true;
        }
    }

    return false;
}

bool detectWakeTokens(const QStringList &tokens, int *consumedPrefixTokens = nullptr)
{
    if (tokens.isEmpty()) {
        return false;
    }

    for (int i = 0; i < tokens.size(); ++i) {
        const QString &token = tokens.at(i);
        if (token == QStringLiteral("hey") && i + 1 < tokens.size() && isWakeTokenVariant(tokens.at(i + 1))) {
            if (consumedPrefixTokens && i == 0) {
                *consumedPrefixTokens = 2;
            }
            return true;
        }

        if (i == 0 && isWakeTokenVariant(token)) {
            if (consumedPrefixTokens) {
                *consumedPrefixTokens = 1;
            }
            return true;
        }
    }

    return false;
}

QString trimLeadingRoutingPunctuation(QString text)
{
    text = text.trimmed();
    while (!text.isEmpty() && QStringLiteral(",.!?:;").contains(text.front())) {
        text.remove(0, 1);
        text = text.trimmed();
    }
    return text;
}
}

bool WakeWordDetector::isWakeWordDetected(const std::string &transcript)
{
    return isWakeWordDetected(QString::fromStdString(transcript));
}

bool WakeWordDetector::isWakeWordDetected(const QString &transcript)
{
    return detectWakeTokens(normalizedTokens(transcript));
}

QString WakeWordDetector::normalizeTranscript(const QString &transcript)
{
    QString normalized = transcript.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    return normalized.simplified();
}

QString WakeWordDetector::stripWakeWordPrefix(const QString &transcript)
{
    const QString trimmed = transcript.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QStringList rawTokens = trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    const QStringList normalized = normalizedTokens(trimmed);
    if (rawTokens.isEmpty() || rawTokens.size() != normalized.size()) {
        return trimLeadingRoutingPunctuation(trimmed);
    }

    int consumedPrefixTokens = 0;
    if (!detectWakeTokens(normalized, &consumedPrefixTokens) || consumedPrefixTokens <= 0) {
        return trimLeadingRoutingPunctuation(trimmed);
    }

    return trimLeadingRoutingPunctuation(rawTokens.mid(consumedPrefixTokens).join(QStringLiteral(" ")));
}
