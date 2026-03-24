#include <QStringList>
#include <QDateTime>
#include <QLocale>
#include <QTimeZone>

#include "ai/PromptAdapter.h"

namespace {
QString profilePreferencesText(const UserProfile &userProfile)
{
    if (!userProfile.preferences.is_object() || userProfile.preferences.empty()) {
        return QStringLiteral("none recorded");
    }

    QStringList lines;
    for (auto it = userProfile.preferences.begin(); it != userProfile.preferences.end(); ++it) {
        QString value;
        if (it.value().is_string()) {
            value = QString::fromStdString(it.value().get<std::string>());
        } else {
            value = QString::fromStdString(it.value().dump());
        }
        lines.push_back(QStringLiteral("%1 = %2").arg(QString::fromStdString(it.key()), value));
    }

    return lines.join(QStringLiteral("; "));
}

QString currentTimeContext()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QLocale locale = QLocale::system();

    return QStringLiteral("- local datetime: %1\n- timezone: %2")
        .arg(locale.toString(now, QLocale::LongFormat),
             QString::fromUtf8(now.timeZone().id()));
}

QString resolvedDisplayName(const UserProfile &userProfile)
{
    return userProfile.displayName.isEmpty() ? userProfile.userName : userProfile.displayName;
}

QString resolvedSpokenName(const UserProfile &userProfile)
{
    const QString displayName = resolvedDisplayName(userProfile);
    return userProfile.spokenName.isEmpty() ? displayName : userProfile.spokenName;
}
}

PromptAdapter::PromptAdapter(QObject *parent)
    : QObject(parent)
{
}

QList<AiMessage> PromptAdapter::buildConversationMessages(
    const QString &input,
    const QList<AiMessage> &history,
    const QList<MemoryRecord> &memory,
    const AssistantIdentity &identity,
    const UserProfile &userProfile,
    ReasoningMode mode) const
{
    QString systemPrompt =
        QStringLiteral("You are %1, a %2 desktop AI assistant. "
                       "Maintain a %3 tone. "
                       "Address the user with a %4 style. "
                       "Respond with concise, confident, minimal verbosity. "
                       "Use short, clean sentences. "
                       "Avoid filler words and long paragraphs. "
                       "When a reply may be spoken aloud, use natural punctuation for calm pauses. "
                       "Sound intelligent, precise, controlled, and directly useful.")
            .arg(identity.assistantName, identity.personality, identity.tone, identity.addressingStyle);

    const QString displayName = resolvedDisplayName(userProfile);
    const QString spokenName = resolvedSpokenName(userProfile);

    systemPrompt += QStringLiteral("\nUser profile:");
    systemPrompt += QStringLiteral("\n- display name: %1").arg(displayName.isEmpty() ? QStringLiteral("unknown") : displayName);
    systemPrompt += QStringLiteral("\n- spoken name: %1").arg(spokenName.isEmpty() ? QStringLiteral("unknown") : spokenName);
    systemPrompt += QStringLiteral("\n- preferences: %1").arg(profilePreferencesText(userProfile));
    systemPrompt += QStringLiteral("\nCurrent runtime context:");
    systemPrompt += QStringLiteral("\n%1").arg(currentTimeContext());
    systemPrompt += QStringLiteral("\n- wake phrase: Jarvis");

    if (!memory.isEmpty()) {
        systemPrompt += QStringLiteral("\nRelevant user memory:");
        for (const auto &record : memory) {
            systemPrompt += QStringLiteral("\n- %1: %2 = %3")
                                .arg(record.type, record.key, record.value);
        }
    }

    QList<AiMessage> messages{
        {.role = QStringLiteral("system"), .content = systemPrompt}
    };

    for (const auto &item : history) {
        messages.push_back(item);
    }

    messages.push_back({
        .role = QStringLiteral("user"),
        .content = applyReasoningMode(input, mode)
    });

    return messages;
}

QList<AiMessage> PromptAdapter::buildCommandMessages(
    const QString &input,
    const AssistantIdentity &identity,
    const UserProfile &userProfile,
    ReasoningMode mode) const
{
    const QString displayName = resolvedDisplayName(userProfile);
    const QString spokenName = resolvedSpokenName(userProfile);

    return {
        {
            .role = QStringLiteral("system"),
            .content =
                QStringLiteral("You are %1. "
                               "The user display name is %2. "
                               "The user spoken name is %3. "
                               "You extract desktop assistant commands. "
                               "Current runtime context:\n%4\n"
                               "Return strict JSON only with keys: intent, target, action, confidence, args. "
                               "Use confidence from 0.0 to 1.0. If uncertain, return intent as \"unknown\".")
                    .arg(identity.assistantName,
                         displayName.isEmpty() ? QStringLiteral("unknown") : displayName,
                         spokenName.isEmpty() ? QStringLiteral("unknown") : spokenName,
                         currentTimeContext())
        },
        {
            .role = QStringLiteral("user"),
            .content = applyReasoningMode(input, mode)
        }
    };
}

QString PromptAdapter::applyReasoningMode(const QString &input, ReasoningMode mode) const
{
    switch (mode) {
    case ReasoningMode::Fast:
        return input + QStringLiteral(" /no_think");
    case ReasoningMode::Deep:
        return QStringLiteral("Think step by step before answering.\n") + input;
    case ReasoningMode::Balanced:
    default:
        return input;
    }
}
