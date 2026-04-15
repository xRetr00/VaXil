#pragma once

#include <QDateTime>
#include <QString>

#include "companion/contracts/ConnectorEvent.h"

class CalendarIcsEventBuilder
{
public:
    [[nodiscard]] static ConnectorEvent fromFile(const QString &filePath,
                                                 const QDateTime &lastModifiedUtc,
                                                 const QString &defaultPriority = QStringLiteral("medium"));
};
