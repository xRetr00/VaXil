#include <QtTest>

#include <QTemporaryDir>

#include "memory/MemoryStore.h"

class MemoryStoreTests : public QObject
{
    Q_OBJECT

private slots:
    void returnsConnectorMemoryRecordsForRelevantQuery();
};

void MemoryStoreTests::returnsConnectorMemoryRecordsForRelevantQuery()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(store.upsertConnectorState(QStringLiteral("connector:schedule:today.ics"), {
        {QStringLiteral("sourceKind"), QStringLiteral("connector_schedule_calendar")},
        {QStringLiteral("connectorKind"), QStringLiteral("schedule")},
        {QStringLiteral("seenCount"), 3},
        {QStringLiteral("presentedCount"), 1},
        {QStringLiteral("historyRecentlySeen"), true},
        {QStringLiteral("historyRecentlyPresented"), false},
        {QStringLiteral("lastSeenAtMs"), 1200}
    }));

    const QList<MemoryRecord> records = store.connectorMemory(QStringLiteral("schedule"));
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().source, QStringLiteral("connector_memory"));
    QCOMPARE(records.first().key, QStringLiteral("connector_history_schedule"));
    QVERIFY(records.first().value.contains(QStringLiteral("Schedule signals seen 3 times")));
}

QTEST_APPLESS_MAIN(MemoryStoreTests)
#include "MemoryStoreTests.moc"
