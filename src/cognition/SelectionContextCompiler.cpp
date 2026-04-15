#include "cognition/SelectionContextCompiler.h"

#include <QDateTime>

#include "cognition/CompiledContextHistoryPolicy.h"
#include "cognition/CompiledContextLayeredSignalBuilder.h"
#include "cognition/DesktopContextSelectionBuilder.h"
#include "core/AssistantBehaviorPolicy.h"
#include "core/MemoryPolicyHandler.h"

namespace {
MemoryContext fallbackMemoryContext(const QList<MemoryRecord> &memoryRecords,
                                    const QList<MemoryRecord> &compiledContextRecords)
{
    MemoryContext context;
    context.activeCommitments = compiledContextRecords;
    for (const MemoryRecord &record : memoryRecords) {
        bool duplicate = false;
        for (const MemoryRecord &existing : context.activeCommitments) {
            if (existing.key == record.key && existing.source == record.source) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            context.episodic.push_back(record);
        }
    }
    return context;
}

void appendIfPresent(QList<MemoryRecord> &records,
                     const QString &key,
                     const QString &value,
                     float confidence,
                     const QString &updatedAt)
{
    const QString trimmedValue = value.trimmed();
    if (trimmedValue.isEmpty()) {
        return;
    }

    records.push_back({
        .type = QStringLiteral("context"),
        .key = key,
        .value = trimmedValue,
        .confidence = confidence,
        .source = QStringLiteral("desktop_context"),
        .updatedAt = updatedAt
    });
}

QList<MemoryRecord> desktopContextRecords(const QVariantMap &desktopContext,
                                          const QString &desktopSummary)
{
    QList<MemoryRecord> records;
    const QString updatedAt = QString::number(QDateTime::currentMSecsSinceEpoch());
    appendIfPresent(records, QStringLiteral("desktop_context_summary"), desktopSummary, 0.92f, updatedAt);
    appendIfPresent(records,
                    QStringLiteral("desktop_context_task"),
                    desktopContext.value(QStringLiteral("taskId")).toString(),
                    0.88f,
                    updatedAt);
    appendIfPresent(records,
                    QStringLiteral("desktop_context_topic"),
                    desktopContext.value(QStringLiteral("topic")).toString(),
                    0.88f,
                    updatedAt);
    appendIfPresent(records,
                    QStringLiteral("desktop_context_document"),
                    desktopContext.value(QStringLiteral("documentContext")).toString(),
                    0.86f,
                    updatedAt);
    appendIfPresent(records,
                    QStringLiteral("desktop_context_site"),
                    desktopContext.value(QStringLiteral("siteContext")).toString(),
                    0.84f,
                    updatedAt);
    appendIfPresent(records,
                    QStringLiteral("desktop_context_workspace"),
                    desktopContext.value(QStringLiteral("workspaceContext")).toString(),
                    0.84f,
                    updatedAt);
    appendIfPresent(records,
                    QStringLiteral("desktop_context_app"),
                    desktopContext.value(QStringLiteral("appId")).toString(),
                    0.8f,
                    updatedAt);
    return records;
}

QString promptReasonForKey(const QString &key)
{
    if (key == QStringLiteral("desktop_prompt_summary")) {
        return QStringLiteral("prompt.desktop_summary");
    }
    if (key == QStringLiteral("desktop_prompt_task")) {
        return QStringLiteral("prompt.task_alignment");
    }
    if (key == QStringLiteral("desktop_prompt_topic")) {
        return QStringLiteral("prompt.topic_alignment");
    }
    if (key == QStringLiteral("desktop_prompt_document")) {
        return QStringLiteral("prompt.document_relevance");
    }
    if (key == QStringLiteral("desktop_prompt_site")) {
        return QStringLiteral("prompt.site_relevance");
    }
    if (key == QStringLiteral("desktop_prompt_workspace")) {
        return QStringLiteral("prompt.workspace_relevance");
    }
    if (key == QStringLiteral("desktop_prompt_app")) {
        return QStringLiteral("prompt.app_relevance");
    }
    if (key == QStringLiteral("desktop_prompt_thread")) {
        return QStringLiteral("prompt.thread_continuity");
    }
    if (key == QStringLiteral("history_prompt_mode")) {
        return QStringLiteral("prompt.history_mode");
    }
    return QStringLiteral("prompt.context_relevance");
}

void appendPromptRecord(QList<MemoryRecord> &records,
                        const QString &key,
                        const QString &value,
                        float confidence,
                        const QString &updatedAt)
{
    const QString trimmedValue = value.trimmed();
    if (trimmedValue.isEmpty()) {
        return;
    }

    records.push_back({
        .type = QStringLiteral("prompt_context"),
        .key = key,
        .value = trimmedValue,
        .confidence = confidence,
        .source = promptReasonForKey(key),
        .updatedAt = updatedAt
    });
}

bool isBrowserTask(const QString &taskId)
{
    return taskId.compare(QStringLiteral("browser_tab"), Qt::CaseInsensitive) == 0
        || taskId.contains(QStringLiteral("browser"), Qt::CaseInsensitive);
}

bool isEditorTask(const QString &taskId)
{
    return taskId.compare(QStringLiteral("editor_document"), Qt::CaseInsensitive) == 0
        || taskId.contains(QStringLiteral("editor"), Qt::CaseInsensitive)
        || taskId.contains(QStringLiteral("code"), Qt::CaseInsensitive);
}

QList<MemoryRecord> promptContextRecordsForIntent(IntentType intent,
                                                  const QVariantMap &desktopContext,
                                                  const QString &desktopSummary,
                                                  const CompiledContextHistoryPolicyDecision &historyDecision)
{
    QList<MemoryRecord> records;
    const QString updatedAt = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString summary = desktopSummary.simplified();
    const QString taskId = desktopContext.value(QStringLiteral("taskId")).toString().trimmed();
    const QString topic = desktopContext.value(QStringLiteral("topic")).toString().trimmed();
    const QString document = desktopContext.value(QStringLiteral("documentContext")).toString().trimmed();
    const QString site = desktopContext.value(QStringLiteral("siteContext")).toString().trimmed();
    const QString workspace = desktopContext.value(QStringLiteral("workspaceContext")).toString().trimmed();
    const QString app = desktopContext.value(QStringLiteral("appId")).toString().trimmed();
    const QString threadId = desktopContext.value(QStringLiteral("threadId")).toString().trimmed();

    appendPromptRecord(records,
                       QStringLiteral("desktop_prompt_summary"),
                       summary,
                       0.92f,
                       updatedAt);
    appendPromptRecord(records,
                       QStringLiteral("desktop_prompt_task"),
                       QStringLiteral("Task type: %1.").arg(taskId),
                       0.9f,
                       updatedAt);

    switch (intent) {
    case IntentType::LIST_FILES:
        appendPromptRecord(records,
                           QStringLiteral("desktop_prompt_workspace"),
                           QStringLiteral("Workspace: %1.").arg(workspace),
                           0.86f,
                           updatedAt);
        appendPromptRecord(records,
                           QStringLiteral("desktop_prompt_document"),
                           QStringLiteral("Document: %1.").arg(document),
                           0.84f,
                           updatedAt);
        break;
    case IntentType::READ_FILE:
    case IntentType::WRITE_FILE:
        appendPromptRecord(records,
                           QStringLiteral("desktop_prompt_document"),
                           QStringLiteral("Document: %1.").arg(document),
                           0.9f,
                           updatedAt);
        appendPromptRecord(records,
                           QStringLiteral("desktop_prompt_workspace"),
                           QStringLiteral("Workspace: %1.").arg(workspace),
                           0.86f,
                           updatedAt);
        appendPromptRecord(records,
                           QStringLiteral("desktop_prompt_app"),
                           QStringLiteral("App: %1.").arg(app),
                           0.8f,
                           updatedAt);
        break;
    case IntentType::MEMORY_WRITE:
        appendPromptRecord(records,
                           QStringLiteral("desktop_prompt_topic"),
                           QStringLiteral("Topic: %1.").arg(topic),
                           0.88f,
                           updatedAt);
        appendPromptRecord(records,
                           QStringLiteral("desktop_prompt_thread"),
                           QStringLiteral("Thread: %1.").arg(threadId),
                           0.84f,
                           updatedAt);
        break;
    case IntentType::GENERAL_CHAT:
        appendPromptRecord(records,
                           QStringLiteral("desktop_prompt_topic"),
                           QStringLiteral("Topic: %1.").arg(topic),
                           0.88f,
                           updatedAt);
        if (isBrowserTask(taskId)) {
            appendPromptRecord(records,
                               QStringLiteral("desktop_prompt_document"),
                               QStringLiteral("Page: %1.").arg(document),
                               0.88f,
                               updatedAt);
            appendPromptRecord(records,
                               QStringLiteral("desktop_prompt_site"),
                               QStringLiteral("Site: %1.").arg(site),
                               0.86f,
                               updatedAt);
        } else if (isEditorTask(taskId)) {
            appendPromptRecord(records,
                               QStringLiteral("desktop_prompt_document"),
                               QStringLiteral("Document: %1.").arg(document),
                               0.88f,
                               updatedAt);
            appendPromptRecord(records,
                               QStringLiteral("desktop_prompt_workspace"),
                               QStringLiteral("Workspace: %1.").arg(workspace),
                               0.86f,
                               updatedAt);
        } else {
            appendPromptRecord(records,
                               QStringLiteral("desktop_prompt_document"),
                               QStringLiteral("Document: %1.").arg(document),
                               0.84f,
                               updatedAt);
            appendPromptRecord(records,
                               QStringLiteral("desktop_prompt_site"),
                               QStringLiteral("Site: %1.").arg(site),
                               0.82f,
                               updatedAt);
            appendPromptRecord(records,
                               QStringLiteral("desktop_prompt_workspace"),
                               QStringLiteral("Workspace: %1.").arg(workspace),
                               0.82f,
                               updatedAt);
        }
        appendPromptRecord(records,
                           QStringLiteral("desktop_prompt_thread"),
                           QStringLiteral("Thread: %1.").arg(threadId),
                           0.84f,
                           updatedAt);
        break;
    }

    if (historyDecision.isValid()) {
        appendPromptRecord(records,
                           QStringLiteral("history_prompt_mode"),
                           historyDecision.promptDirective,
                           0.84f,
                           updatedAt);
    }

    return records;
}

QString promptContextFromRecords(const QList<MemoryRecord> &records)
{
    QStringList parts;
    for (const MemoryRecord &record : records) {
        const QString value = record.value.simplified();
        if (!value.isEmpty()) {
            parts.push_back(value);
        }
    }
    return parts.join(QLatin1Char(' ')).simplified();
}

QList<MemoryRecord> mergedRecords(const QList<MemoryRecord> &compiledContextRecords,
                                  const QList<MemoryRecord> &selectedMemoryRecords)
{
    QList<MemoryRecord> merged = compiledContextRecords;
    for (const MemoryRecord &record : selectedMemoryRecords) {
        bool duplicate = false;
        for (const MemoryRecord &existing : merged) {
            if (existing.key == record.key && existing.source == record.source) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            merged.push_back(record);
        }
    }
    return merged;
}
}

QString SelectionContextCompiler::buildCompiledDesktopSummary(const QVariantMap &desktopContext,
                                                             const QString &desktopSummary)
{
    QStringList parts;
    const QString summary = desktopSummary.simplified();
    if (!summary.isEmpty()) {
        parts << summary;
    }

    const QString taskId = desktopContext.value(QStringLiteral("taskId")).toString().trimmed();
    const QString topic = desktopContext.value(QStringLiteral("topic")).toString().trimmed();
    const QString document = desktopContext.value(QStringLiteral("documentContext")).toString().trimmed();
    const QString site = desktopContext.value(QStringLiteral("siteContext")).toString().trimmed();
    const QString workspace = desktopContext.value(QStringLiteral("workspaceContext")).toString().trimmed();
    const QString app = desktopContext.value(QStringLiteral("appId")).toString().trimmed();

    if (!document.isEmpty()) {
        parts << QStringLiteral("Document: %1.").arg(document);
    }
    if (!site.isEmpty()) {
        parts << QStringLiteral("Site: %1.").arg(site);
    }
    if (!workspace.isEmpty()) {
        parts << QStringLiteral("Workspace: %1.").arg(workspace);
    }
    if (!taskId.isEmpty()) {
        parts << QStringLiteral("Task: %1.").arg(taskId);
    }
    if (!topic.isEmpty()) {
        parts << QStringLiteral("Topic: %1.").arg(topic);
    }
    if (!app.isEmpty()) {
        parts << QStringLiteral("App: %1.").arg(app);
    }

    return parts.join(QLatin1Char(' ')).simplified();
}

QString SelectionContextCompiler::buildSelectionInput(const QString &input,
                                                      IntentType intent,
                                                      const QVariantMap &desktopContext,
                                                      const QString &desktopSummary,
                                                      qint64 desktopContextAtMs,
                                                      bool privateModeEnabled,
                                                      const QList<MemoryRecord> &historyRecords)
{
    const QString selectionInput = DesktopContextSelectionBuilder::buildSelectionInput(
        input,
        intent,
        buildCompiledDesktopSummary(desktopContext, desktopSummary),
        desktopContext,
        desktopContextAtMs,
        QDateTime::currentMSecsSinceEpoch(),
        privateModeEnabled);
    const CompiledContextHistoryPolicyDecision historyDecision =
        CompiledContextHistoryPolicy::evaluate(historyRecords);
    if (!historyDecision.isValid()) {
        return selectionInput;
    }

    return QStringLiteral("%1\n\n%2")
        .arg(selectionInput.trimmed(), historyDecision.selectionDirective)
        .simplified();
}

QString SelectionContextCompiler::buildPromptContext(IntentType intent,
                                                     const QString &desktopSummary,
                                                     const QVariantMap &desktopContext,
                                                     const QList<MemoryRecord> &historyRecords)
{
    const CompiledContextHistoryPolicyDecision historyDecision =
        CompiledContextHistoryPolicy::evaluate(historyRecords);
    return promptContextFromRecords(promptContextRecordsForIntent(
        intent,
        desktopContext,
        buildCompiledDesktopSummary(desktopContext, desktopSummary),
        historyDecision));
}

SelectionContextCompilation SelectionContextCompiler::compile(const QString &query,
                                                             IntentType intent,
                                                             const QVariantMap &desktopContext,
                                                             const QString &desktopSummary,
                                                             qint64 desktopContextAtMs,
                                                             bool privateModeEnabled,
                                                             const MemoryRecord &runtimeRecord,
                                                             const QList<MemoryRecord> &historyRecords,
                                                             const MemoryPolicyHandler *memoryPolicyHandler,
                                                             const AssistantBehaviorPolicy *behaviorPolicy)
{
    SelectionContextCompilation compilation;
    const CompiledContextHistoryPolicyDecision historyDecision = memoryPolicyHandler != nullptr
        ? memoryPolicyHandler->compiledContextPolicyDecision()
        : CompiledContextHistoryPolicyDecision{};
    const QList<MemoryRecord> layeredMemoryRecords = memoryPolicyHandler != nullptr
        ? memoryPolicyHandler->compiledContextLayeredMemoryRecords()
        : QList<MemoryRecord>{};
    const QList<MemoryRecord> evolutionRecords = memoryPolicyHandler != nullptr
        ? memoryPolicyHandler->compiledContextPolicyEvolutionRecords()
        : QList<MemoryRecord>{};
    const QList<MemoryRecord> tuningRecords = memoryPolicyHandler != nullptr
        ? memoryPolicyHandler->compiledContextPolicyTuningSignalRecords()
        : QList<MemoryRecord>{};
    const CompiledContextHistoryPolicyDecision effectiveHistoryDecision = historyDecision.isValid()
        ? historyDecision
        : CompiledContextHistoryPolicy::evaluate(historyRecords);
    compilation.compiledDesktopSummary = buildCompiledDesktopSummary(desktopContext, desktopSummary);
    compilation.selectionInput = buildSelectionInput(query,
                                                     intent,
                                                     desktopContext,
                                                     desktopSummary,
                                                     desktopContextAtMs,
                                                     privateModeEnabled,
                                                     effectiveHistoryDecision.isValid() ? QList<MemoryRecord>{} : historyRecords);
    if (effectiveHistoryDecision.isValid()) {
        compilation.selectionInput = QStringLiteral("%1\n\n%2")
            .arg(compilation.selectionInput.trimmed(), effectiveHistoryDecision.selectionDirective)
            .simplified();
    }
    const QString layeredSelectionDirective =
        CompiledContextLayeredSignalBuilder::buildSelectionDirective(
            mergedRecords(mergedRecords(layeredMemoryRecords, evolutionRecords), tuningRecords));
    if (!layeredSelectionDirective.isEmpty()) {
        compilation.selectionInput = QStringLiteral("%1\n\n%2")
            .arg(compilation.selectionInput.trimmed(), layeredSelectionDirective)
            .simplified();
    }
    compilation.historySelectionDirective = effectiveHistoryDecision.selectionDirective;
    compilation.historyPolicyMetadata = effectiveHistoryDecision.plannerMetadata;
    compilation.promptContextRecords = promptContextRecordsForIntent(
        intent,
        desktopContext,
        compilation.compiledDesktopSummary,
        effectiveHistoryDecision);
    compilation.promptContextRecords.append(
        CompiledContextLayeredSignalBuilder::buildPromptContextRecords(
            mergedRecords(mergedRecords(layeredMemoryRecords, evolutionRecords), tuningRecords)));
    compilation.promptContext = promptContextFromRecords(compilation.promptContextRecords);
    compilation.selectedMemoryRecords = memoryPolicyHandler
        ? memoryPolicyHandler->requestMemory(compilation.selectionInput, runtimeRecord)
        : QList<MemoryRecord>{};
    compilation.compiledContextRecords = desktopContextRecords(desktopContext, desktopSummary);
    if (memoryPolicyHandler != nullptr) {
        const QList<MemoryRecord> policySummaryRecords = memoryPolicyHandler->compiledContextPolicySummaryRecords();
        for (const MemoryRecord &record : policySummaryRecords) {
            compilation.compiledContextRecords.push_back(record);
        }
        for (const MemoryRecord &record : layeredMemoryRecords) {
            compilation.compiledContextRecords.push_back(record);
        }
        for (const MemoryRecord &record : evolutionRecords) {
            compilation.compiledContextRecords.push_back(record);
        }
        for (const MemoryRecord &record : tuningRecords) {
            compilation.compiledContextRecords.push_back(record);
        }
    }
    if (effectiveHistoryDecision.isValid()) {
        compilation.compiledContextRecords.push_back(
            CompiledContextHistoryPolicy::buildContextRecord(effectiveHistoryDecision));
    }

    const QList<MemoryRecord> merged = mergedRecords(compilation.compiledContextRecords,
                                                     compilation.selectedMemoryRecords);
    compilation.memoryContext = behaviorPolicy
        ? behaviorPolicy->buildMemoryContext(compilation.selectionInput, merged)
        : fallbackMemoryContext(compilation.selectedMemoryRecords, compilation.compiledContextRecords);
    return compilation;
}
