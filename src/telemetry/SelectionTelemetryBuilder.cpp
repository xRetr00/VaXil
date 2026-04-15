#include "telemetry/SelectionTelemetryBuilder.h"

#include <QStringList>
#include <QVariantList>

namespace {
QVariantList candidatePayload(const QList<ToolPlanStep> &candidates)
{
    QVariantList rows;
    for (int index = 0; index < candidates.size() && index < 4; ++index) {
        const ToolPlanStep &step = candidates.at(index);
        rows.push_back(QVariantMap{
            {QStringLiteral("toolName"), step.toolName},
            {QStringLiteral("affordanceScore"), step.affordanceScore},
            {QStringLiteral("riskScore"), step.riskScore},
            {QStringLiteral("reason"), step.reason}
        });
    }
    return rows;
}

QStringList recordKeys(const QList<MemoryRecord> &records)
{
    QStringList keys;
    for (const MemoryRecord &record : records) {
        const QString key = record.key.trimmed().isEmpty() ? record.type.trimmed() : record.key.trimmed();
        if (!key.isEmpty()) {
            keys.push_back(key);
        }
        if (keys.size() >= 5) {
            break;
        }
    }
    return keys;
}

QStringList toolNames(const QList<AgentToolSpec> &tools)
{
    QStringList names;
    for (const AgentToolSpec &tool : tools) {
        if (!tool.name.trimmed().isEmpty()) {
            names.push_back(tool.name.trimmed());
        }
    }
    return names;
}
}

QVariantMap SelectionTelemetryBuilder::basePayload(const QString &purpose,
                                                   const QString &inputPreview,
                                                   const QVariantMap &desktopContext,
                                                   const QString &desktopSummary)
{
    return {
        {QStringLiteral("purpose"), purpose},
        {QStringLiteral("inputPreview"), inputPreview.left(160)},
        {QStringLiteral("desktopSummary"), desktopSummary},
        {QStringLiteral("desktopTaskId"), desktopContext.value(QStringLiteral("taskId")).toString()},
        {QStringLiteral("desktopThreadId"), desktopContext.value(QStringLiteral("threadId")).toString()},
        {QStringLiteral("desktopTopic"), desktopContext.value(QStringLiteral("topic")).toString()}
    };
}

BehaviorTraceEvent SelectionTelemetryBuilder::toolPlanEvent(const QString &purpose,
                                                            const QString &inputPreview,
                                                            const QVariantMap &desktopContext,
                                                            const QString &desktopSummary,
                                                            const ToolPlan &plan)
{
    QVariantMap payload = basePayload(purpose, inputPreview, desktopContext, desktopSummary);
    payload.insert(QStringLiteral("goal"), plan.goal);
    payload.insert(QStringLiteral("rationale"), plan.rationale);
    payload.insert(QStringLiteral("orderedToolNames"), plan.orderedToolNames);
    payload.insert(QStringLiteral("requiresGrounding"), plan.requiresGrounding);
    payload.insert(QStringLiteral("sideEffecting"), plan.sideEffecting);
    payload.insert(QStringLiteral("candidateCount"), plan.candidates.size());
    payload.insert(QStringLiteral("candidates"), candidatePayload(plan.candidates));

    BehaviorTraceEvent event = BehaviorTraceEvent::create(
        QStringLiteral("selection_context"),
        QStringLiteral("tool_plan"),
        QStringLiteral("selection.tool_plan_compiled"),
        payload,
        QStringLiteral("system"));
    event.capabilityId = QStringLiteral("tool_selector");
    event.threadId = desktopContext.value(QStringLiteral("threadId")).toString();
    return event;
}

BehaviorTraceEvent SelectionTelemetryBuilder::memoryContextEvent(const QString &purpose,
                                                                 const QString &inputPreview,
                                                                 const QVariantMap &desktopContext,
                                                                 const QString &desktopSummary,
                                                                 const MemoryContext &memoryContext)
{
    QVariantMap payload = basePayload(purpose, inputPreview, desktopContext, desktopSummary);
    payload.insert(QStringLiteral("profileCount"), memoryContext.profile.size());
    payload.insert(QStringLiteral("activeCommitmentCount"), memoryContext.activeCommitments.size());
    payload.insert(QStringLiteral("episodicCount"), memoryContext.episodic.size());
    payload.insert(QStringLiteral("profileKeys"), recordKeys(memoryContext.profile));
    payload.insert(QStringLiteral("activeCommitmentKeys"), recordKeys(memoryContext.activeCommitments));
    payload.insert(QStringLiteral("episodicKeys"), recordKeys(memoryContext.episodic));

    int connectorMemoryCount = 0;
    int connectorSummaryCount = 0;
    for (const MemoryRecord &record : memoryContext.activeCommitments) {
        if (record.source.compare(QStringLiteral("connector_memory"), Qt::CaseInsensitive) == 0) {
            connectorMemoryCount += 1;
        }
        if (record.source.compare(QStringLiteral("connector_summary"), Qt::CaseInsensitive) == 0) {
            connectorSummaryCount += 1;
        }
    }
    payload.insert(QStringLiteral("connectorMemoryCount"), connectorMemoryCount);
    payload.insert(QStringLiteral("connectorSummaryCount"), connectorSummaryCount);

    BehaviorTraceEvent event = BehaviorTraceEvent::create(
        QStringLiteral("selection_context"),
        QStringLiteral("memory_context"),
        QStringLiteral("selection.memory_context_built"),
        payload,
        QStringLiteral("system"));
    event.capabilityId = QStringLiteral("memory_selector");
    event.threadId = desktopContext.value(QStringLiteral("threadId")).toString();
    return event;
}

BehaviorTraceEvent SelectionTelemetryBuilder::toolExposureEvent(const QString &purpose,
                                                                const QString &inputPreview,
                                                                const QVariantMap &desktopContext,
                                                                const QString &desktopSummary,
                                                                const QList<AgentToolSpec> &tools)
{
    QVariantMap payload = basePayload(purpose, inputPreview, desktopContext, desktopSummary);
    payload.insert(QStringLiteral("toolCount"), tools.size());
    payload.insert(QStringLiteral("selectedToolNames"), toolNames(tools));

    BehaviorTraceEvent event = BehaviorTraceEvent::create(
        QStringLiteral("selection_context"),
        QStringLiteral("tool_exposure"),
        QStringLiteral("selection.tools_exposed"),
        payload,
        QStringLiteral("system"));
    event.capabilityId = QStringLiteral("tool_selector");
    event.threadId = desktopContext.value(QStringLiteral("threadId")).toString();
    return event;
}
