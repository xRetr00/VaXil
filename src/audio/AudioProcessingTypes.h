#pragma once

#include <array>

#include <QMetaType>
#include <QString>

struct AudioFrame
{
    static constexpr int kMaxSamples = 480;

    std::array<float, kMaxSamples> samples{};
    int sampleCount = 0;
    int sampleRate = 16000;
    int channels = 1;
    bool speechDetected = false;
    qint64 sequence = 0;
};

struct AudioProcessingConfig
{
    bool aecEnabled = true;
    bool noiseSuppressionEnabled = true;
    bool agcEnabled = true;
    bool rnnoiseEnabled = false;
    float vadSensitivity = 0.55f;
};

Q_DECLARE_METATYPE(AudioFrame)
Q_DECLARE_METATYPE(AudioProcessingConfig)
