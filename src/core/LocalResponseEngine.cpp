#include "core/LocalResponseEngine.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

LocalResponseEngine::LocalResponseEngine(QObject *parent)
    : QObject(parent)
{
}

bool LocalResponseEngine::initialize()
{
    QFile file(QStringLiteral(":/qt/qml/JARVIS/gui/data/local_responses.json"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
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
    const QString fallbackName = context.userName.isEmpty() ? QStringLiteral("there") : context.userName;
    text.replace(QStringLiteral("{assistant_name}"), context.assistantName);
    text.replace(QStringLiteral("{user_name}"), fallbackName);
    text.replace(QStringLiteral("{time_of_day}"), context.timeOfDay);
    text.replace(QStringLiteral("{system_state}"), context.systemState);
    text.replace(QStringLiteral("{tone}"), context.tone);
    text.replace(QStringLiteral("{addressing_style}"), context.addressingStyle);
    text.replace(QStringLiteral("{target}"), target);
    return text;
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
