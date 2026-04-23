#include "core/TurnOrchestrationRuntime.h"

#include "core/AssistantBehaviorPolicy.h"
#include "core/tools/ToolResultEvidencePolicy.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace {
QString routeKindName(InputRouteKind kind)
{
    switch (kind) {
    case InputRouteKind::LocalResponse:
        return QStringLiteral("local_response");
    case InputRouteKind::DeterministicTasks:
        return QStringLiteral("deterministic_tasks");
    case InputRouteKind::BackgroundTasks:
        return QStringLiteral("background_tasks");
    case InputRouteKind::Conversation:
        return QStringLiteral("conversation");
    case InputRouteKind::AgentConversation:
        return QStringLiteral("agent_conversation");
    case InputRouteKind::CommandExtraction:
        return QStringLiteral("command_extraction");
    case InputRouteKind::AgentCapabilityError:
        return QStringLiteral("agent_capability_error");
    case InputRouteKind::None:
    default:
        return QStringLiteral("none");
    }
}

QStringList toolNames(const QList<AgentToolSpec> &tools)
{
    QStringList names;
    for (const AgentToolSpec &tool : tools) {
        names.push_back(tool.name);
    }
    return names;
}

bool toolListContainsPrefix(const QList<AgentToolSpec> &tools, const QString &prefix)
{
    for (const AgentToolSpec &tool : tools) {
        if (tool.name.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

bool toolListContains(const QList<AgentToolSpec> &tools, const QString &name)
{
    for (const AgentToolSpec &tool : tools) {
        if (tool.name == name) {
            return true;
        }
    }
    return false;
}

bool isBackendRoute(InputRouteKind kind)
{
    return kind == InputRouteKind::Conversation
        || kind == InputRouteKind::AgentConversation
        || kind == InputRouteKind::CommandExtraction;
}

QString toolFamily(const QString &toolName)
{
    const QString normalized = toolName.trimmed().toLower();
    if (normalized.startsWith(QStringLiteral("web_"))) {
        return QStringLiteral("web");
    }
    if (normalized == QStringLiteral("browser_fetch_text")) {
        return QStringLiteral("web");
    }
    if (normalized.startsWith(QStringLiteral("file_"))
        || normalized == QStringLiteral("dir_list")
        || normalized.startsWith(QStringLiteral("log_"))
        || normalized == QStringLiteral("ai_log_read")) {
        return QStringLiteral("file");
    }
    if (normalized.startsWith(QStringLiteral("memory_"))) {
        return QStringLiteral("memory");
    }
    return normalized.section(QLatin1Char('_'), 0, 0);
}

bool hasAnyCue(const QString &lowered, const QStringList &cues)
{
    for (const QString &cue : cues) {
        if (lowered.contains(cue)) {
            return true;
        }
    }
    return false;
}

QList<AgentToolSpec> minimalBackendTools(const QString &effectiveInput,
                                         const QList<AgentToolSpec> &availableTools)
{
    const QString lowered = effectiveInput.toLower();
    QStringList preferred = {
        QStringLiteral("web_search"),
        QStringLiteral("web_fetch"),
        QStringLiteral("browser_fetch_text"),
        QStringLiteral("memory_search")
    };
    const bool fileOrLogIntent = hasAnyCue(lowered, {
        QStringLiteral("file"),
        QStringLiteral("read"),
        QStringLiteral("log"),
        QStringLiteral("error"),
        QStringLiteral("trace")
    });
    if (fileOrLogIntent) {
        preferred << QStringLiteral("file_read")
                  << QStringLiteral("file_search")
                  << QStringLiteral("log_search")
                  << QStringLiteral("log_tail")
                  << QStringLiteral("ai_log_read");
    }
    if (hasAnyCue(lowered, {QStringLiteral("open "), QStringLiteral("launch"), QStringLiteral("browser"), QStringLiteral("url")})) {
        preferred << QStringLiteral("browser_open")
                  << QStringLiteral("computer_open_url")
                  << QStringLiteral("computer_open_app");
    }

    QList<AgentToolSpec> selected;
    QSet<QString> selectedNames;
    for (const QString &toolName : preferred) {
        for (const AgentToolSpec &tool : availableTools) {
            if (tool.name == toolName && !selectedNames.contains(tool.name)) {
                selected.push_back(tool);
                selectedNames.insert(tool.name);
            }
        }
    }
    if (!selected.isEmpty()) {
        return selected;
    }
    if (availableTools.isEmpty()) {
        return {};
    }
    return {availableTools.first()};
}

QString payloadPreview(const QJsonObject &payload)
{
    if (payload.isEmpty()) {
        return {};
    }
    QString preview = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    if (preview.size() > 1200) {
        preview = preview.left(1200) + QStringLiteral("...");
    }
    return preview;
}

QString clipStateText(QString text, int maxChars)
{
    text = text.simplified();
    if (text.size() > maxChars) {
        text = text.left(maxChars).trimmed() + QStringLiteral("...");
    }
    return text;
}

QString sanitizeThreadText(QString text, int maxChars)
{
    text = text.trimmed();
    const QString marker = QStringLiteral("User follow-up:");
    const int markerIndex = text.lastIndexOf(marker, -1, Qt::CaseInsensitive);
    if (markerIndex >= 0) {
        text = text.mid(markerIndex + marker.size()).trimmed();
    }
    text.remove(QRegularExpression(
        QStringLiteral("^You are continuing the current assistant action thread\\.?\\s*"),
        QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(
        QStringLiteral("^Treat the user's message as a follow-up to this task when appropriate\\.?\\s*Only start a brand-new unrelated task if the user clearly asks for one\\.?\\s*"),
        QRegularExpression::CaseInsensitiveOption));
    const int threadStateIndex = text.indexOf(QStringLiteral("Thread state:"), 0, Qt::CaseInsensitive);
    if (threadStateIndex >= 0) {
        text = text.left(threadStateIndex).trimmed();
    }
    return clipStateText(text, maxChars);
}

QString evidenceTextForResult(const AgentToolResult &result)
{
    QStringList lines;
    lines << QStringLiteral("tool=%1 success=%2")
                 .arg(result.toolName, result.success ? QStringLiteral("true") : QStringLiteral("false"));
    if (!result.summary.trimmed().isEmpty()) {
        lines << QStringLiteral("summary: %1").arg(result.summary.trimmed());
    }
    if (!result.output.trimmed().isEmpty()) {
        lines << QStringLiteral("output: %1").arg(result.output.trimmed().left(2400));
    } else if (!result.detail.trimmed().isEmpty()) {
        lines << QStringLiteral("detail: %1").arg(result.detail.trimmed().left(1200));
    } else {
        const QString payload = payloadPreview(result.payload);
        if (!payload.isEmpty()) {
            lines << QStringLiteral("payload: %1").arg(payload);
        }
    }
    return lines.join(QStringLiteral("\n"));
}

QString actionThreadStateText(const std::optional<ActionThread> &thread, qint64 nowMs)
{
    if (!thread.has_value() || !thread->isUsable(nowMs)) {
        return {};
    }

    QStringList lines;
    lines << QStringLiteral("active_thread=%1").arg(thread->id);
    if (!thread->taskType.trimmed().isEmpty()) {
        lines << QStringLiteral("task_type=%1").arg(thread->taskType.trimmed());
    }
    if (!thread->userGoal.trimmed().isEmpty()) {
        lines << QStringLiteral("user_goal=%1").arg(sanitizeThreadText(thread->userGoal, 420));
    }
    if (!thread->resultSummary.trimmed().isEmpty()) {
        lines << QStringLiteral("last_result=%1").arg(sanitizeThreadText(thread->resultSummary, 700));
    }
    if (!thread->nextStepHint.trimmed().isEmpty()) {
        lines << QStringLiteral("next_step=%1").arg(sanitizeThreadText(thread->nextStepHint, 240));
    }
    if (!thread->sourceUrls.isEmpty()) {
        lines << QStringLiteral("sources=%1").arg(thread->sourceUrls.join(QStringLiteral(", ")));
    }
    return lines.join(QStringLiteral("\n"));
}

QString actionThreadEvidenceText(const std::optional<ActionThread> &thread, qint64 nowMs)
{
    if (!thread.has_value() || !thread->isUsable(nowMs) || !thread->hasArtifacts()) {
        return {};
    }

    QStringList lines;
    if (!thread->artifactText.trimmed().isEmpty()) {
        lines << sanitizeThreadText(thread->artifactText, 2400);
    }
    const QString payload = payloadPreview(thread->payload);
    if (!payload.isEmpty()) {
        lines << QStringLiteral("artifact_payload: %1").arg(payload);
    }
    if (!thread->sourceUrls.isEmpty()) {
        lines << QStringLiteral("artifact_sources: %1").arg(thread->sourceUrls.join(QStringLiteral(", ")));
    }
    return lines.join(QStringLiteral("\n"));
}

bool isPrivateContextMemory(const MemoryRecord &record)
{
    const QString text = (record.key + QLatin1Char(' ') + record.value + QLatin1Char(' ') + record.source).toLower();
    return text.contains(QStringLiteral("private_mode"))
        || text.contains(QStringLiteral("private context"))
        || record.key.startsWith(QStringLiteral("desktop_context"), Qt::CaseInsensitive)
        || record.key.startsWith(QStringLiteral("compiled_context"), Qt::CaseInsensitive);
}

MemoryContext suppressPrivateContextMemory(MemoryContext context)
{
    auto filterLane = [](QList<MemoryRecord> &lane) {
        QList<MemoryRecord> kept;
        for (const MemoryRecord &record : lane) {
            if (!isPrivateContextMemory(record)) {
                kept.push_back(record);
            }
        }
        lane = kept;
    };
    filterLane(context.profile);
    filterLane(context.episodic);
    filterLane(context.activeCommitments);
    return context;
}

PromptAssemblyReport runtimeReportFor(const PromptTurnContext &context, const QString &evidenceState)
{
    PromptAssemblyReport report;
    report.selectedToolNames = toolNames(context.allowedTools);
    report.selectedMemoryCount = context.selectedMemory.promptRecords().size();
    report.evidenceCount = context.verifiedEvidence.trimmed().isEmpty() ? 0 : 1;

    const auto addIncluded = [&report](const QString &name, const QString &reason, const QString &content) {
        if (content.trimmed().isEmpty()) {
            return;
        }
        report.includedBlocks.push_back({name, reason, content, true});
        report.totalPromptChars += content.size();
    };
    const auto addSuppressed = [&report](const QString &name, const QString &reason) {
        report.suppressedBlocks.push_back({name, reason, {}, false});
    };

    addIncluded(QStringLiteral("identity"), QStringLiteral("prompt.identity_minimal"), context.identity.assistantName);
    addIncluded(QStringLiteral("task_state"), QStringLiteral("prompt.task_state_selected"), context.activeTaskState);
    addIncluded(QStringLiteral("memory"), QStringLiteral("prompt.memory_selected"), QString::number(report.selectedMemoryCount));
    addIncluded(QStringLiteral("evidence"), evidenceState == QStringLiteral("low_signal")
                    ? QStringLiteral("prompt.evidence_low_signal")
                    : QStringLiteral("prompt.evidence_verified"),
                context.verifiedEvidence);
    addIncluded(QStringLiteral("tools"), QStringLiteral("prompt.tools_selected"), report.selectedToolNames.join(QStringLiteral(",")));
    addIncluded(QStringLiteral("constraints"), QStringLiteral("prompt.constraints_active"), context.activeBehavioralConstraints);
    addIncluded(QStringLiteral("response_contract"), QStringLiteral("prompt.response_contract_compact"), context.compactResponseContract);

    if (!context.includeWorkspaceContext) {
        addSuppressed(QStringLiteral("workspace"), QStringLiteral("prompt.workspace_not_relevant"));
    }
    if (!context.includeLogContext) {
        addSuppressed(QStringLiteral("logs"), QStringLiteral("prompt.logs_not_relevant"));
    }
    if (!context.includeFewShotExamples) {
        addSuppressed(QStringLiteral("examples"), QStringLiteral("prompt.examples_disabled_by_default"));
    }
    if (context.allowedTools.isEmpty()) {
        addSuppressed(QStringLiteral("tools"), QStringLiteral("prompt.no_tools_selected"));
    }
    if (context.verifiedEvidence.trimmed().isEmpty()) {
        addSuppressed(QStringLiteral("evidence"), evidenceState == QStringLiteral("low_signal")
            ? QStringLiteral("prompt.evidence_low_signal_omitted")
            : QStringLiteral("prompt.no_verified_evidence"));
    }

    return report;
}
}

TurnOrchestrationRuntime::TurnOrchestrationRuntime(const AssistantBehaviorPolicy *behaviorPolicy)
    : m_behaviorPolicy(behaviorPolicy)
{
}

TurnRuntimePlan TurnOrchestrationRuntime::buildPlan(const TurnRuntimeInput &input) const
{
    TurnRuntimePlan plan;
    const QString effectiveInput = input.effectiveInput.trimmed().isEmpty()
        ? input.rawUserInput
        : input.effectiveInput;
    const IntentType intent = input.intent;

    plan.toolPlan = m_behaviorPolicy
        ? m_behaviorPolicy->buildToolPlan(effectiveInput, intent, input.availableTools)
        : ToolPlan{};

    plan.selectedTools = input.preselectedTools;
    if (plan.selectedTools.isEmpty() && m_behaviorPolicy) {
        plan.selectedTools = m_behaviorPolicy->selectRelevantTools(effectiveInput, intent, input.availableTools);
    }
    if (plan.selectedTools.isEmpty() && isBackendRoute(input.routeDecision.kind) && !input.availableTools.isEmpty()) {
        plan.selectedTools = minimalBackendTools(effectiveInput, input.availableTools);
    }

    plan.trustDecision = m_behaviorPolicy
        ? m_behaviorPolicy->assessTrust(input.rawUserInput, input.routeDecision, plan.toolPlan, input.desktopContextSnapshot)
        : input.actionSession.trust;

    if (input.currentActionThread.has_value() && m_behaviorPolicy) {
        plan.continuesActionThread = m_behaviorPolicy->shouldContinueActionThread(
            input.rawUserInput,
            input.routeDecision,
            *input.currentActionThread,
            input.currentTimeMs);
    }

    QStringList verifiedEvidence;
    bool sawLowSignalEvidence = false;
    QString firstUsefulFamily;
    bool toolDriftDetected = false;
    for (const AgentToolResult &result : input.toolResults) {
        const ToolResultEvidenceAssessment assessment = ToolResultEvidencePolicy::assess(result);
        if (assessment.lowSignal) {
            sawLowSignalEvidence = true;
            continue;
        }
        if (result.success) {
            const QString evidence = evidenceTextForResult(result);
            if (!evidence.trimmed().isEmpty()) {
                verifiedEvidence.push_back(evidence);
                const QString family = toolFamily(result.toolName);
                if (firstUsefulFamily.isEmpty()) {
                    firstUsefulFamily = family;
                } else if (family != firstUsefulFamily && family != QStringLiteral("memory")) {
                    toolDriftDetected = true;
                }
            }
        }
    }

    const QString threadEvidence = actionThreadEvidenceText(input.currentActionThread, input.currentTimeMs);
    if (!threadEvidence.trimmed().isEmpty()) {
        verifiedEvidence.push_back(threadEvidence);
    }

    if (!verifiedEvidence.isEmpty()) {
        plan.evidenceState = QStringLiteral("verified");
        plan.evidenceSufficient = true;
    } else if (sawLowSignalEvidence) {
        plan.evidenceState = QStringLiteral("low_signal");
    }
    plan.toolDriftDetected = toolDriftDetected;

    plan.selectedMemory = input.privateMode
        ? suppressPrivateContextMemory(input.selectedMemory)
        : input.selectedMemory;

    QStringList constraints;
    constraints << QStringLiteral("route=%1").arg(routeKindName(input.routeDecision.kind));
    if (input.privateMode) {
        constraints << QStringLiteral("private_mode=enabled");
    }
    if (input.focusMode.enabled) {
        constraints << QStringLiteral("focus_mode=enabled");
    }
    if (plan.trustDecision.requiresConfirmation) {
        constraints << QStringLiteral("requires_confirmation=%1").arg(plan.trustDecision.reason);
    }
    if (plan.evidenceState == QStringLiteral("low_signal")) {
        constraints << QStringLiteral("evidence=low_signal_do_not_claim_grounded_success");
    }
    if (plan.evidenceSufficient) {
        constraints << QStringLiteral("evidence_sufficient=true_answer_from_verified_evidence_no_more_tools_unless_explicitly_needed");
    }
    if (plan.toolDriftDetected) {
        constraints << QStringLiteral("tool_drift_detected=true_stop_cross_family_tool_hopping");
    }

    QStringList taskState;
    const QString threadState = actionThreadStateText(input.currentActionThread, input.currentTimeMs);
    if (!threadState.trimmed().isEmpty()) {
        taskState << threadState;
    }
    if (!input.actionSession.goal.trimmed().isEmpty()) {
        taskState << QStringLiteral("session_goal=%1").arg(input.actionSession.goal.trimmed());
    }
    if (plan.continuesActionThread) {
        taskState << QStringLiteral("continuation=true");
    }

    plan.actionLoopState = input.toolResults.isEmpty()
        ? QStringLiteral("inspect_decide")
        : QStringLiteral("verify_continue");

    PromptTurnContext context;
    context.userInput = input.rawUserInput;
    context.identity = input.identity;
    context.userProfile = input.userProfile;
    context.intent = intent;
    context.reasoningMode = input.reasoningMode;
    context.responseMode = input.actionSession.responseMode;
    context.sessionGoal = input.actionSession.goal;
    context.nextStepHint = input.actionSession.nextStepHint;
    context.selectedMemory = plan.selectedMemory;
    context.allowedTools = plan.selectedTools;
    context.toolResults = input.toolResults;
    context.workspaceRoot = input.workspaceRoot;
    context.desktopContext = input.privateMode ? QString() : input.desktopContext;
    context.visionContext = input.visionContext;
    context.activeTaskState = taskState.join(QStringLiteral("\n"));
    context.verifiedEvidence = verifiedEvidence.join(QStringLiteral("\n\n---\n\n"));
    context.activeBehavioralConstraints = constraints.join(QStringLiteral("\n"));
    context.compactResponseContract = plan.evidenceSufficient
        ? QStringLiteral("Answer from verified evidence now. Do not call more tools unless a clearly missing same-topic source is required.")
        : QStringLiteral("Use inspect -> decide -> tool-use -> verify -> continue. Answer only from verified evidence; ask one concise clarification if required.");
    context.includeWorkspaceContext = intent == IntentType::LIST_FILES
        || intent == IntentType::READ_FILE
        || intent == IntentType::WRITE_FILE
        || toolListContainsPrefix(plan.selectedTools, QStringLiteral("file_"))
        || toolListContains(plan.selectedTools, QStringLiteral("dir_list"))
        || toolListContains(plan.selectedTools, QStringLiteral("computer_write_file"));
    context.includeLogContext = toolListContains(plan.selectedTools, QStringLiteral("log_tail"))
        || toolListContains(plan.selectedTools, QStringLiteral("log_search"))
        || toolListContains(plan.selectedTools, QStringLiteral("ai_log_read"));
    context.includeFewShotExamples = false;

    plan.promptContext = context;
    plan.promptReport = runtimeReportFor(context, plan.evidenceState);
    return plan;
}
