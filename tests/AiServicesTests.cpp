#include <QtTest>

#include "ai/PromptAdapter.h"
#include "ai/StreamAssembler.h"

class AiServicesTests : public QObject
{
    Q_OBJECT

private slots:
    void promptAdapterAppliesNoThink();
    void promptAdapterAppliesDeepPrefix();
    void promptAdapterInjectsIdentityAndProfile();
    void streamAssemblerEmitsSentences();
};

void AiServicesTests::promptAdapterAppliesNoThink()
{
    PromptAdapter adapter;
    QCOMPARE(adapter.applyReasoningMode(QStringLiteral("turn on the light"), ReasoningMode::Fast),
             QStringLiteral("turn on the light /no_think"));
}

void AiServicesTests::promptAdapterAppliesDeepPrefix()
{
    PromptAdapter adapter;
    QVERIFY(adapter.applyReasoningMode(QStringLiteral("Explain this"), ReasoningMode::Deep)
                .startsWith(QStringLiteral("Think step by step before answering.\n")));
}

void AiServicesTests::promptAdapterInjectsIdentityAndProfile()
{
    PromptAdapter adapter;
    UserProfile profile;
    profile.userName = QStringLiteral("Alex");
    profile.preferences["theme"] = "dark";

    const auto messages = adapter.buildConversationMessages(
        QStringLiteral("Status report"),
        {},
        {},
        {
            .assistantName = QStringLiteral("JARVIS"),
            .personality = QStringLiteral("calm"),
            .tone = QStringLiteral("confident"),
            .addressingStyle = QStringLiteral("direct")
        },
        profile,
        ReasoningMode::Balanced);

    QVERIFY(messages.first().content.contains(QStringLiteral("You are JARVIS")));
    QVERIFY(messages.first().content.contains(QStringLiteral("name: Alex")));
    QVERIFY(messages.first().content.contains(QStringLiteral("theme = dark")));
}

void AiServicesTests::streamAssemblerEmitsSentences()
{
    StreamAssembler assembler;
    QSignalSpy spy(&assembler, &StreamAssembler::sentenceReady);

    assembler.appendChunk(QStringLiteral("Hello world. "));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("Hello world."));
}

QTEST_MAIN(AiServicesTests)
#include "AiServicesTests.moc"
