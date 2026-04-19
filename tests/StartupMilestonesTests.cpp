#include <QtTest>

#include "diagnostics/StartupMilestones.h"

class StartupMilestonesTests : public QObject
{
    Q_OBJECT

private slots:
    void startupSequenceContainsRequiredMilestones();
    void shutdownSequenceContainsRequiredMilestones();
};

void StartupMilestonesTests::startupSequenceContainsRequiredMilestones()
{
    const QStringList sequence = StartupMilestones::orderedStartupSequence();
    QVERIFY(sequence.contains(StartupMilestones::startupBegin()));
    QVERIFY(sequence.contains(StartupMilestones::startupLoggingReady()));
    QVERIFY(sequence.contains(StartupMilestones::startupOverlayBegin()));
    QVERIFY(sequence.contains(StartupMilestones::startupOverlayOk()));
    QVERIFY(sequence.contains(StartupMilestones::startupTtsBegin()));
    QVERIFY(sequence.contains(StartupMilestones::startupTtsOk()));
    QVERIFY(sequence.contains(StartupMilestones::startupWakeBegin()));
    QVERIFY(sequence.contains(StartupMilestones::startupWakeOk()));
    QVERIFY(sequence.contains(StartupMilestones::startupCompleted()));
}

void StartupMilestonesTests::shutdownSequenceContainsRequiredMilestones()
{
    const QStringList sequence = StartupMilestones::orderedShutdownSequence();
    QCOMPARE(sequence.size(), 2);
    QCOMPARE(sequence.first(), StartupMilestones::shutdownBegin());
    QCOMPARE(sequence.last(), StartupMilestones::shutdownCompleted());
}

QTEST_APPLESS_MAIN(StartupMilestonesTests)
#include "StartupMilestonesTests.moc"
