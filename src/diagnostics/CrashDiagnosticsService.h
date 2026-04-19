#pragma once

#include <QElapsedTimer>
#include <QMutex>
#include <QString>
#include <functional>

class CrashReportWriter;

struct CrashDiagnosticsConfig
{
    QString applicationName = QStringLiteral("vaxil");
    QString applicationVersion;
    QString buildInfo;
    QString logsRootPath;
    int breadcrumbCapacity = 200;
    bool enableWindowsMinidump = true;
};

class CrashDiagnosticsService
{
public:
    static CrashDiagnosticsService &instance();

    void initialize(const CrashDiagnosticsConfig &config);
    void installHandlers();

    void setLogCallback(
        std::function<void(const QString &severity,
                           const QString &channel,
                           const QString &message,
                           const QString &errorCode)> callback);
    void setFlushCallback(std::function<void()> callback);

    void markStartupMilestone(const QString &milestoneId,
                              const QString &detail = QString(),
                              bool success = true);
    void recordBreadcrumb(const QString &module,
                          const QString &event,
                          const QString &detail = QString(),
                          const QString &traceId = QString(),
                          const QString &sessionId = QString(),
                          const QString &threadId = QString());
    void updateRuntimeContext(const QString &module,
                              const QString &route,
                              const QString &tool,
                              const QString &traceId,
                              const QString &sessionId,
                              const QString &threadId);

    void captureQtFatal(const QString &message);
    void captureHandledException(const QString &module,
                                 const QString &errorCode,
                                 const QString &message);

    QString lastCrashReportPath() const;
    QString lastMinidumpPath() const;
    qint64 uptimeMs() const;

private:
    CrashDiagnosticsService() = default;

    static void terminateHandler();
    static void signalHandler(int signalNumber);

#ifdef Q_OS_WIN
    static long __stdcall windowsUnhandledExceptionFilter(struct _EXCEPTION_POINTERS *exceptionPointers);
    QString writeMiniDump(struct _EXCEPTION_POINTERS *exceptionPointers);
#endif

    void handleTerminate();
    void handleSignalCrash(int signalNumber);
    void handleWindowsCrash(const QString &reason,
                            const QString &errorCode,
                            const QString &exceptionCode,
#ifdef Q_OS_WIN
                            struct _EXCEPTION_POINTERS *exceptionPointers,
#else
                            void *exceptionPointers,
#endif
                            const QString &signalName,
                            const QString &crashType);

    CrashDiagnosticsConfig m_config;
    QElapsedTimer m_uptimeTimer;
    mutable QMutex m_mutex;
    QString m_lastCrashReportPath;
    QString m_lastMinidumpPath;
    QString m_runtimeModule;
    QString m_runtimeRoute;
    QString m_runtimeTool;
    QString m_runtimeTraceId;
    QString m_runtimeSessionId;
    QString m_runtimeThreadId;
    std::function<void(const QString &, const QString &, const QString &, const QString &)> m_logCallback;
    std::function<void()> m_flushCallback;
    bool m_initialized = false;
    bool m_handlersInstalled = false;
    bool m_crashCaptured = false;
};
