#include <QtTest>

#include "ai/PromptAdapter.h"
#include "ai/SpokenReply.h"
#include "ai/StreamAssembler.h"
#include "core/tools/WebSearchQueryBuilder.h"

class AiServicesTests : public QObject
{
    Q_OBJECT

private slots:
    void promptAdapterAppliesNoThink();
    void promptAdapterAppliesDeepPrefix();
    void promptAdapterInjectsIdentityAndProfile();
    void promptAdapterInjectsRuntimeContext();
    void promptAdapterInjectsVisionContext();
    void promptAdapterStructuresMemoryByLane();
    void promptAdapterUsesCanonicalToolCallEnvelope();
    void promptAdapterBuildsHybridContinuationEnvelope();
    void promptAdapterSelectsComputerToolsForGeneralChat();
    void promptAdapterPrefersPlaywrightForBrowserRequests();
    void promptAdapterForbidsFalseCapabilityDenialsAndHonorifics();
    void spokenReplyParsesStructuredPayload();
    void spokenReplyFallsBackToSanitizedPlainText();
    void spokenReplyStripsUnclosedThinkBlocks();
    void spokenReplyStripsModelWrapperTags();
    void spokenReplyStripsPromptLeakageLines();
    void spokenReplySuppressesStatusOnlySpeech();
    void spokenReplyTruncatesLongSpeechForPlayback();
    void webSearchQueryBuilderRemovesFillerAndAddsFreshYear();
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
        ResponseMode::Chat,
        QString(),
        QString(),
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
        ResponseMode::Chat,
        QString(),
        QString(),
        ReasoningMode::Balanced);

    QVERIFY(messages.first().content.contains(QStringLiteral("Runtime:")));
    QVERIFY(messages.first().content.contains(QStringLiteral("wake phrase: Hey Vaxil")));
    QVERIFY(messages.first().content.contains(QStringLiteral("timezone:")));
    QVERIFY(messages.first().content.contains(QStringLiteral("Return only the user-facing answer")));
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
        ResponseMode::Chat,
        QString(),
        QString(),
        ReasoningMode::Balanced,
        QStringLiteral("User appears to be holding a red can"));

    QVERIFY(messages.first().content.contains(QStringLiteral("current environment summary: User appears to be holding a red can")));
}

void AiServicesTests::promptAdapterStructuresMemoryByLane()
{
    PromptAdapter adapter;

    MemoryContext memory;
    memory.profile.push_back({.type = QStringLiteral("preference"), .key = QStringLiteral("general_preference"), .value = QStringLiteral("short answers")});
    memory.activeCommitments.push_back({.type = QStringLiteral("context"), .key = QStringLiteral("current_project"), .value = QStringLiteral("Vaxil behavior layer")});
    memory.episodic.push_back({.type = QStringLiteral("fact"), .key = QStringLiteral("recent_note"), .value = QStringLiteral("Tool narration still needs work")});

    const auto messages = adapter.buildConversationMessages(
        QStringLiteral("What should we tackle next?"),
        {},
        memory,
        {
            .assistantName = QStringLiteral("Vaxil"),
            .personality = QStringLiteral("calm"),
            .tone = QStringLiteral("confident"),
            .addressingStyle = QStringLiteral("direct")
        },
        {},
        ResponseMode::Act,
        QStringLiteral("Check the next task"),
        QStringLiteral("Keep the result grounded."),
        ReasoningMode::Balanced);

    QVERIFY(messages.first().content.contains(QStringLiteral("<profile_memory>")));
    QVERIFY(messages.first().content.contains(QStringLiteral("<active_commitments>")));
    QVERIFY(messages.first().content.contains(QStringLiteral("<episodic_memory>")));
    QVERIFY(messages.first().content.contains(QStringLiteral("current_project = Vaxil behavior layer")));
    QVERIFY(messages.first().content.contains(QStringLiteral("mode: act")));
    QVERIFY(messages.first().content.contains(QStringLiteral("session_goal: Check the next task")));
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
        ResponseMode::Act,
        QStringLiteral("List the workspace files"),
        QStringLiteral("Show only grounded results."),
        ReasoningMode::Balanced);

    QVERIFY(messages.first().content.contains(QStringLiteral("tool_calls")));
    QVERIFY(!messages.first().content.contains(QStringLiteral("background_tasks")));
    QVERIFY(messages.first().content.contains(QStringLiteral("D:/Vaxil")));
}

void AiServicesTests::promptAdapterBuildsHybridContinuationEnvelope()
{
    PromptAdapter adapter;

    const auto messages = adapter.buildHybridAgentContinuationMessages(
        QStringLiteral("check the latest release notes"),
        {
            AgentToolResult{
                .callId = QStringLiteral("call-1"),
                .toolName = QStringLiteral("web_search"),
                .success = true,
                .errorKind = ToolErrorKind::None,
                .summary = QStringLiteral("Web search ready"),
                .detail = QStringLiteral("Search completed"),
                .payload = QJsonObject{{QStringLiteral("text"), QStringLiteral("Result body")}}
            }
        },
        {},
        {
            .assistantName = QStringLiteral("Vaxil"),
            .personality = QStringLiteral("calm"),
            .tone = QStringLiteral("confident"),
            .addressingStyle = QStringLiteral("direct")
        },
        {},
        QStringLiteral("D:/Vaxil"),
        IntentType::GENERAL_CHAT,
        {{QStringLiteral("web_search"), QStringLiteral("Search the web"), nlohmann::json::object()}},
        ResponseMode::ActWithProgress,
        QStringLiteral("Verify the latest release notes"),
        QStringLiteral("Use grounded tool results."),
        ReasoningMode::Balanced);

    QVERIFY(messages.first().content.contains(QStringLiteral("tool_calls")));
    QVERIFY(messages.first().content.contains(QStringLiteral("Continue the active action thread")));
    QVERIFY(messages.last().content.contains(QStringLiteral("Completed tool results")));
    QVERIFY(messages.last().content.contains(QStringLiteral("\"tool_name\": \"web_search\"")));
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
    QVERIFY(names.contains(QStringLiteral("computer_open_url")));
    QVERIFY(names.contains(QStringLiteral("web_search")));
    QVERIFY(names.indexOf(QStringLiteral("web_search")) < names.indexOf(QStringLiteral("computer_open_url")));
    QVERIFY(!names.contains(QStringLiteral("computer_write_file")));
    QVERIFY(!names.contains(QStringLiteral("computer_set_timer")));
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
        ResponseMode::Chat,
        QString(),
        QString(),
        ReasoningMode::Balanced);

    QVERIFY(messages.first().content.contains(QStringLiteral("wake phrase: Hey Vaxil")));
    QVERIFY(messages.first().content.contains(QStringLiteral("Answer naturally and concisely")));
}

void AiServicesTests::promptAdapterForbidsFalseCapabilityDenialsAndHonorifics()
{
    PromptAdapter adapter;

    const auto messages = adapter.buildConversationMessages(
        QStringLiteral("Did you create the file already?"),
        {},
        {},
        {
            .assistantName = QStringLiteral("Vaxil"),
            .personality = QStringLiteral("calm"),
            .tone = QStringLiteral("confident"),
            .addressingStyle = QStringLiteral("direct")
        },
        {},
        ResponseMode::Chat,
        QString(),
        QStringLiteral("Ground the answer in retrieved evidence before responding."),
        ReasoningMode::Balanced);

    const QString systemPrompt = messages.first().content;
    QVERIFY(systemPrompt.contains(QStringLiteral("Do not call the user sir")));
    QVERIFY(systemPrompt.contains(QStringLiteral("Speak naturally, briefly")));
    QVERIFY(systemPrompt.contains(QStringLiteral("Return only the user-facing answer")));
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

void AiServicesTests::spokenReplyStripsModelWrapperTags()
{
    const SpokenReply reply = parseSpokenReply(QStringLiteral("<answer>The latest model is GPT-5.4.</answer> 3 < 5"));

    QVERIFY(!reply.displayText.contains(QStringLiteral("answer"), Qt::CaseInsensitive));
    QVERIFY(!reply.spokenText.contains(QStringLiteral("answer"), Qt::CaseInsensitive));
    QVERIFY(reply.displayText.contains(QStringLiteral("The latest model is GPT-5.4.")));
    QVERIFY(reply.displayText.contains(QStringLiteral("3 < 5")));
    QVERIFY(reply.shouldSpeak);
}

void AiServicesTests::spokenReplyStripsPromptLeakageLines()
{
    const SpokenReply reply = parseSpokenReply(
        QStringLiteral("Tone: concise, confident, non-robotic.\n"
                       "Runtime: local datetime.\n"
                       "The latest OpenAI model is GPT-5.4."));

    QVERIFY(!reply.displayText.contains(QStringLiteral("Tone:"), Qt::CaseInsensitive));
    QVERIFY(!reply.displayText.contains(QStringLiteral("Runtime:"), Qt::CaseInsensitive));
    QVERIFY(reply.displayText.contains(QStringLiteral("GPT-5.4")));
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

void AiServicesTests::webSearchQueryBuilderRemovesFillerAndAddsFreshYear()
{
    QCOMPARE(WebSearchQueryBuilder::build(
                 QStringLiteral("Search the web instead of guessing latest OpenAI model"),
                 2026),
             QStringLiteral("latest OpenAI model 2026"));
    QCOMPARE(WebSearchQueryBuilder::build(
                 QStringLiteral("look it up Python tutorials"),
                 2026),
             QStringLiteral("Python tutorials"));
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
