#include "core/IntentRouter.h"

IntentRouter::IntentRouter(QObject *parent)
    : QObject(parent)
    , m_greetingKeywords({
          QStringLiteral("hello"),
          QStringLiteral("hi"),
          QStringLiteral("hey"),
          QStringLiteral("good morning"),
          QStringLiteral("good evening"),
          QStringLiteral("good afternoon")
      })
    , m_smallTalkKeywords({
          QStringLiteral("how are you"),
          QStringLiteral("what's up"),
          QStringLiteral("whats up"),
          QStringLiteral("how's it going"),
          QStringLiteral("nice to meet you"),
          QStringLiteral("good to see you"),
          QStringLiteral("thank you"),
          QStringLiteral("thanks")
      })
    , m_commandKeywords({
          QStringLiteral("turn on"),
          QStringLiteral("turn off"),
          QStringLiteral("open"),
          QStringLiteral("close"),
          QStringLiteral("start"),
          QStringLiteral("stop"),
          QStringLiteral("launch"),
          QStringLiteral("mute"),
          QStringLiteral("unmute"),
          QStringLiteral("enable"),
          QStringLiteral("disable")
      })
    , m_complexKeywords({
          QStringLiteral("analyze"),
          QStringLiteral("analysis"),
          QStringLiteral("explain"),
          QStringLiteral("why"),
          QStringLiteral("strategy"),
          QStringLiteral("compare"),
          QStringLiteral("reason"),
          QStringLiteral("tradeoff"),
          QStringLiteral("design"),
          QStringLiteral("architecture")
      })
{
}

LocalIntent IntentRouter::classify(const QString &input) const
{
    const QString lowered = input.trimmed().toLower();
    if (lowered.isEmpty()) {
        return LocalIntent::Unknown;
    }

    for (const auto &keyword : m_greetingKeywords) {
        if (lowered == keyword || lowered.startsWith(keyword) || lowered.contains(keyword)) {
            return LocalIntent::Greeting;
        }
    }

    for (const auto &keyword : m_smallTalkKeywords) {
        if (lowered.contains(keyword)) {
            return LocalIntent::SmallTalk;
        }
    }

    for (const auto &keyword : m_commandKeywords) {
        if (lowered.startsWith(keyword) || lowered.contains(keyword)) {
            return LocalIntent::Command;
        }
    }

    for (const auto &keyword : m_complexKeywords) {
        if (lowered.contains(keyword)) {
            return LocalIntent::ComplexQuery;
        }
    }

    return lowered.size() > 70 ? LocalIntent::ComplexQuery : LocalIntent::Unknown;
}
