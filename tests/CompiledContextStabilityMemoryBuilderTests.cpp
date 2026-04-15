#include <QtTest>

#include "cognition/CompiledContextStabilityMemoryBuilder.h"

class CompiledContextStabilityMemoryBuilderTests : public QObject
{
    Q_OBJECT

private slots:
    void returnsEmptyRecordWhenNotStable();
    void buildsContextRecordForStableSummary();
};

void CompiledContextStabilityMemoryBuilderTests::returnsEmptyRecordWhenNotStable()
{
    const MemoryRecord record = CompiledContextStabilityMemoryBuilder::build(
        QStringLiteral("conversation"),
        QStringLiteral("Context is still fresh."),
        {QStringLiteral("desktop_context_summary")},
        0,
        0);

    QVERIFY(record.key.isEmpty());
}

void CompiledContextStabilityMemoryBuilderTests::buildsContextRecordForStableSummary()
{
    const MemoryRecord record = CompiledContextStabilityMemoryBuilder::build(
        QStringLiteral("agent"),
        QStringLiteral("Context stable for 3 cycles over 90000 ms."),
        {QStringLiteral("desktop_context_summary"), QStringLiteral("connector_summary_schedule")},
        3,
        90000);

    QCOMPARE(record.type, QStringLiteral("context"));
    QCOMPARE(record.key, QStringLiteral("compiled_context_stability_agent"));
    QCOMPARE(record.source, QStringLiteral("compiled_context_stability"));
    QVERIFY(record.value.contains(QStringLiteral("Context stable for 3 cycles")));
    QVERIFY(record.value.contains(QStringLiteral("connector_summary_schedule")));
}

QTEST_APPLESS_MAIN(CompiledContextStabilityMemoryBuilderTests)
#include "CompiledContextStabilityMemoryBuilderTests.moc"
