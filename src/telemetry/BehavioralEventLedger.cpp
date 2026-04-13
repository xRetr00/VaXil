#include "telemetry/BehavioralEventLedger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringConverter>
#include <QTextStream>
#include <QUuid>

namespace {
QString connectionName()
{
    return QStringLiteral("vaxil_behavior_ledger_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

BehaviorTraceEvent eventFromQuery(const QSqlQuery &query)
{
    BehaviorTraceEvent event;
    event.eventId = query.value(QStringLiteral("event_id")).toString();
    event.sessionId = query.value(QStringLiteral("session_id")).toString();
    event.traceId = query.value(QStringLiteral("trace_id")).toString();
    event.threadId = query.value(QStringLiteral("thread_id")).toString();
    event.capabilityId = query.value(QStringLiteral("capability_id")).toString();
    event.actor = query.value(QStringLiteral("actor")).toString();
    event.stage = query.value(QStringLiteral("stage")).toString();
    event.family = query.value(QStringLiteral("family")).toString();
    event.reasonCode = query.value(QStringLiteral("reason_code")).toString();
    event.timestampUtc = QDateTime::fromString(query.value(QStringLiteral("timestamp_utc")).toString(), Qt::ISODateWithMs);

    const QJsonDocument payload = QJsonDocument::fromJson(query.value(QStringLiteral("payload_json")).toByteArray());
    event.payload = payload.isObject() ? payload.object().toVariantMap() : QVariantMap{};
    return event;
}

constexpr auto kSchemaSql =
    "CREATE TABLE IF NOT EXISTS behavioral_events ("
    " event_id TEXT PRIMARY KEY,"
    " session_id TEXT,"
    " trace_id TEXT,"
    " thread_id TEXT,"
    " capability_id TEXT,"
    " actor TEXT NOT NULL,"
    " stage TEXT NOT NULL,"
    " family TEXT NOT NULL,"
    " reason_code TEXT NOT NULL,"
    " timestamp_utc TEXT NOT NULL,"
    " payload_json TEXT NOT NULL"
    ")";
}

BehavioralEventLedger::BehavioralEventLedger(QString rootPath)
    : m_rootPath(rootPath.trimmed())
{
    if (m_rootPath.isEmpty()) {
        m_rootPath = defaultRootPath();
    }

    m_databasePath = m_rootPath + QStringLiteral("/behavioral_events.sqlite");
    m_ndjsonPath = m_rootPath + QStringLiteral("/behavioral_events.ndjson");
}

bool BehavioralEventLedger::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (!ensureRootPathExists()) {
        return false;
    }

    QFile ndjsonFile(m_ndjsonPath);
    if (!ndjsonFile.exists()) {
        if (!ndjsonFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return false;
        }
        ndjsonFile.close();
    }

    const QString name = connectionName();
    bool schemaOk = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(m_databasePath);
        if (db.open()) {
            QSqlQuery query(db);
            schemaOk = query.exec(QString::fromUtf8(kSchemaSql));
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return schemaOk;
}

bool BehavioralEventLedger::recordEvent(const BehaviorTraceEvent &event) const
{
    QMutexLocker locker(&m_mutex);
    if (!ensureRootPathExists()) {
        return false;
    }

    const bool ndjsonOk = appendNdjsonLocked(event);
    const bool sqliteOk = recordSqliteLocked(event);
    return ndjsonOk || sqliteOk;
}

QList<BehaviorTraceEvent> BehavioralEventLedger::recentEvents(int limit) const
{
    QMutexLocker locker(&m_mutex);
    return recentEventsSqliteLocked(limit);
}

QString BehavioralEventLedger::databasePath() const
{
    return m_databasePath;
}

QString BehavioralEventLedger::ndjsonPath() const
{
    return m_ndjsonPath;
}

bool BehavioralEventLedger::ensureRootPathExists() const
{
    return QDir().mkpath(m_rootPath);
}

bool BehavioralEventLedger::appendNdjsonLocked(const BehaviorTraceEvent &event) const
{
    QFile file(m_ndjsonPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QJsonDocument::fromVariant(event.toVariantMap()).toJson(QJsonDocument::Compact) << '\n';
    stream.flush();
    file.close();
    return stream.status() == QTextStream::Ok;
}

bool BehavioralEventLedger::recordSqliteLocked(const BehaviorTraceEvent &event) const
{
    const QString name = connectionName();
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(m_databasePath);
        if (db.open()) {
            QSqlQuery schemaQuery(db);
            if (schemaQuery.exec(QString::fromUtf8(kSchemaSql))) {
                QSqlQuery insertQuery(db);
                insertQuery.prepare(QStringLiteral(
                    "INSERT OR REPLACE INTO behavioral_events ("
                    "event_id, session_id, trace_id, thread_id, capability_id, actor, stage, family, reason_code, timestamp_utc, payload_json"
                    ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
                insertQuery.addBindValue(event.eventId);
                insertQuery.addBindValue(event.sessionId);
                insertQuery.addBindValue(event.traceId);
                insertQuery.addBindValue(event.threadId);
                insertQuery.addBindValue(event.capabilityId);
                insertQuery.addBindValue(event.actor);
                insertQuery.addBindValue(event.stage);
                insertQuery.addBindValue(event.family);
                insertQuery.addBindValue(event.reasonCode);
                insertQuery.addBindValue(event.timestampUtc.toString(Qt::ISODateWithMs));
                insertQuery.addBindValue(payloadToJson(event.payload));
                ok = insertQuery.exec();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

QList<BehaviorTraceEvent> BehavioralEventLedger::recentEventsSqliteLocked(int limit) const
{
    QList<BehaviorTraceEvent> events;
    const QString name = connectionName();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(m_databasePath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral(
                "SELECT event_id, session_id, trace_id, thread_id, capability_id, actor, stage, family, reason_code, timestamp_utc, payload_json "
                "FROM behavioral_events ORDER BY timestamp_utc DESC LIMIT ?"));
            query.addBindValue(qMax(1, limit));
            if (query.exec()) {
                while (query.next()) {
                    events.push_back(eventFromQuery(query));
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return events;
}

QString BehavioralEventLedger::defaultRootPath() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/logs/behavior");
}

QString BehavioralEventLedger::payloadToJson(const QVariantMap &payload)
{
    return QString::fromUtf8(QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact));
}
