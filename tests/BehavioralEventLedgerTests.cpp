#include <QtTest>

#include <QFileInfo>
#include <QTemporaryDir>

#include "telemetry/BehavioralEventLedger.h"

class BehavioralEventLedgerTests : public QObject
{
    Q_OBJECT

private slots:
    void writesNdjsonAndSqliteArtifacts();
    void returnsRecentEventsNewestFirst();
};

void BehavioralEventLedgerTests::writesNdjsonAndSqliteArtifacts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    BehavioralEventLedger ledger(dir.path());
    QVERIFY(ledger.initialize());

    BehaviorTraceEvent event = BehaviorTraceEvent::create(
        QStringLiteral("focus_mode"),
        QStringLiteral("state_transition"),
        QStringLiteral("focus_mode.enabled"),
        {
            { QStringLiteral("enabled"), true },
            { QStringLiteral("durationMinutes"), 60 }
        });
    event.sessionId = QStringLiteral("session-a");
    event.traceId = QStringLiteral("trace-a");
    event.threadId = QStringLiteral("thread-a");

    QVERIFY(ledger.recordEvent(event));
    QVERIFY(QFileInfo::exists(ledger.ndjsonPath()));
    QVERIFY(QFileInfo::exists(ledger.databasePath()));
}

void BehavioralEventLedgerTests::returnsRecentEventsNewestFirst()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    BehavioralEventLedger ledger(dir.path());
    QVERIFY(ledger.initialize());

    BehaviorTraceEvent older = BehaviorTraceEvent::create(
        QStringLiteral("cooldown"),
        QStringLiteral("decision"),
        QStringLiteral("cooldown.clear"));
    older.eventId = QStringLiteral("older");
    older.timestampUtc = QDateTime::fromString(QStringLiteral("2026-01-01T10:00:00.000Z"), Qt::ISODateWithMs);

    BehaviorTraceEvent newer = BehaviorTraceEvent::create(
        QStringLiteral("cooldown"),
        QStringLiteral("decision"),
        QStringLiteral("cooldown.break_high_novelty"));
    newer.eventId = QStringLiteral("newer");
    newer.timestampUtc = QDateTime::fromString(QStringLiteral("2026-01-01T10:05:00.000Z"), Qt::ISODateWithMs);

    QVERIFY(ledger.recordEvent(older));
    QVERIFY(ledger.recordEvent(newer));

    const QList<BehaviorTraceEvent> events = ledger.recentEvents(10);
    QVERIFY(events.size() >= 2);
    QCOMPARE(events.first().eventId, QStringLiteral("newer"));
}

QTEST_APPLESS_MAIN(BehavioralEventLedgerTests)
#include "BehavioralEventLedgerTests.moc"
