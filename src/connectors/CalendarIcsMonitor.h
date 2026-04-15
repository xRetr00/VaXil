#pragma once

#include <QHash>
#include <QObject>

#include "companion/contracts/ConnectorEvent.h"

class QFileSystemWatcher;
class QTimer;

class CalendarIcsMonitor : public QObject
{
    Q_OBJECT

public:
    explicit CalendarIcsMonitor(QObject *parent = nullptr);

    void start();
    void stop();
    [[nodiscard]] QString calendarRootPath() const;

signals:
    void connectorEventDetected(const ConnectorEvent &event);

private:
    void ensureWatchTargets();
    void handleDirectoryChanged(const QString &path);
    void handleFileChanged(const QString &path);
    void pollFiles();

    QTimer *m_pollTimer = nullptr;
    QFileSystemWatcher *m_fileWatcher = nullptr;
    QHash<QString, QString> m_lastEventIdByPath;
};
