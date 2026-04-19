#include "CrashDiagnosticsService.h"

#include "CrashBreadcrumbTrail.h"
#include "CrashReportWriter.h"
#include "VaxilErrorCodes.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QThread>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#endif

namespace {
QString signalName(int signalNumber)
{
    switch (signalNumber) {
    case SIGABRT:
        return QStringLiteral("SIGABRT");
    case SIGSEGV:
        return QStringLiteral("SIGSEGV");
    case SIGILL:
        return QStringLiteral("SIGILL");
    case SIGFPE:
        return QStringLiteral("SIGFPE");
    case SIGTERM:
        return QStringLiteral("SIGTERM");
    default:
        return QStringLiteral("SIGNAL_%1").arg(signalNumber);
    }
}

qint64 currentThreadNumericId()
{
    return static_cast<qint64>(reinterpret_cast<quintptr>(QThread::currentThreadId()));
}

QString defaultLogsRootPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
}

QString defaultBuildInfo()
{
    return QStringLiteral("built=%1T%2Z")
        .arg(QString::fromLatin1(__DATE__))
        .arg(QString::fromLatin1(__TIME__));
}

QString formatExceptionCodeHex(quint32 code)
{
    return QStringLiteral("0x%1").arg(static_cast<qulonglong>(code), 8, 16, QLatin1Char('0')).toUpper();
}

QStringList relatedLogPaths(const QString &logsRootPath)
{
    const QStringList names{
        QStringLiteral("startup.log"),
        QStringLiteral("vaxil.log"),
        QStringLiteral("ai.log"),
        QStringLiteral("tool_audit.log"),
        QStringLiteral("route_audit.log"),
        QStringLiteral("tts.log"),
        QStringLiteral("stt.log"),
        QStringLiteral("wake_engine.log"),
        QStringLiteral("orb_render.log"),
        QStringLiteral("crash.log")
    };

    QStringList output;
    output.reserve(names.size());
    for (const QString &name : names) {
        const QString path = logsRootPath + QStringLiteral("/") + name;
        if (QFileInfo::exists(path)) {
            output.push_back(path);
        }
    }
    return output;
}
} // namespace

CrashDiagnosticsService &CrashDiagnosticsService::instance()
{
    static CrashDiagnosticsService service;
    return service;
}

void CrashDiagnosticsService::initialize(const CrashDiagnosticsConfig &config)
{
    QMutexLocker locker(&m_mutex);
    m_config = config;
    if (m_config.logsRootPath.trimmed().isEmpty()) {
        m_config.logsRootPath = defaultLogsRootPath();
    }
    if (m_config.buildInfo.trimmed().isEmpty()) {
        m_config.buildInfo = defaultBuildInfo();
    }

    QDir().mkpath(m_config.logsRootPath);
    QDir().mkpath(m_config.logsRootPath + QStringLiteral("/crash"));
    QDir().mkpath(m_config.logsRootPath + QStringLiteral("/dumps"));

    CrashBreadcrumbTrail::instance().setCapacity(m_config.breadcrumbCapacity);
    if (!m_uptimeTimer.isValid()) {
        m_uptimeTimer.start();
    }
    m_initialized = true;
}

void CrashDiagnosticsService::installHandlers()
{
    QMutexLocker locker(&m_mutex);
    if (m_handlersInstalled) {
        return;
    }

    std::set_terminate(&CrashDiagnosticsService::terminateHandler);
    std::signal(SIGABRT, &CrashDiagnosticsService::signalHandler);
    std::signal(SIGSEGV, &CrashDiagnosticsService::signalHandler);
    std::signal(SIGILL, &CrashDiagnosticsService::signalHandler);
    std::signal(SIGFPE, &CrashDiagnosticsService::signalHandler);
    std::signal(SIGTERM, &CrashDiagnosticsService::signalHandler);

#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(&CrashDiagnosticsService::windowsUnhandledExceptionFilter);
#endif

    m_handlersInstalled = true;
}

void CrashDiagnosticsService::setLogCallback(
    std::function<void(const QString &severity,
                       const QString &channel,
                       const QString &message,
                       const QString &errorCode)> callback)
{
    QMutexLocker locker(&m_mutex);
    m_logCallback = std::move(callback);
}

void CrashDiagnosticsService::setFlushCallback(std::function<void()> callback)
{
    QMutexLocker locker(&m_mutex);
    m_flushCallback = std::move(callback);
}

void CrashDiagnosticsService::markStartupMilestone(const QString &milestoneId,
                                                   const QString &detail,
                                                   bool success)
{
    const QString normalized = milestoneId.trimmed().isEmpty()
        ? QStringLiteral("startup.unknown")
        : milestoneId.trimmed();
    const QString status = success ? QStringLiteral("ok") : QStringLiteral("fail");

    recordBreadcrumb(QStringLiteral("startup"), normalized, detail);

    std::function<void(const QString &, const QString &, const QString &, const QString &)> callback;
    {
        QMutexLocker locker(&m_mutex);
        callback = m_logCallback;
    }
    if (callback) {
        callback(success ? QStringLiteral("INFO") : QStringLiteral("ERROR"),
                 QStringLiteral("startup"),
                 QStringLiteral("milestone=%1 status=%2 detail=\"%3\"")
                     .arg(normalized, status, detail),
                 QString());
    }
}

void CrashDiagnosticsService::recordBreadcrumb(const QString &module,
                                               const QString &event,
                                               const QString &detail,
                                               const QString &traceId,
                                               const QString &sessionId,
                                               const QString &threadId)
{
    CrashBreadcrumb crumb;
    crumb.timestampUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    crumb.module = module.trimmed().isEmpty() ? QStringLiteral("core") : module.trimmed();
    crumb.event = event.trimmed().isEmpty() ? QStringLiteral("event") : event.trimmed();
    crumb.detail = detail.trimmed();
    crumb.traceId = traceId.trimmed();
    crumb.sessionId = sessionId.trimmed();
    crumb.threadId = threadId.trimmed().isEmpty() ? QString::number(currentThreadNumericId()) : threadId.trimmed();
    crumb.uptimeMs = uptimeMs();
    CrashBreadcrumbTrail::instance().add(crumb);
}

void CrashDiagnosticsService::updateRuntimeContext(const QString &module,
                                                   const QString &route,
                                                   const QString &tool,
                                                   const QString &traceId,
                                                   const QString &sessionId,
                                                   const QString &threadId)
{
    QMutexLocker locker(&m_mutex);
    if (!module.trimmed().isEmpty()) {
        m_runtimeModule = module.trimmed();
    }
    if (!route.trimmed().isEmpty()) {
        m_runtimeRoute = route.trimmed();
    }
    if (!tool.trimmed().isEmpty()) {
        m_runtimeTool = tool.trimmed();
    }
    if (!traceId.trimmed().isEmpty()) {
        m_runtimeTraceId = traceId.trimmed();
    }
    if (!sessionId.trimmed().isEmpty()) {
        m_runtimeSessionId = sessionId.trimmed();
    }
    if (!threadId.trimmed().isEmpty()) {
        m_runtimeThreadId = threadId.trimmed();
    }
}

void CrashDiagnosticsService::captureQtFatal(const QString &message)
{
    handleWindowsCrash(message,
                       VaxilErrorCodes::forKey(VaxilErrorCodes::Key::CrashQtFatal),
                       QString(),
                       nullptr,
                       QStringLiteral("QtFatalMsg"),
                       QStringLiteral("qt_fatal"));
}

void CrashDiagnosticsService::captureHandledException(const QString &module,
                                                      const QString &errorCode,
                                                      const QString &message)
{
    recordBreadcrumb(module,
                     QStringLiteral("exception.handled"),
                     QStringLiteral("code=%1 message=%2").arg(errorCode, message));

    std::function<void(const QString &, const QString &, const QString &, const QString &)> callback;
    {
        QMutexLocker locker(&m_mutex);
        callback = m_logCallback;
    }
    if (callback) {
        callback(QStringLiteral("ERROR"), module, message, errorCode);
    }
}

QString CrashDiagnosticsService::lastCrashReportPath() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastCrashReportPath;
}

QString CrashDiagnosticsService::lastMinidumpPath() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastMinidumpPath;
}

qint64 CrashDiagnosticsService::uptimeMs() const
{
    return m_uptimeTimer.isValid() ? m_uptimeTimer.elapsed() : 0;
}

void CrashDiagnosticsService::terminateHandler()
{
    instance().handleTerminate();
    std::_Exit(EXIT_FAILURE);
}

void CrashDiagnosticsService::signalHandler(int signalNumber)
{
    instance().handleSignalCrash(signalNumber);
    std::_Exit(128 + signalNumber);
}

#ifdef Q_OS_WIN
long __stdcall CrashDiagnosticsService::windowsUnhandledExceptionFilter(EXCEPTION_POINTERS *exceptionPointers)
{
    const QString reason = QStringLiteral("Unhandled structured exception");
    const QString code = VaxilErrorCodes::forKey(VaxilErrorCodes::Key::CrashWindowsException);
    const QString exceptionCode = exceptionPointers && exceptionPointers->ExceptionRecord
        ? formatExceptionCodeHex(exceptionPointers->ExceptionRecord->ExceptionCode)
        : QStringLiteral("0x00000000");

    instance().handleWindowsCrash(reason,
                                  code,
                                  exceptionCode,
                                  exceptionPointers,
                                  QStringLiteral("SEH"),
                                  QStringLiteral("windows_exception"));
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void CrashDiagnosticsService::handleTerminate()
{
    QString reason = QStringLiteral("std::terminate invoked");
    try {
        const std::exception_ptr ex = std::current_exception();
        if (ex) {
            std::rethrow_exception(ex);
        }
    } catch (const std::exception &exception) {
        reason = QStringLiteral("std::terminate invoked: %1").arg(exception.what());
    } catch (...) {
        reason = QStringLiteral("std::terminate invoked with unknown exception");
    }

    handleWindowsCrash(reason,
                       VaxilErrorCodes::forKey(VaxilErrorCodes::Key::CrashTerminate),
                       QString(),
                       nullptr,
                       QStringLiteral("terminate"),
                       QStringLiteral("terminate"));
}

void CrashDiagnosticsService::handleSignalCrash(int signalNumber)
{
    const QString sigName = signalName(signalNumber);
    const QString reason = QStringLiteral("Fatal signal intercepted: %1").arg(sigName);
    handleWindowsCrash(reason,
                       VaxilErrorCodes::crashSignalCode(signalNumber),
                       QStringLiteral("signal=%1").arg(signalNumber),
                       nullptr,
                       sigName,
                       QStringLiteral("signal"));
}

void CrashDiagnosticsService::handleWindowsCrash(const QString &reason,
                                                 const QString &errorCode,
                                                 const QString &exceptionCode,
#ifdef Q_OS_WIN
                                                 EXCEPTION_POINTERS *exceptionPointers,
#else
                                                 void *exceptionPointers,
#endif
                                                 const QString &signalName,
                                                 const QString &crashType)
{
    Q_UNUSED(exceptionPointers)

    {
        QMutexLocker locker(&m_mutex);
        if (m_crashCaptured) {
            return;
        }
        m_crashCaptured = true;
    }

    recordBreadcrumb(QStringLiteral("crash"),
                     QStringLiteral("crash.captured"),
                     QStringLiteral("type=%1 code=%2 reason=%3").arg(crashType, errorCode, reason));

    std::function<void()> flushCallback;
    std::function<void(const QString &, const QString &, const QString &, const QString &)> logCallback;
    CrashDiagnosticsConfig config;
    CrashRuntimeContext runtimeContext;
    {
        QMutexLocker locker(&m_mutex);
        flushCallback = m_flushCallback;
        logCallback = m_logCallback;
        config = m_config;
        runtimeContext.module = m_runtimeModule;
        runtimeContext.route = m_runtimeRoute;
        runtimeContext.tool = m_runtimeTool;
        runtimeContext.traceId = m_runtimeTraceId;
        runtimeContext.sessionId = m_runtimeSessionId;
        runtimeContext.threadId = m_runtimeThreadId;
    }

    if (config.logsRootPath.trimmed().isEmpty()) {
        config.logsRootPath = defaultLogsRootPath();
    }

    if (flushCallback) {
        flushCallback();
    }

    CrashReportWriter writer(config.logsRootPath);

    QString minidumpPath;
#ifdef Q_OS_WIN
    if (config.enableWindowsMinidump) {
        minidumpPath = writeMiniDump(exceptionPointers);
    }
#endif

    const QList<CrashBreadcrumb> breadcrumbs = CrashBreadcrumbTrail::instance().snapshot(config.breadcrumbCapacity);
    writer.writeBreadcrumbSnapshot(breadcrumbs, reason);

    CrashReportInput input;
    input.timestampUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    input.applicationName = config.applicationName;
    input.applicationVersion = config.applicationVersion;
    input.buildInfo = config.buildInfo;
    input.crashType = crashType;
    input.errorCode = errorCode;
    input.reason = reason;
    input.exceptionCode = exceptionCode;
    input.signalName = signalName;
    input.processId = QCoreApplication::applicationPid();
    input.threadId = currentThreadNumericId();
    input.uptimeMs = uptimeMs();
    input.runtimeContext = runtimeContext;
    input.breadcrumbs = breadcrumbs;
    input.minidumpPath = minidumpPath;
    input.relatedLogs = relatedLogPaths(config.logsRootPath);

    const QString reportPath = writer.writeCrashReport(input);

    {
        QMutexLocker locker(&m_mutex);
        m_lastCrashReportPath = reportPath;
        m_lastMinidumpPath = minidumpPath;
    }

    if (logCallback) {
        logCallback(QStringLiteral("CRASH"),
                    QStringLiteral("crash"),
                    QStringLiteral("Crash diagnostics captured. report=%1 dump=%2 reason=%3")
                        .arg(reportPath, minidumpPath, reason),
                    errorCode);
    }
}

#ifdef Q_OS_WIN
QString CrashDiagnosticsService::writeMiniDump(EXCEPTION_POINTERS *exceptionPointers)
{
    CrashDiagnosticsConfig config;
    {
        QMutexLocker locker(&m_mutex);
        config = m_config;
    }

    CrashReportWriter writer(config.logsRootPath);
    QDir().mkpath(writer.dumpsDirectoryPath());

    const QString dumpFilePath = writer.dumpsDirectoryPath()
        + QStringLiteral("/")
        + config.applicationName
        + QStringLiteral("_")
        + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss"))
        + QStringLiteral(".dmp");

    HANDLE dumpFile = CreateFileW(
        reinterpret_cast<const wchar_t *>(dumpFilePath.utf16()),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (dumpFile == INVALID_HANDLE_VALUE) {
        return {};
    }

    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
    exceptionInfo.ThreadId = GetCurrentThreadId();
    exceptionInfo.ExceptionPointers = exceptionPointers;
    exceptionInfo.ClientPointers = FALSE;

    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(),
                                      GetCurrentProcessId(),
                                      dumpFile,
                                      static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithDataSegs),
                                      exceptionPointers ? &exceptionInfo : nullptr,
                                      nullptr,
                                      nullptr);
    CloseHandle(dumpFile);
    if (!ok) {
        return {};
    }

    return dumpFilePath;
}
#endif
