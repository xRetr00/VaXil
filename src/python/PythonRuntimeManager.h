#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QStringList>

class QProcess;
class LoggingService;

class PythonRuntimeManager : public QObject
{
    Q_OBJECT

public:
    explicit PythonRuntimeManager(const QStringList &allowedRoots = {},
                                  LoggingService *loggingService = nullptr,
                                  QObject *parent = nullptr);
    ~PythonRuntimeManager() override;

    bool isAvailable(QString *error = nullptr);
    QJsonArray listCatalog(QString *error = nullptr);
    bool supportsAction(const QString &name, QString *error = nullptr);
    QJsonObject executeAction(const QString &name,
                              const QJsonObject &args,
                              const QJsonObject &context = {},
                              QString *error = nullptr);
    QJsonArray listSkills(QString *error = nullptr);
    QJsonObject createSkill(const QString &id,
                            const QString &name,
                            const QString &description,
                            QString *error = nullptr);
    QJsonObject installSkill(const QString &url, QString *error = nullptr);
    void shutdown();

private:
    QString resolvePackagedRuntimeExecutable() const;
    QString resolvePythonExecutable() const;
    QString resolveRuntimeScriptPath() const;
    QString resolvePlaywrightBrowsersPath() const;
    QJsonObject makeInitializeParams() const;
    bool ensureStarted(QString *error = nullptr);
    QJsonObject call(const QString &method,
                     const QJsonObject &params,
                     int timeoutMs,
                     QString *error = nullptr);
    void invalidateCatalog();
    void logStdErr();

    QStringList m_allowedRoots;
    LoggingService *m_loggingService = nullptr;
    QPointer<QProcess> m_process;
    bool m_initialized = false;
    qint64 m_nextRequestId = 1;
    QJsonArray m_catalogCache;
    bool m_catalogLoaded = false;
};
