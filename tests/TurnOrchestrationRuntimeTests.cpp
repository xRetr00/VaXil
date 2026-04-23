#include <QtTest>
#include <QDateTime>

#include "core/AssistantBehaviorPolicy.h"
#include "core/TurnOrchestrationRuntime.h"

namespace {
AssistantIdentity identity()
{
    return AssistantIdentity{
        .assistantName = QStringLiteral("Vaxil"),
        .personality = QStringLiteral("desktop companion"),
        .tone = QStringLiteral("direct"),
        .addressingStyle = QStringLiteral("natural")
    };
}

UserProfile userProfile()
{
    return UserProfile{.userName = QStringLiteral("Tester")};
}

AgentToolSpec tool(const QString &name)
{
    return AgentToolSpec{
        .name = name,
        .description = QStringLiteral("Test tool %1").arg(name),
        .parameters = nlohmann::json::object()
    };
}

ActionThread usableThread(qint64 nowMs)
{
    return ActionThread{
        .id = QStringLiteral("thread-1"),
        .taskType = QStringLiteral("browser_open"),
        .userGoal = QStringLiteral("Open YouTube and search for machine learning courses"),
        .resultSummary = QStringLiteral("Opened search results"),
        .artifactText = QStringLiteral("Machine Learning Course - example result"),
        .sourceUrls = {QStringLiteral("https://example.com/course")},
        .nextStepHint = QStringLiteral("Summarize the visible results"),
        .state = ActionThreadState::Completed,
        .success = true,
        .valid = true,
        .updatedAtMs = nowMs,
        .expiresAtMs = nowMs + 60000
    };
}
}

class TurnOrchestrationRuntimeTests : public QObject
{
    Q_OBJECT

private slots:
    void sameTaskFollowUpContinuesThreadState();
    void unrelatedRequestDoesNotAttachToThread();
    void selectedToolsAreNarrowedByIntent();
    void lowSignalEvidenceBlocksGroundedState();
    void verifiedEvidenceSetsSufficientAndStopsMoreToolPrompting();
    void promptTaskStateClipsRecursiveThreadEnvelope();
    void privateModeSuppressesDesktopContextMemory();
    void backendRouteGetsMinimalToolsWhenSelectionEmpty();
};

void TurnOrchestrationRuntimeTests::sameTaskFollowUpContinuesThreadState()
{
    AssistantBehaviorPolicy policy;
    TurnOrchestrationRuntime runtime(&policy);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    InputRouteDecision route;
    route.kind = InputRouteKind::Conversation;
    route.intent = IntentType::GENERAL_CHAT;

    TurnRuntimeInput input;
    input.rawUserInput = QStringLiteral("What did you see?");
    input.effectiveInput = input.rawUserInput;
    input.routeDecision = route;
    input.intent = IntentType::GENERAL_CHAT;
    input.currentActionThread = usableThread(nowMs);
    input.identity = identity();
    input.userProfile = userProfile();
    input.currentTimeMs = nowMs;

    const TurnRuntimePlan plan = runtime.buildPlan(input);

    QVERIFY(plan.continuesActionThread);
    QVERIFY(plan.promptContext.activeTaskState.contains(QStringLiteral("continuation=true")));
    QVERIFY(plan.promptContext.verifiedEvidence.contains(QStringLiteral("Machine Learning Course")));
    QCOMPARE(plan.evidenceState, QStringLiteral("verified"));
}

void TurnOrchestrationRuntimeTests::unrelatedRequestDoesNotAttachToThread()
{
    AssistantBehaviorPolicy policy;
    TurnOrchestrationRuntime runtime(&policy);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    InputRouteDecision route;
    route.kind = InputRouteKind::Conversation;
    route.intent = IntentType::GENERAL_CHAT;

    TurnRuntimeInput input;
    input.rawUserInput = QStringLiteral("Write a poem about winter");
    input.effectiveInput = input.rawUserInput;
    input.routeDecision = route;
    input.intent = IntentType::GENERAL_CHAT;
    input.currentActionThread = usableThread(nowMs);
    input.identity = identity();
    input.userProfile = userProfile();
    input.currentTimeMs = nowMs;

    const TurnRuntimePlan plan = runtime.buildPlan(input);

    QVERIFY(!plan.continuesActionThread);
}

void TurnOrchestrationRuntimeTests::selectedToolsAreNarrowedByIntent()
{
    AssistantBehaviorPolicy policy;
    TurnOrchestrationRuntime runtime(&policy);

    InputRouteDecision route;
    route.kind = InputRouteKind::AgentConversation;
    route.intent = IntentType::READ_FILE;

    TurnRuntimeInput input;
    input.rawUserInput = QStringLiteral("Read D:/Vaxil/PLAN.md");
    input.effectiveInput = input.rawUserInput;
    input.routeDecision = route;
    input.intent = IntentType::READ_FILE;
    input.identity = identity();
    input.userProfile = userProfile();
    input.availableTools = {
        tool(QStringLiteral("file_read")),
        tool(QStringLiteral("dir_list")),
        tool(QStringLiteral("computer_open_app"))
    };
    input.currentTimeMs = QDateTime::currentMSecsSinceEpoch();

    const TurnRuntimePlan plan = runtime.buildPlan(input);
    QStringList selected;
    for (const AgentToolSpec &spec : plan.selectedTools) {
        selected.push_back(spec.name);
    }

    QVERIFY(selected.contains(QStringLiteral("file_read")));
    QVERIFY(!selected.contains(QStringLiteral("computer_open_app")));
    QVERIFY(plan.promptContext.includeWorkspaceContext);
}

void TurnOrchestrationRuntimeTests::lowSignalEvidenceBlocksGroundedState()
{
    AssistantBehaviorPolicy policy;
    TurnOrchestrationRuntime runtime(&policy);

    InputRouteDecision route;
    route.kind = InputRouteKind::AgentConversation;
    route.intent = IntentType::GENERAL_CHAT;

    TurnRuntimeInput input;
    input.rawUserInput = QStringLiteral("What courses did you see?");
    input.effectiveInput = input.rawUserInput;
    input.routeDecision = route;
    input.intent = IntentType::GENERAL_CHAT;
    input.identity = identity();
    input.userProfile = userProfile();
    input.currentTimeMs = QDateTime::currentMSecsSinceEpoch();
    input.toolResults = {
        AgentToolResult{
            .callId = QStringLiteral("call-1"),
            .toolName = QStringLiteral("browser_fetch_text"),
            .output = QString(),
            .success = true,
            .errorKind = ToolErrorKind::None,
            .payload = QJsonObject{{QStringLiteral("text"), QString()}}
        }
    };

    const TurnRuntimePlan plan = runtime.buildPlan(input);

    QCOMPARE(plan.evidenceState, QStringLiteral("low_signal"));
    QVERIFY(plan.promptContext.verifiedEvidence.trimmed().isEmpty());
    QVERIFY(plan.promptContext.activeBehavioralConstraints.contains(QStringLiteral("low_signal")));
}

void TurnOrchestrationRuntimeTests::verifiedEvidenceSetsSufficientAndStopsMoreToolPrompting()
{
    AssistantBehaviorPolicy policy;
    TurnOrchestrationRuntime runtime(&policy);

    InputRouteDecision route;
    route.kind = InputRouteKind::AgentConversation;
    route.intent = IntentType::GENERAL_CHAT;

    TurnRuntimeInput input;
    input.rawUserInput = QStringLiteral("latest OpenAI model");
    input.effectiveInput = input.rawUserInput;
    input.routeDecision = route;
    input.intent = IntentType::GENERAL_CHAT;
    input.identity = identity();
    input.userProfile = userProfile();
    input.currentTimeMs = QDateTime::currentMSecsSinceEpoch();
    input.toolResults = {
        AgentToolResult{
            .callId = QStringLiteral("call-1"),
            .toolName = QStringLiteral("web_search"),
            .output = QStringLiteral("OpenAI release result with sources."),
            .success = true,
            .errorKind = ToolErrorKind::None,
            .summary = QStringLiteral("OpenAI release result"),
            .payload = QJsonObject{{QStringLiteral("text"), QStringLiteral("OpenAI release result with sources.")}}
        }
    };

    const TurnRuntimePlan plan = runtime.buildPlan(input);

    QCOMPARE(plan.evidenceState, QStringLiteral("verified"));
    QVERIFY(plan.evidenceSufficient);
    QVERIFY(plan.promptContext.activeBehavioralConstraints.contains(QStringLiteral("evidence_sufficient=true")));
    QVERIFY(plan.promptContext.compactResponseContract.contains(QStringLiteral("Do not call more tools")));
}

void TurnOrchestrationRuntimeTests::promptTaskStateClipsRecursiveThreadEnvelope()
{
    AssistantBehaviorPolicy policy;
    TurnOrchestrationRuntime runtime(&policy);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    const QString recursiveText = QStringLiteral(
        "You are continuing the current assistant action thread.\n"
        "Treat the user's message as a follow-up to this task when appropriate. "
        "Only start a brand-new unrelated task if the user clearly asks for one.\n\n"
        "Thread state: completed\n"
        "Task type: browser_open\n"
        "User goal: Open YouTube and search for machine learning courses\n"
        "Result summary: Opened search results\n\n"
        "User follow-up: Create a simple HTML snake game and launch it on the browser.");

    ActionThread thread;
    thread.id = QStringLiteral("thread-recursive");
    thread.taskType = QStringLiteral("browser_open");
    thread.userGoal = recursiveText;
    thread.resultSummary = recursiveText + QString(1200, QLatin1Char('x'));
    thread.nextStepHint = recursiveText;
    thread.state = ActionThreadState::Completed;
    thread.success = true;
    thread.valid = true;
    thread.updatedAtMs = nowMs;
    thread.expiresAtMs = nowMs + 60000;

    InputRouteDecision route;
    route.kind = InputRouteKind::AgentConversation;
    route.intent = IntentType::GENERAL_CHAT;

    TurnRuntimeInput input;
    input.rawUserInput = QStringLiteral("Create a simple HTML snake game and launch it on the browser.");
    input.effectiveInput = input.rawUserInput;
    input.routeDecision = route;
    input.intent = IntentType::GENERAL_CHAT;
    input.currentActionThread = thread;
    input.identity = identity();
    input.userProfile = userProfile();
    input.currentTimeMs = nowMs;

    const TurnRuntimePlan plan = runtime.buildPlan(input);

    QVERIFY(!plan.continuesActionThread);
    QVERIFY(plan.promptContext.activeTaskState.size() < 1200);
    QVERIFY(!plan.promptContext.activeTaskState.contains(QStringLiteral("You are continuing")));
    QVERIFY(!plan.promptContext.activeTaskState.contains(QStringLiteral("Thread state:")));
    QVERIFY(plan.promptContext.activeTaskState.contains(
        QStringLiteral("user_goal=Create a simple HTML snake game and launch it on the browser.")));
}

void TurnOrchestrationRuntimeTests::privateModeSuppressesDesktopContextMemory()
{
    AssistantBehaviorPolicy policy;
    TurnOrchestrationRuntime runtime(&policy);

    InputRouteDecision route;
    route.kind = InputRouteKind::Conversation;
    route.intent = IntentType::GENERAL_CHAT;

    MemoryContext memory;
    memory.activeCommitments.push_back(MemoryRecord{
        .type = QStringLiteral("context"),
        .key = QStringLiteral("desktop_context_topic"),
        .value = QStringLiteral("private_mode browser tab"),
        .source = QStringLiteral("desktop_context")
    });
    memory.profile.push_back(MemoryRecord{
        .type = QStringLiteral("preference"),
        .key = QStringLiteral("reply_style"),
        .value = QStringLiteral("concise"),
        .source = QStringLiteral("profile")
    });

    TurnRuntimeInput input;
    input.rawUserInput = QStringLiteral("thanks");
    input.effectiveInput = input.rawUserInput;
    input.routeDecision = route;
    input.intent = IntentType::GENERAL_CHAT;
    input.selectedMemory = memory;
    input.identity = identity();
    input.userProfile = userProfile();
    input.desktopContext = QStringLiteral("Private browser tab: banking");
    input.privateMode = true;
    input.currentTimeMs = QDateTime::currentMSecsSinceEpoch();

    const TurnRuntimePlan plan = runtime.buildPlan(input);

    QCOMPARE(plan.selectedMemory.activeCommitments.size(), 0);
    QCOMPARE(plan.selectedMemory.profile.size(), 1);
    QVERIFY(plan.promptContext.desktopContext.isEmpty());
}

void TurnOrchestrationRuntimeTests::backendRouteGetsMinimalToolsWhenSelectionEmpty()
{
    TurnOrchestrationRuntime runtime(nullptr);

    InputRouteDecision route;
    route.kind = InputRouteKind::Conversation;
    route.intent = IntentType::GENERAL_CHAT;

    TurnRuntimeInput input;
    input.rawUserInput = QStringLiteral("why is startup slow today?");
    input.effectiveInput = input.rawUserInput;
    input.routeDecision = route;
    input.intent = IntentType::GENERAL_CHAT;
    input.identity = identity();
    input.userProfile = userProfile();
    input.availableTools = {
        tool(QStringLiteral("web_search")),
        tool(QStringLiteral("memory_search")),
        tool(QStringLiteral("file_read"))
    };
    input.currentTimeMs = QDateTime::currentMSecsSinceEpoch();

    const TurnRuntimePlan plan = runtime.buildPlan(input);
    QVERIFY(!plan.selectedTools.isEmpty());
    QStringList selected;
    for (const AgentToolSpec &spec : plan.selectedTools) {
        selected.push_back(spec.name);
    }
    QVERIFY(selected.contains(QStringLiteral("web_search")));
}

QTEST_APPLESS_MAIN(TurnOrchestrationRuntimeTests)
#include "TurnOrchestrationRuntimeTests.moc"
