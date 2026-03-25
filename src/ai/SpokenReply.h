#pragma once

#include <QString>

struct SpokenReply
{
    QString displayText;
    QString spokenText;
    bool shouldSpeak = true;
};

QString sanitizeDisplayText(const QString &input);
QString sanitizeSpokenText(const QString &input);
SpokenReply parseSpokenReply(const QString &input);
