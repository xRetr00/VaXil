#include "vision/VisionContextGate.h"

#include <QRegularExpression>

namespace {
QString normalizeVisionInput(const QString &input)
{
    QString normalized = input.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    return normalized.simplified();
}

bool containsAnyPhrase(const QString &input, const QStringList &phrases)
{
    for (const QString &phrase : phrases) {
        if (input.contains(phrase)) {
            return true;
        }
    }
    return false;
}
}

bool VisionContextGate::shouldInject(const QString &input,
                                     IntentType intent,
                                     bool hasFreshSnapshot,
                                     bool explicitVisionModeEnabled,
                                     bool recentGestureAction)
{
    if (!hasFreshSnapshot) {
        return false;
    }

    if (explicitVisionModeEnabled || recentGestureAction) {
        return true;
    }

    if (intent == IntentType::GENERAL_CHAT) {
        return isVisionRelevantQuery(input);
    }

    return isVisionRelevantQuery(input);
}

bool VisionContextGate::isVisionRelevantQuery(const QString &input)
{
    const QString normalized = normalizeVisionInput(input);
    return containsAnyPhrase(normalized, {
        QStringLiteral("what do you see"),
        QStringLiteral("can you see"),
        QStringLiteral("do you see"),
        QStringLiteral("what am i holding"),
        QStringLiteral("am i holding"),
        QStringLiteral("holding"),
        QStringLiteral("what is this"),
        QStringLiteral("what is that"),
        QStringLiteral("look"),
        QStringLiteral("look at"),
        QStringLiteral("look around"),
        QStringLiteral("around me"),
        QStringLiteral("in front of me"),
        QStringLiteral("on my desk"),
        QStringLiteral("on the desk"),
        QStringLiteral("see"),
        QStringLiteral("camera"),
        QStringLiteral("gesture"),
        QStringLiteral("hand"),
        QStringLiteral("object"),
        QStringLiteral("environment"),
        QStringLiteral("room"),
        QStringLiteral("desk")
    });
}

bool VisionContextGate::needsRawVisionDetails(const QString &input)
{
    const QString normalized = normalizeVisionInput(input);
    return containsAnyPhrase(normalized, {
        QStringLiteral("which object"),
        QStringLiteral("what objects"),
        QStringLiteral("list objects"),
        QStringLiteral("what gesture"),
        QStringLiteral("which gesture"),
        QStringLiteral("show raw"),
        QStringLiteral("details"),
        QStringLiteral("confidence")
    });
}
