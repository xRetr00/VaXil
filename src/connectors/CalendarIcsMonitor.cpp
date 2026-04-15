#include "connectors/CalendarIcsMonitor.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QStandardPaths>
#include <QTimer>

#include "connectors/CalendarIcsEventBuilder.h"

namespace {
constexpr int kPollIntervalMs = 5000;
}

CalendarIcsMonitor::CalendarIcsMonitor(QObject *parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
    , m_fileWatcher(new QFileSystemWatcher(this))
{
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &CalendarIcsMonitor::pollFiles);
    connect(m_fileWatcher, &QFileSystemWatcher::directoryChanged, this, &CalendarIcsMonitor::handleDirectoryChanged);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this, &CalendarIcsMonitor::handleFileChanged);
}

void CalendarIcsMonitor::start()
{
    QDir().mkpath(calendarRootPath());
    ensureWatchTargets();
    pollFiles();
    if (!m_pollTimer->isActive()) {
        m_pollTimer->start();
    }
}

void CalendarIcsMonitor::stop()
{
    m_pollTimer->stop();
    m_fileWatcher->removePaths(m_fileWatcher->files());
    m_fileWatcher->removePaths(m_fileWatcher->directories());
}

QString CalendarIcsMonitor::calendarRootPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + QStringLiteral("/Vaxil Calendar");
}

void CalendarIcsMonitor::ensureWatchTargets()
{
    const QString root = calendarRootPath();
    if (!m_fileWatcher->directories().contains(root)) {
        m_fileWatcher->addPath(root);
    }

    const QDir dir(root);
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.ics")},
                                                  QDir::Files | QDir::Readable,
                                                  QDir::Time);
    for (const QFileInfo &info : files) {
        const QString path = info.absoluteFilePath();
        if (!m_fileWatcher->files().contains(path)) {
            m_fileWatcher->addPath(path);
        }
    }
}

void CalendarIcsMonitor::handleDirectoryChanged(const QString &path)
{
    if (path != calendarRootPath()) {
        return;
    }
    ensureWatchTargets();
    pollFiles();
}

void CalendarIcsMonitor::handleFileChanged(const QString &path)
{
    Q_UNUSED(path)
    ensureWatchTargets();
    pollFiles();
}

void CalendarIcsMonitor::pollFiles()
{
    const QDir dir(calendarRootPath());
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.ics")},
                                                  QDir::Files | QDir::Readable,
                                                  QDir::Time);
    for (const QFileInfo &info : files) {
        const ConnectorEvent event = CalendarIcsEventBuilder::fromFile(
            info.absoluteFilePath(),
            info.lastModified().toUTC());
        if (!event.isValid()) {
            continue;
        }

        const QString path = info.absoluteFilePath();
        if (m_lastEventIdByPath.value(path) == event.eventId) {
            continue;
        }

        m_lastEventIdByPath.insert(path, event.eventId);
        emit connectorEventDetected(event);
    }
}
