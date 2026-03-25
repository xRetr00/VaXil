#include <QtTest>

#include "ai/PromptAdapter.h"
#include "ai/SpokenReply.h"
#include "ai/StreamAssembler.h"

class AiServicesTests : public QObject
{
    Q_OBJECT

private slots:
    void promptAdapterAppliesNoThink();
    void promptAdapterAppliesDeepPrefix();
    void promptAdapterInjectsIdentityAndProfile();
    void promptAdapterInjectsRuntimeContext();
    void spokenReplyParsesStructuredPayload();
    void spokenReplyFallsBackToSanitizedPlainText();
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
    profile.displayName = QStringLiteral("Alex");
    profile.spokenName = QStringLiteral("Alexander");
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
    QVERIFY(messages.first().content.contains(QStringLiteral("display name: Alex")));
    QVERIFY(messages.first().content.contains(QStringLiteral("spoken name: Alexander")));
    QVERIFY(messages.first().content.contains(QStringLiteral("theme = dark")));
}

void AiServicesTests::promptAdapterInjectsRuntimeContext()
{
    PromptAdapter adapter;

    const auto messages = adapter.buildConversationMessages(
        QStringLiteral("What time is it?"),
        {},
        {},
        {
            .assistantName = QStringLiteral("JARVIS"),
            .personality = QStringLiteral("calm"),
            .tone = QStringLiteral("confident"),
            .addressingStyle = QStringLiteral("direct")
        },
        {},
        ReasoningMode::Balanced);

    QVERIFY(messages.first().content.contains(QStringLiteral("Current runtime context:")));
    QVERIFY(messages.first().content.contains(QStringLiteral("wake phrase: Jarvis")));
    QVERIFY(messages.first().content.contains(QStringLiteral("timezone:")));
    QVERIFY(messages.first().content.contains(QStringLiteral("Spoken-safe output only")));
}

void AiServicesTests::spokenReplyParsesStructuredPayload()
{
    const SpokenReply reply = parseSpokenReply(QStringLiteral(R"({
        "display_text": "Hello there.",
        "spoken_text": "Hello there.",
        "should_speak": true
    })"));

    QCOMPARE(reply.displayText, QStringLiteral("Hello there."));
    QCOMPARE(reply.spokenText, QStringLiteral("Hello there."));
    QVERIFY(reply.shouldSpeak);
}

void AiServicesTests::spokenReplyFallsBackToSanitizedPlainText()
{
    const SpokenReply reply = parseSpokenReply(QStringLiteral("<think>secret</think> assistant: Hello 😄 https://example.com"));

    QCOMPARE(reply.displayText, QStringLiteral("Hello"));
    QCOMPARE(reply.spokenText, QStringLiteral("Hello."));
    QVERIFY(reply.shouldSpeak);
}

void AiServicesTests::streamAssemblerEmitsSentences()
{
    StreamAssembler assembler;
    QSignalSpy spy(&assembler, &StreamAssembler::sentenceReady);

    assembler.appendChunk(QStringLiteral("Hello world. "));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("Hello world."));
}

QTEST_APPLESS_MAIN(AiServicesTests)
#include "AiServicesTests.moc"
