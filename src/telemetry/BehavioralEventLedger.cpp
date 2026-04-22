#include "telemetry/BehavioralEventLedger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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
    event.turnId = query.value(QStringLiteral("turn_id")).toString();
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
    " turn_id TEXT,"
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

bool ensureColumnExists(QSqlDatabase &db, const QString &table, const QString &column, const QString &columnDef)
{
    QSqlQuery pragmaQuery(db);
    if (!pragmaQuery.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        return false;
    }

    bool exists = false;
    while (pragmaQuery.next()) {
        if (pragmaQuery.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) {
            exists = true;
            break;
        }
    }
    if (exists) {
        return true;
    }

    QSqlQuery alterQuery(db);
    return alterQuery.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                               .arg(table, column, columnDef));
}
}

BehavioralEventLedger::BehavioralEventLedger(QString rootPath, bool sqliteEnabled)
    : m_rootPath(rootPath.trimmed())
{
    m_connectionName = connectionName();
    m_sqliteEnabled = sqliteEnabled;
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

    QFile databaseFile(m_databasePath);
    if (!databaseFile.exists()) {
        if (!databaseFile.open(QIODevice::WriteOnly)) {
            return false;
        }
        databaseFile.close();
    }

    if (!m_sqliteEnabled || !QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        return true;
    }

    bool schemaOk = true;
    {
        QSqlDatabase db = QSqlDatabase::contains(m_connectionName)
            ? QSqlDatabase::database(m_connectionName)
            : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        db.setDatabaseName(m_databasePath);
        if (db.open()) {
            {
                QSqlQuery query(db);
                schemaOk = query.exec(QString::fromUtf8(kSchemaSql));
            }
            if (schemaOk) {
                schemaOk = ensureColumnExists(
                    db,
                    QStringLiteral("behavioral_events"),
                    QStringLiteral("turn_id"),
                    QStringLiteral("TEXT"));
            }
            db.close();
        }
    }
    return schemaOk;
}

bool BehavioralEventLedger::recordEvent(const BehaviorTraceEvent &event) const
{
    QMutexLocker locker(&m_mutex);
    if (!ensureRootPathExists()) {
        return false;
    }

    const bool ndjsonOk = appendNdjsonLocked(event);
    const bool sqliteOk = m_sqliteEnabled ? recordSqliteLocked(event) : false;
    return ndjsonOk || sqliteOk;
}

QList<BehaviorTraceEvent> BehavioralEventLedger::recentEvents(int limit) const
{
    QMutexLocker locker(&m_mutex);
    if (!m_sqliteEnabled || !QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        return {};
    }
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
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::contains(m_connectionName)
            ? QSqlDatabase::database(m_connectionName)
            : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        db.setDatabaseName(m_databasePath);
        if (db.open()) {
            bool schemaOk = false;
            {
                QSqlQuery schemaQuery(db);
                schemaOk = schemaQuery.exec(QString::fromUtf8(kSchemaSql));
            }
            if (schemaOk) {
                schemaOk = ensureColumnExists(
                    db,
                    QStringLiteral("behavioral_events"),
                    QStringLiteral("turn_id"),
                    QStringLiteral("TEXT"));
            }
            if (schemaOk) {
                QSqlQuery insertQuery(db);
                insertQuery.prepare(QStringLiteral(
                    "INSERT OR REPLACE INTO behavioral_events ("
                    "event_id, session_id, turn_id, trace_id, thread_id, capability_id, actor, stage, family, reason_code, timestamp_utc, payload_json"
                    ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
                insertQuery.addBindValue(event.eventId);
                insertQuery.addBindValue(event.sessionId);
                insertQuery.addBindValue(event.turnId);
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
    return ok;
}

QList<BehaviorTraceEvent> BehavioralEventLedger::recentEventsSqliteLocked(int limit) const
{
    QList<BehaviorTraceEvent> events;
    {
        QSqlDatabase db = QSqlDatabase::contains(m_connectionName)
            ? QSqlDatabase::database(m_connectionName)
            : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        db.setDatabaseName(m_databasePath);
        if (db.open()) {
            {
                QSqlQuery query(db);
                query.prepare(QStringLiteral(
                    "SELECT event_id, session_id, turn_id, trace_id, thread_id, capability_id, actor, stage, family, reason_code, timestamp_utc, payload_json "
                    "FROM behavioral_events ORDER BY timestamp_utc DESC LIMIT ?"));
                query.addBindValue(qMax(1, limit));
                if (query.exec()) {
                    while (query.next()) {
                        events.push_back(eventFromQuery(query));
                    }
                }
            }
            db.close();
        }
    }
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
