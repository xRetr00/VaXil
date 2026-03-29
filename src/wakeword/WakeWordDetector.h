#pragma once

#include <string>

#include <QString>

class WakeWordDetector
{
public:
    static bool isWakeWordDetected(const std::string &transcript);
    static bool isWakeWordDetected(const QString &transcript);
    static QString normalizeTranscript(const QString &transcript);
    static QString stripWakeWordPrefix(const QString &transcript);
};
