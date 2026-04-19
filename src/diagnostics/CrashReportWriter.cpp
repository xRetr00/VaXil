#include "CrashReportWriter.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStringConverter>
#include <QTextStream>

namespace {
QString timestampForFileName()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss"));
}

void writeBreadcrumbLines(QTextStream &stream, const QList<CrashBreadcrumb> &breadcrumbs)
{
    if (breadcrumbs.isEmpty()) {
        stream << "- no breadcrumbs captured\n";
        return;
    }

    for (const CrashBreadcrumb &breadcrumb : breadcrumbs) {
        stream << "- [" << breadcrumb.timestampUtc << "]"
               << " module=" << breadcrumb.module
               << " event=" << breadcrumb.event;
        if (!breadcrumb.detail.isEmpty()) {
            stream << " detail=\"" << breadcrumb.detail << "\"";
        }
        if (!breadcrumb.traceId.isEmpty()) {
            stream << " trace=" << breadcrumb.traceId;
        }
        if (!breadcrumb.sessionId.isEmpty()) {
            stream << " session=" << breadcrumb.sessionId;
        }
        if (!breadcrumb.threadId.isEmpty()) {
            stream << " thread=" << breadcrumb.threadId;
        }
        stream << " uptimeMs=" << breadcrumb.uptimeMs << "\n";
    }
}
}

CrashReportWriter::CrashReportWriter(const QString &logsRootPath)
    : m_logsRootPath(logsRootPath)
{
}

QString CrashReportWriter::writeCrashReport(const CrashReportInput &input) const
{
    QDir().mkpath(crashDirectoryPath());

    const QString filePath = crashDirectoryPath()
        + QStringLiteral("/crash_")
        + timestampForFileName()
        + QStringLiteral(".log");

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return {};
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "timestamp_utc=" << input.timestampUtc << "\n";
    stream << "application_name=" << input.applicationName << "\n";
    stream << "application_version=" << input.applicationVersion << "\n";
    stream << "build_info=" << input.buildInfo << "\n";
    stream << "process_id=" << input.processId << "\n";
    stream << "thread_id=" << input.threadId << "\n";
    stream << "uptime_ms=" << input.uptimeMs << "\n";
    stream << "crash_type=" << input.crashType << "\n";
    stream << "error_code=" << input.errorCode << "\n";
    stream << "reason=" << input.reason << "\n";
    stream << "exception_code=" << input.exceptionCode << "\n";
    stream << "signal=" << input.signalName << "\n";

    stream << "\n[runtime_context]\n";
    stream << "module=" << input.runtimeContext.module << "\n";
    stream << "route=" << input.runtimeContext.route << "\n";
    stream << "tool=" << input.runtimeContext.tool << "\n";
    stream << "trace_id=" << input.runtimeContext.traceId << "\n";
    stream << "session_id=" << input.runtimeContext.sessionId << "\n";
    stream << "thread_id=" << input.runtimeContext.threadId << "\n";

    stream << "\n[artifacts]\n";
    stream << "crash_report_path=" << filePath << "\n";
    stream << "minidump_path=" << input.minidumpPath << "\n";

    stream << "\n[related_logs]\n";
    if (input.relatedLogs.isEmpty()) {
        stream << "- none\n";
    } else {
        for (const QString &logPath : input.relatedLogs) {
            stream << "- " << logPath << "\n";
        }
    }

    stream << "\n[breadcrumbs]\n";
    writeBreadcrumbLines(stream, input.breadcrumbs);
    stream.flush();
    file.close();
    return filePath;
}

QString CrashReportWriter::writeBreadcrumbSnapshot(const QList<CrashBreadcrumb> &breadcrumbs,
                                                   const QString &reason) const
{
    QDir().mkpath(crashDirectoryPath());

    const QString filePath = crashDirectoryPath()
        + QStringLiteral("/breadcrumbs_")
        + timestampForFileName()
        + QStringLiteral(".log");

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return {};
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "reason=" << reason << "\n";
    stream << "timestamp_utc=" << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << "\n";
    stream << "\n[breadcrumbs]\n";
    writeBreadcrumbLines(stream, breadcrumbs);
    stream.flush();
    file.close();
    return filePath;
}

QString CrashReportWriter::crashDirectoryPath() const
{
    return m_logsRootPath + QStringLiteral("/crash");
}

QString CrashReportWriter::dumpsDirectoryPath() const
{
    return m_logsRootPath + QStringLiteral("/dumps");
}
