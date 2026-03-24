#include <QtTest>

#include "core/IntentRouter.h"
#include "core/LocalResponseEngine.h"

class LocalResponseEngineTests : public QObject
{
    Q_OBJECT

private slots:
    void classifiesGreeting();
    void classifiesCommand();
    void loadsAndRendersGreeting();
    void rendersWakeWordReady();
    void rendersCurrentTime();
    void avoidsImmediateRepetitionWhenVariantsExist();
};

void LocalResponseEngineTests::classifiesGreeting()
{
    IntentRouter router;
    QCOMPARE(router.classify(QStringLiteral("good morning jarvis")), LocalIntent::Greeting);
}

void LocalResponseEngineTests::classifiesCommand()
{
    IntentRouter router;
    QCOMPARE(router.classify(QStringLiteral("turn off the light")), LocalIntent::Command);
}

void LocalResponseEngineTests::loadsAndRendersGreeting()
{
    LocalResponseEngine engine;
    QVERIFY(engine.initialize());
    const QString text = engine.respondToIntent(LocalIntent::Greeting, {
        .userName = QStringLiteral("Alex"),
        .timeOfDay = QStringLiteral("morning"),
        .systemState = QStringLiteral("IDLE")
    });
    QVERIFY(text.contains(QStringLiteral("Alex")));
}

void LocalResponseEngineTests::rendersWakeWordReady()
{
    LocalResponseEngine engine;
    QVERIFY(engine.initialize());

    const QString text = engine.wakeWordReady({
        .assistantName = QStringLiteral("JARVIS"),
        .userName = QStringLiteral("Alex"),
        .timeOfDay = QStringLiteral("morning"),
        .systemState = QStringLiteral("IDLE"),
        .tone = QStringLiteral("confident"),
        .addressingStyle = QStringLiteral("direct"),
        .currentTime = QStringLiteral("09:30"),
        .currentDate = QStringLiteral("Monday, March 24, 2026"),
        .wakeWord = QStringLiteral("Jarvis")
    });

    QVERIFY(text.contains(QStringLiteral("Alex")) || text.contains(QStringLiteral("JARVIS")));
}

void LocalResponseEngineTests::rendersCurrentTime()
{
    LocalResponseEngine engine;
    QVERIFY(engine.initialize());

    const QString text = engine.currentTimeResponse({
        .assistantName = QStringLiteral("JARVIS"),
        .userName = QStringLiteral("Alex"),
        .timeOfDay = QStringLiteral("morning"),
        .systemState = QStringLiteral("IDLE"),
        .tone = QStringLiteral("confident"),
        .addressingStyle = QStringLiteral("direct"),
        .currentTime = QStringLiteral("09:30"),
        .currentDate = QStringLiteral("Monday, March 24, 2026"),
        .wakeWord = QStringLiteral("Jarvis")
    });

    QVERIFY(text.contains(QStringLiteral("09:30")));
}

void LocalResponseEngineTests::avoidsImmediateRepetitionWhenVariantsExist()
{
    LocalResponseEngine engine;
    QVERIFY(engine.initialize());

    const LocalResponseContext context{
        .userName = QStringLiteral("Alex"),
        .timeOfDay = QStringLiteral("evening"),
        .systemState = QStringLiteral("IDLE")
    };

    const QString first = engine.respondToError(QStringLiteral("ai_offline"), context);
    const QString second = engine.respondToError(QStringLiteral("ai_offline"), context);
    QVERIFY(first != second);
}

QTEST_APPLESS_MAIN(LocalResponseEngineTests)
#include "LocalResponseEngineTests.moc"
