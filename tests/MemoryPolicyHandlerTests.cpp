#include <QtTest>

#include <QTemporaryDir>

#include "core/MemoryPolicyHandler.h"
#include "memory/MemoryStore.h"

class MemoryPolicyHandlerTests : public QObject
{
    Q_OBJECT

private slots:
    void requestMemoryIncludesConnectorSummaries();
};

void MemoryPolicyHandlerTests::requestMemoryIncludesConnectorSummaries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MemoryStore store(dir.path() + QStringLiteral("/memory.json"));
    MemoryPolicyHandler handler(nullptr, &store);

    QVERIFY(store.upsertConnectorState(QStringLiteral("connector:inbox:priority"), {
        {QStringLiteral("historyKey"), QStringLiteral("connector:inbox:priority")},
        {QStringLiteral("connectorKind"), QStringLiteral("inbox")},
        {QStringLiteral("sourceKind"), QStringLiteral("connector_inbox_maildrop")},
        {QStringLiteral("seenCount"), 5},
        {QStringLiteral("presentedCount"), 2},
        {QStringLiteral("historyRecentlySeen"), true},
        {QStringLiteral("historyRecentlyPresented"), true},
        {QStringLiteral("lastSeenAtMs"), 4800}
    }));

    const QList<MemoryRecord> memory = handler.requestMemory(QStringLiteral("inbox"), {});
    bool foundSummary = false;
    for (const MemoryRecord &record : memory) {
        if (record.source == QStringLiteral("connector_summary")
            && record.key == QStringLiteral("connector_summary_inbox")) {
            foundSummary = true;
            QVERIFY(record.value.contains(QStringLiteral("Inbox activity")));
        }
    }
    QVERIFY(foundSummary);
}

QTEST_APPLESS_MAIN(MemoryPolicyHandlerTests)
#include "MemoryPolicyHandlerTests.moc"
