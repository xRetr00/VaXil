#pragma once

#include <QHash>
#include <QObject>
#include <memory>
#include <vector>

#include "core/agent/IntentDetector.h"

class LoggingService;
class AppSettings;

class IntentEngine : public QObject
{
    Q_OBJECT

public:
    explicit IntentEngine(AppSettings *settings, LoggingService *loggingService = nullptr, QObject *parent = nullptr);
    ~IntentEngine() override;

    IntentResult classify(const QString &text);
    bool isReady() const;
    QString modelPath() const;

public slots:
    void reloadModel();

private:
    struct Impl;

    struct CachedIntentResult {
        IntentResult result;
        qint64 createdAtMs = 0;
    };

    IntentResult classifyWithModel(const QString &text);
    IntentResult classifyWithHeuristics(const QString &text) const;
    QString normalizeText(const QString &text) const;
    void encodeFeatures(const QString &text, std::vector<float> &buffer) const;
    QString resolveModelPath() const;

    AppSettings *m_settings = nullptr;
    LoggingService *m_loggingService = nullptr;
    QHash<QString, CachedIntentResult> m_cache;
    std::unique_ptr<Impl> m_impl;
    QString m_modelPath;
};
