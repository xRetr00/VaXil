#include "core/LocalResponseEngine.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

namespace {
QHash<QString, QStringList> defaultResponses()
{
    return {
        {QStringLiteral("greeting_morning"), {
             QStringLiteral("Good morning, {user_name}. How can I help?"),
             QStringLiteral("Good morning, {user_name}. What do you need?"),
             QStringLiteral("Morning, {user_name}. How may I help?")
         }},
        {QStringLiteral("greeting_evening"), {
             QStringLiteral("Good evening, {user_name}. How can I assist?"),
             QStringLiteral("Good evening, {user_name}. What do you need?"),
             QStringLiteral("Evening, {user_name}. How may I help?")
         }},
        {QStringLiteral("small_talk"), {
             QStringLiteral("I'm here, {user_name}."),
             QStringLiteral("Ready when you are, {user_name}."),
             QStringLiteral("How can I help, {user_name}?")
         }},
        {QStringLiteral("wakeword_ready"), {
             QStringLiteral("Yes?"),
             QStringLiteral("I'm listening."),
             QStringLiteral("Ready.")
         }},
        {QStringLiteral("time_status"), {
             QStringLiteral("It is {current_time}."),
             QStringLiteral("The current time is {current_time}."),
             QStringLiteral("It is now {current_time}.")
         }},
        {QStringLiteral("date_status"), {
             QStringLiteral("Today is {current_date}."),
             QStringLiteral("The date is {current_date}."),
             QStringLiteral("It is {current_date} today.")
         }},
        {QStringLiteral("ai_offline"), {
             QStringLiteral("I'm currently unable to reach the AI core."),
             QStringLiteral("The AI core is offline right now."),
             QStringLiteral("I can't reach the AI core at the moment.")
         }},
        {QStringLiteral("error_timeout"), {
             QStringLiteral("The AI core did not answer in time."),
             QStringLiteral("That took too long to complete."),
             QStringLiteral("The request timed out.")
         }},
        {QStringLiteral("error_auth"), {
             QStringLiteral("The AI provider rejected the request credentials."),
             QStringLiteral("Authentication failed for the AI provider."),
             QStringLiteral("The AI provider needs valid credentials.")
         }},
        {QStringLiteral("error_capability"), {
             QStringLiteral("That mode is not available with the current provider or model."),
             QStringLiteral("The selected provider or model cannot handle that request mode."),
             QStringLiteral("The current provider setup cannot execute that capability.")
         }},
        {QStringLiteral("error_invalid"), {
             QStringLiteral("The backend returned an invalid response."),
             QStringLiteral("I received malformed output and stopped the request."),
             QStringLiteral("The response payload was invalid, so I stopped instead of guessing.")
         }},
        {QStringLiteral("error_transport"), {
             QStringLiteral("I couldn't complete that because the backend connection failed."),
             QStringLiteral("The backend connection failed before the request could finish."),
             QStringLiteral("A transport error interrupted the backend request.")
         }},
        {QStringLiteral("acknowledgement"), {
             QStringLiteral("Understood. Working on {target}."),
             QStringLiteral("All right. Handling {target}."),
             QStringLiteral("Done. {target} is handled.")
         }}
    };
}

QString sanitizeLocalResponse(QString text)
{
    static const QStringList forbiddenFragments = {
        QStringLiteral("tone"),
        QStringLiteral("addressing style"),
        QStringLiteral("system prompt"),
        QStringLiteral("internal prompt"),
        QStringLiteral("primary goals"),
        QStringLiteral("response contract"),
        QStringLiteral("local systems remain active"),
        QStringLiteral("operating normally"),
        QStringLiteral("running smoothly")
    };

    const QString lowered = text.toLower();
    for (const QString &fragment : forbiddenFragments) {
        if (lowered.contains(fragment)) {
            return QStringLiteral("How can I assist?");
        }
    }

    return text.simplified();
}
}

LocalResponseEngine::LocalResponseEngine(QObject *parent)
    : QObject(parent)
{
}

bool LocalResponseEngine::initialize()
{
    m_responses = defaultResponses();

    QFile file(QStringLiteral(":/qt/qml/VAXIL/gui/data/local_responses.json"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return !m_responses.isEmpty();
    }

    const auto document = QJsonDocument::fromJson(file.readAll());
    const auto root = document.object();
    m_responses.clear();
    for (auto it = root.begin(); it != root.end(); ++it) {
        QStringList variants;
        for (const auto &entry : it.value().toArray()) {
            variants.push_back(entry.toString());
        }
        m_responses.insert(it.key(), variants);
    }

    return !m_responses.isEmpty();
}

QString LocalResponseEngine::respondToIntent(LocalIntent intent, const LocalResponseContext &context)
{
    return renderTemplate(chooseVariant(resolveGroup(intent, context)), context);
}

QString LocalResponseEngine::respondToError(const QString &errorKey, const LocalResponseContext &context)
{
    return renderTemplate(chooseVariant(errorKey), context);
}

QString LocalResponseEngine::acknowledgement(const QString &target, const LocalResponseContext &context)
{
    return renderTemplate(chooseVariant(QStringLiteral("acknowledgement")), context, target);
}

QString LocalResponseEngine::wakeWordReady(const LocalResponseContext &context)
{
    return renderTemplate(chooseVariant(QStringLiteral("wakeword_ready")), context);
}

QString LocalResponseEngine::currentTimeResponse(const LocalResponseContext &context)
{
    return renderTemplate(chooseVariant(QStringLiteral("time_status")), context);
}

QString LocalResponseEngine::currentDateResponse(const LocalResponseContext &context)
{
    return renderTemplate(chooseVariant(QStringLiteral("date_status")), context);
}

QString LocalResponseEngine::resolveGroup(LocalIntent intent, const LocalResponseContext &context) const
{
    switch (intent) {
    case LocalIntent::Greeting:
        if (context.timeOfDay == QStringLiteral("morning")) {
            return QStringLiteral("greeting_morning");
        }
        if (context.timeOfDay == QStringLiteral("evening")) {
            return QStringLiteral("greeting_evening");
        }
        return QStringLiteral("small_talk");
    case LocalIntent::SmallTalk:
        return QStringLiteral("small_talk");
    default:
        return QStringLiteral("small_talk");
    }
}

QString LocalResponseEngine::renderTemplate(const QString &variant, const LocalResponseContext &context, const QString &target) const
{
    QString text = variant;
    const QString fallbackName = context.userName.isEmpty() ? QStringLiteral("sir") : context.userName;
    text.replace(QStringLiteral("{assistant_name}"), context.assistantName);
    text.replace(QStringLiteral("{user_name}"), fallbackName);
    text.replace(QStringLiteral("{time_of_day}"), context.timeOfDay);
    text.replace(QStringLiteral("{system_state}"), context.systemState);
    text.replace(QStringLiteral("{tone}"), context.tone);
    text.replace(QStringLiteral("{addressing_style}"), context.addressingStyle);
    text.replace(QStringLiteral("{target}"), target);
    text.replace(QStringLiteral("{current_time}"), context.currentTime);
    text.replace(QStringLiteral("{current_date}"), context.currentDate);
    text.replace(QStringLiteral("{wake_word}"), context.wakeWord);
    return sanitizeLocalResponse(text);
}

QString LocalResponseEngine::chooseVariant(const QString &group)
{
    const auto variants = m_responses.value(group);
    if (variants.isEmpty()) {
        return QStringLiteral("I am ready.");
    }

    if (variants.size() == 1) {
        return variants.first();
    }

    const int previousIndex = m_lastIndexByGroup.value(group, -1);
    int nextIndex = QRandomGenerator::global()->bounded(variants.size());
    if (variants.size() > 1 && nextIndex == previousIndex) {
        nextIndex = (nextIndex + 1) % variants.size();
    }

    m_lastIndexByGroup[group] = nextIndex;
    return variants.at(nextIndex);
}
