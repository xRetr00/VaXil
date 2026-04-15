#include "cognition/CompiledContextLayeredSignalBuilder.h"

#include <QDateTime>

namespace {
QString recordValue(const QList<MemoryRecord> &records, const QString &key)
{
    for (const MemoryRecord &record : records) {
        if (record.key == key) {
            return record.value.trimmed();
        }
    }
    return {};
}
}

QString CompiledContextLayeredSignalBuilder::buildSelectionDirective(const QList<MemoryRecord> &records)
{
    const QString focus = recordValue(records, QStringLiteral("compiled_context_layered_focus"));
    const QString continuity = recordValue(records, QStringLiteral("compiled_context_layered_continuity"));
    const QString evolution = recordValue(records, QStringLiteral("compiled_context_policy_evolution"));
    if (focus.isEmpty() && continuity.isEmpty() && evolution.isEmpty()) {
        return {};
    }

    return QStringLiteral("%1 %2 %3").arg(focus, continuity, evolution).simplified();
}

QString CompiledContextLayeredSignalBuilder::buildPlannerSummary(const QList<MemoryRecord> &records)
{
    QStringList values;
    for (const MemoryRecord &record : records) {
        if (!record.value.trimmed().isEmpty()) {
            values.push_back(record.value.simplified());
        }
    }
    return values.join(QStringLiteral(" ")).simplified();
}

QStringList CompiledContextLayeredSignalBuilder::buildPlannerKeys(const QList<MemoryRecord> &records)
{
    QStringList keys;
    for (const MemoryRecord &record : records) {
        if (!record.key.trimmed().isEmpty()) {
            keys.push_back(record.key.trimmed());
        }
    }
    return keys;
}

QList<MemoryRecord> CompiledContextLayeredSignalBuilder::buildPromptContextRecords(
    const QList<MemoryRecord> &records)
{
    const QString updatedAt = QString::number(QDateTime::currentMSecsSinceEpoch());
    QList<MemoryRecord> promptRecords;

    const QString focus = recordValue(records, QStringLiteral("compiled_context_layered_focus"));
    if (!focus.isEmpty()) {
        promptRecords.push_back({
            .type = QStringLiteral("prompt_context"),
            .key = QStringLiteral("history_prompt_layered_focus"),
            .value = focus,
            .confidence = 0.84f,
            .source = QStringLiteral("prompt.layered_focus"),
            .updatedAt = updatedAt
        });
    }

    const QString continuity = recordValue(records, QStringLiteral("compiled_context_layered_continuity"));
    if (!continuity.isEmpty()) {
        promptRecords.push_back({
            .type = QStringLiteral("prompt_context"),
            .key = QStringLiteral("history_prompt_layered_continuity"),
            .value = continuity,
            .confidence = 0.8f,
            .source = QStringLiteral("prompt.layered_continuity"),
            .updatedAt = updatedAt
        });
    }

    const QString evolution = recordValue(records, QStringLiteral("compiled_context_policy_evolution"));
    if (!evolution.isEmpty()) {
        promptRecords.push_back({
            .type = QStringLiteral("prompt_context"),
            .key = QStringLiteral("history_prompt_policy_evolution"),
            .value = evolution,
            .confidence = 0.78f,
            .source = QStringLiteral("prompt.policy_evolution"),
            .updatedAt = updatedAt
        });
    }

    return promptRecords;
}
