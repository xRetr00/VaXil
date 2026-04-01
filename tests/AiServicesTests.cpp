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
    void promptAdapterInjectsVisionContext();
    void promptAdapterUsesCanonicalToolCallEnvelope();
    void promptAdapterSelectsComputerToolsForGeneralChat();
    void promptAdapterPrefersPlaywrightForBrowserRequests();
    void spokenReplyParsesStructuredPayload();
    void spokenReplyFallsBackToSanitizedPlainText();
    void spokenReplyStripsUnclosedThinkBlocks();
    void spokenReplySuppressesStatusOnlySpeech();
    void spokenReplyTruncatesLongSpeechForPlayback();
    void streamAssemblerEmitsSentences();
};

void AiServicesTests::promptAdapterAppliesNoThink()
{
    PromptAdapter adapter;
    QCOMPARE(adapter.applyReasoningMode(QStringLiteral("turn on the light"), ReasoningMode::Fast),
             QStringLiteral("[FAST MODE] turn on the light"));
}

void AiServicesTests::promptAdapterAppliesDeepPrefix()
{
    PromptAdapter adapter;
    QCOMPARE(adapter.applyReasoningMode(QStringLiteral("Explain this"), ReasoningMode::Deep),
             QStringLiteral("[DEEP MODE] Explain this"));
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
            .assistantName = QStringLiteral("Vaxil"),
            .personality = QStringLiteral("calm"),
            .tone = QStringLiteral("confident"),
            .addressingStyle = QStringLiteral("direct")
        },
        profile,
        ReasoningMode::Balanced);

    QVERIFY(messages.first().content.contains(QStringLiteral("You are Vaxil")));
    QVERIFY(messages.first().content.contains(QStringLiteral("User name: Alex")));
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
            .assistantName = QStringLiteral("Vaxil"),
            .personality = QStringLiteral("calm"),
            .tone = QStringLiteral("confident"),
            .addressingStyle = QStringLiteral("direct")
        },
        {},
        ReasoningMode::Balanced);

    QVERIFY(messages.first().content.contains(QStringLiteral("Runtime:")));
    QVERIFY(messages.first().content.contains(QStringLiteral("wake phrase: Hey Vaxil")));
    QVERIFY(messages.first().content.contains(QStringLiteral("timezone:")));
    QVERIFY(messages.first().content.contains(QStringLiteral("Spoken-safe output only")));
}

void AiServicesTests::promptAdapterInjectsVisionContext()
{
    PromptAdapter adapter;

    const auto messages = adapter.buildConversationMessages(
        QStringLiteral("What am I holding?"),
        {},
        {},
        {
            .assistantName = QStringLiteral("Vaxil"),
            .personality = QStringLiteral("calm"),
            .tone = QStringLiteral("confident"),
            .addressingStyle = QStringLiteral("direct")
        },
        {},
        ReasoningMode::Balanced,
        QStringLiteral("User appears to be holding a red can"));

    QVERIFY(messages.first().content.contains(QStringLiteral("current scene summary: User appears to be holding a red can")));
}

void AiServicesTests::promptAdapterUsesCanonicalToolCallEnvelope()
{
    PromptAdapter adapter;

    const auto messages = adapter.buildHybridAgentMessages(
        QStringLiteral("list files in the project"),
        {},
        {
            .assistantName = QStringLiteral("Vaxil"),
            .personality = QStringLiteral("calm"),
            .tone = QStringLiteral("confident"),
            .addressingStyle = QStringLiteral("direct")
        },
        {},
        QStringLiteral("D:/Vaxil"),
        IntentType::LIST_FILES,
        {{QStringLiteral("dir_list"), QStringLiteral("List a directory"), nlohmann::json::object()}},
        ReasoningMode::Balanced);

    QVERIFY(messages.first().content.contains(QStringLiteral("tool_calls")));
    QVERIFY(!messages.first().content.contains(QStringLiteral("background_tasks")));
    QVERIFY(messages.first().content.contains(QStringLiteral("<workspace_root>")));
}

void AiServicesTests::promptAdapterSelectsComputerToolsForGeneralChat()
{
    PromptAdapter adapter;
    const QList<AgentToolSpec> tools = {
        {QStringLiteral("browser_open"), {}, {}},
        {QStringLiteral("dir_list"), {}, {}},
        {QStringLiteral("file_search"), {}, {}},
        {QStringLiteral("memory_search"), {}, {}},
        {QStringLiteral("web_search"), {}, {}},
        {QStringLiteral("computer_open_url"), {}, {}},
        {QStringLiteral("computer_open_app"), {}, {}},
        {QStringLiteral("computer_write_file"), {}, {}},
        {QStringLiteral("computer_set_timer"), {}, {}}
    };

    const QList<AgentToolSpec> selected = adapter.getRelevantTools(
        QStringLiteral("open the browser and search the web"),
        IntentType::GENERAL_CHAT,
        tools);
    QStringList names;
    for (const auto &tool : selected) {
        names.push_back(tool.name);
    }

    QVERIFY(names.contains(QStringLiteral("browser_open")));
    QVERIFY(names.contains(QStringLiteral("dir_list")));
    QVERIFY(names.contains(QStringLiteral("file_search")));
    QVERIFY(names.contains(QStringLiteral("memory_search")));
    QVERIFY(names.contains(QStringLiteral("web_search")));
    QVERIFY(names.contains(QStringLiteral("computer_open_url")));
    QVERIFY(names.contains(QStringLiteral("computer_open_app")));
    QVERIFY(names.contains(QStringLiteral("computer_write_file")));
    QVERIFY(names.contains(QStringLiteral("computer_set_timer")));
}

void AiServicesTests::promptAdapterPrefersPlaywrightForBrowserRequests()
{
    PromptAdapter adapter;

    const auto messages = adapter.buildConversationMessages(
        QStringLiteral("Open the browser"),
        {},
        {},
        {
            .assistantName = QStringLiteral("Vaxil"),
            .personality = QStringLiteral("calm"),
            .tone = QStringLiteral("confident"),
            .addressingStyle = QStringLiteral("direct")
        },
        {},
        ReasoningMode::Balanced);

    QVERIFY(messages.first().content.contains(QStringLiteral("browser_open should be the first choice")));
    QVERIFY(messages.first().content.contains(QStringLiteral("computer_open_url is the fallback")));
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

void AiServicesTests::spokenReplyStripsUnclosedThinkBlocks()
{
    const SpokenReply reply = parseSpokenReply(QStringLiteral("<think>draft reasoning assistant: Final answer"));

    QCOMPARE(reply.displayText, QStringLiteral("Final answer"));
    QCOMPARE(reply.spokenText, QStringLiteral("Final answer."));
    QVERIFY(reply.shouldSpeak);
}

void AiServicesTests::spokenReplySuppressesStatusOnlySpeech()
{
    const SpokenReply reply = parseSpokenReply(QStringLiteral("Listening."));

    QCOMPARE(reply.displayText, QStringLiteral("Listening."));
    QVERIFY(reply.spokenText.isEmpty());
    QVERIFY(!reply.shouldSpeak);
}

void AiServicesTests::spokenReplyTruncatesLongSpeechForPlayback()
{
    const SpokenReply reply = parseSpokenReply(
        QStringLiteral("First sentence explains the result. "
                       "Second sentence gives a little more detail. "
                       "Third sentence still fits the spoken summary. "
                       "Fourth sentence should stay on screen only. "
                       "[00:00:00] Timestamp noise should not be spoken."));

    QVERIFY(reply.shouldSpeak);
    QVERIFY(reply.spokenText.contains(QStringLiteral("The rest is on screen.")));
    QVERIFY(!reply.spokenText.contains(QStringLiteral("Fourth sentence should stay on screen only.")));
    QVERIFY(!reply.spokenText.contains(QStringLiteral("00:00:00")));
    QVERIFY(reply.displayText.contains(QStringLiteral("Fourth sentence should stay on screen only.")));
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
