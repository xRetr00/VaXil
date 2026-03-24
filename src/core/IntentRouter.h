#pragma once

#include <QObject>
#include <QStringList>

#include "core/AssistantTypes.h"

class IntentRouter : public QObject
{
    Q_OBJECT

public:
    explicit IntentRouter(QObject *parent = nullptr);

    LocalIntent classify(const QString &input) const;

private:
    QStringList m_greetingKeywords;
    QStringList m_smallTalkKeywords;
    QStringList m_commandKeywords;
    QStringList m_complexKeywords;
};
