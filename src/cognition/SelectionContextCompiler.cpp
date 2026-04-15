#include "cognition/SelectionContextCompiler.h"

#include <QDateTime>

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
                                                      bool privateModeEnabled)
{
    return DesktopContextSelectionBuilder::buildSelectionInput(
        input,
        intent,
        buildCompiledDesktopSummary(desktopContext, desktopSummary),
        desktopContext,
        desktopContextAtMs,
        QDateTime::currentMSecsSinceEpoch(),
        privateModeEnabled);
}

QString SelectionContextCompiler::buildPromptContext(const QString &desktopSummary,
                                                     const QVariantMap &desktopContext)
{
    QString context = buildCompiledDesktopSummary(desktopContext, desktopSummary);
    const QString taskId = desktopContext.value(QStringLiteral("taskId")).toString().trimmed();
    const QString threadId = desktopContext.value(QStringLiteral("threadId")).toString().trimmed();
    if (!taskId.isEmpty()) {
        context += QStringLiteral(" Task type: %1.").arg(taskId);
    }
    if (!threadId.isEmpty()) {
        context += QStringLiteral(" Thread: %1.").arg(threadId);
    }
    return context.simplified();
}

SelectionContextCompilation SelectionContextCompiler::compile(const QString &query,
                                                             IntentType intent,
                                                             const QVariantMap &desktopContext,
                                                             const QString &desktopSummary,
                                                             qint64 desktopContextAtMs,
                                                             bool privateModeEnabled,
                                                             const MemoryRecord &runtimeRecord,
                                                             const MemoryPolicyHandler *memoryPolicyHandler,
                                                             const AssistantBehaviorPolicy *behaviorPolicy)
{
    SelectionContextCompilation compilation;
    compilation.compiledDesktopSummary = buildCompiledDesktopSummary(desktopContext, desktopSummary);
    compilation.selectionInput = buildSelectionInput(query,
                                                     intent,
                                                     desktopContext,
                                                     desktopSummary,
                                                     desktopContextAtMs,
                                                     privateModeEnabled);
    compilation.promptContext = buildPromptContext(desktopSummary, desktopContext);
    compilation.selectedMemoryRecords = memoryPolicyHandler
        ? memoryPolicyHandler->requestMemory(compilation.selectionInput, runtimeRecord)
        : QList<MemoryRecord>{};
    compilation.compiledContextRecords = desktopContextRecords(desktopContext, desktopSummary);

    const QList<MemoryRecord> merged = mergedRecords(compilation.compiledContextRecords,
                                                     compilation.selectedMemoryRecords);
    compilation.memoryContext = behaviorPolicy
        ? behaviorPolicy->buildMemoryContext(compilation.selectionInput, merged)
        : fallbackMemoryContext(compilation.selectedMemoryRecords, compilation.compiledContextRecords);
    return compilation;
}
