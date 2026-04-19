#pragma once

#include <QList>
#include <QString>

#include "CrashBreadcrumbTrail.h"

struct CrashRuntimeContext
{
    QString module;
    QString route;
    QString tool;
    QString traceId;
    QString sessionId;
    QString threadId;
};

struct CrashReportInput
{
    QString timestampUtc;
    QString applicationName;
    QString applicationVersion;
    QString buildInfo;
    QString crashType;
    QString errorCode;
    QString reason;
    QString exceptionCode;
    QString signalName;
    qint64 processId = 0;
    qint64 threadId = 0;
    qint64 uptimeMs = 0;
    CrashRuntimeContext runtimeContext;
    QList<CrashBreadcrumb> breadcrumbs;
    QString minidumpPath;
    QStringList relatedLogs;
};

class CrashReportWriter
{
public:
    explicit CrashReportWriter(const QString &logsRootPath);

    QString writeCrashReport(const CrashReportInput &input) const;
    QString writeBreadcrumbSnapshot(const QList<CrashBreadcrumb> &breadcrumbs,
                                    const QString &reason) const;
    QString crashDirectoryPath() const;
    QString dumpsDirectoryPath() const;

private:
    QString m_logsRootPath;
};
