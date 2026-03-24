#include <QStringList>

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
                       "Stay practical and directly useful.")
            .arg(identity.assistantName, identity.personality, identity.tone, identity.addressingStyle);

    systemPrompt += QStringLiteral("\nUser profile:");
    systemPrompt += QStringLiteral("\n- name: %1").arg(userProfile.userName.isEmpty() ? QStringLiteral("unknown") : userProfile.userName);
    systemPrompt += QStringLiteral("\n- preferences: %1").arg(profilePreferencesText(userProfile));

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
    return {
        {
            .role = QStringLiteral("system"),
            .content =
                QStringLiteral("You are %1. "
                               "The user name is %2. "
                               "You extract desktop assistant commands. "
                               "Return strict JSON only with keys: intent, target, action, confidence, args. "
                               "Use confidence from 0.0 to 1.0. If uncertain, return intent as \"unknown\".")
                    .arg(identity.assistantName,
                         userProfile.userName.isEmpty() ? QStringLiteral("unknown") : userProfile.userName)
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
