#include "connectors/CalendarIcsEventBuilder.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace {
struct CalendarCandidate
{
    QString summary;
    QString location;
    QDateTime startUtc;
    bool upcoming = false;
};

QDateTime parseIcsDateTime(QString value)
{
    value = value.trimmed();
    if (value.isEmpty()) {
        return {};
    }

    if (value.endsWith(QChar::fromLatin1('Z'))) {
        const QDateTime utc = QDateTime::fromString(value, QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
        return utc.isValid() ? utc.toUTC() : QDateTime{};
    }

    if (value.contains(QChar::fromLatin1('T'))) {
        const QDateTime local = QDateTime::fromString(value, QStringLiteral("yyyyMMdd'T'HHmmss"));
        return local.isValid() ? local.toUTC() : QDateTime{};
    }

    const QDate date = QDate::fromString(value, QStringLiteral("yyyyMMdd"));
    if (!date.isValid()) {
        return {};
    }
    return QDateTime(date, QTime(0, 0), Qt::UTC);
}

QString eventValue(const QStringList &lines, const QString &key)
{
    const QString prefix = key + QChar::fromLatin1(':');
    const QString paramPrefix = key + QChar::fromLatin1(';');
    for (const QString &line : lines) {
        if (line.startsWith(prefix, Qt::CaseInsensitive)) {
            return line.mid(prefix.size()).trimmed();
        }
        if (line.startsWith(paramPrefix, Qt::CaseInsensitive)) {
            const int colon = line.indexOf(QChar::fromLatin1(':'));
            if (colon > 0) {
                return line.mid(colon + 1).trimmed();
            }
        }
    }
    return {};
}

QStringList unfoldedLines(const QString &content)
{
    QStringList result;
    const QStringList rawLines = content.split(QRegularExpression(QStringLiteral("\\r?\\n")),
                                               Qt::KeepEmptyParts);
    for (const QString &rawLine : rawLines) {
        if (!result.isEmpty()
            && (rawLine.startsWith(QChar::fromLatin1(' ')) || rawLine.startsWith(QChar::fromLatin1('\t')))) {
            result.last().append(rawLine.mid(1));
            continue;
        }
        result.append(rawLine.trimmed());
    }
    return result;
}

std::optional<CalendarCandidate> bestCandidate(const QString &content)
{
    const QStringList lines = unfoldedLines(content);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    bool inEvent = false;
    QStringList eventLines;
    std::optional<CalendarCandidate> best;

    auto considerEvent = [&](const QStringList &candidateLines) {
        CalendarCandidate candidate;
        candidate.summary = eventValue(candidateLines, QStringLiteral("SUMMARY"));
        candidate.location = eventValue(candidateLines, QStringLiteral("LOCATION"));
        candidate.startUtc = parseIcsDateTime(eventValue(candidateLines, QStringLiteral("DTSTART")));
        if (candidate.summary.isEmpty() || !candidate.startUtc.isValid()) {
            return;
        }

        candidate.upcoming = candidate.startUtc >= nowUtc;
        if (!best.has_value()) {
            best = candidate;
            return;
        }

        if (candidate.upcoming != best->upcoming) {
            if (candidate.upcoming) {
                best = candidate;
            }
            return;
        }

        if (candidate.upcoming) {
            if (candidate.startUtc < best->startUtc) {
                best = candidate;
            }
            return;
        }

        if (candidate.startUtc > best->startUtc) {
            best = candidate;
        }
    };

    for (const QString &line : lines) {
        if (line.compare(QStringLiteral("BEGIN:VEVENT"), Qt::CaseInsensitive) == 0) {
            inEvent = true;
            eventLines.clear();
            continue;
        }
        if (line.compare(QStringLiteral("END:VEVENT"), Qt::CaseInsensitive) == 0) {
            if (inEvent) {
                considerEvent(eventLines);
            }
            inEvent = false;
            eventLines.clear();
            continue;
        }
        if (inEvent) {
            eventLines.append(line);
        }
    }

    return best;
}
}

ConnectorEvent CalendarIcsEventBuilder::fromFile(const QString &filePath,
                                                 const QDateTime &lastModifiedUtc,
                                                 const QString &defaultPriority)
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile() || !info.isReadable()) {
        return {};
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    const auto candidate = bestCandidate(content);
    if (!candidate.has_value()) {
        return {};
    }

    const QByteArray digest = QCryptographicHash::hash(
        (info.absoluteFilePath()
         + QChar::fromLatin1('|')
         + candidate->summary
         + QChar::fromLatin1('|')
         + candidate->startUtc.toString(Qt::ISODateWithMs)).toUtf8(),
        QCryptographicHash::Sha1).toHex();

    ConnectorEvent event;
    event.eventId = QString::fromLatin1(digest.left(16));
    event.sourceKind = QStringLiteral("connector_schedule_calendar");
    event.connectorKind = QStringLiteral("schedule");
    event.taskType = QStringLiteral("calendar_review");
    event.summary = QStringLiteral("Schedule updated: %1").arg(candidate->summary);
    event.taskKey = QStringLiteral("schedule:%1").arg(info.completeBaseName().trimmed().toLower());
    event.itemCount = 1;
    event.priority = defaultPriority.trimmed().isEmpty() ? QStringLiteral("medium") : defaultPriority.trimmed().toLower();
    event.occurredAtUtc = lastModifiedUtc.isValid() ? lastModifiedUtc : QDateTime::currentDateTimeUtc();
    event.metadata = {
        {QStringLiteral("producer"), QStringLiteral("connector_schedule_calendar_monitor")},
        {QStringLiteral("calendarPath"), info.absoluteFilePath()},
        {QStringLiteral("eventTitle"), candidate->summary},
        {QStringLiteral("eventStartUtc"), candidate->startUtc.toString(Qt::ISODateWithMs)},
        {QStringLiteral("eventUpcoming"), candidate->upcoming}
    };
    if (!candidate->location.isEmpty()) {
        event.metadata.insert(QStringLiteral("eventLocation"), candidate->location);
    }
    return event;
}
