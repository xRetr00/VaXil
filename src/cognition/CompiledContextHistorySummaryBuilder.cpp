#include "cognition/CompiledContextHistorySummaryBuilder.h"

#include <algorithm>

namespace {
struct PurposeSummaryRow {
    QString purpose;
    QString summary;
    QStringList stableKeys;
    int stableCycles = 0;
    qint64 stableDurationMs = 0;
};

MemoryRecord buildPurposeRecord(const PurposeSummaryRow &row)
{
    QString value = row.summary.simplified();
    if (!row.stableKeys.isEmpty()) {
        value += QStringLiteral(" Stable keys: %1.").arg(row.stableKeys.join(QStringLiteral(", ")));
    }

    return MemoryRecord{
        .type = QStringLiteral("context"),
        .key = QStringLiteral("compiled_context_history_%1").arg(row.purpose),
        .value = value.simplified(),
        .confidence = 0.82f,
        .source = QStringLiteral("compiled_context_history"),
        .updatedAt = QString::number(row.stableDurationMs)
    };
}

QStringList historyRecordKeys(const QList<MemoryRecord> &records)
{
    QStringList keys;
    for (const MemoryRecord &record : records) {
        const QString key = record.key.trimmed();
        if (!key.isEmpty()) {
            keys.push_back(key);
        }
        if (keys.size() >= 5) {
            break;
        }
    }
    return keys;
}

bool historyContainsToken(const QList<MemoryRecord> &records, const QString &token)
{
    for (const MemoryRecord &record : records) {
        if (record.value.contains(token, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}
}

QList<MemoryRecord> CompiledContextHistorySummaryBuilder::build(
    const QHash<QString, QString> &summariesByPurpose,
    const QHash<QString, QStringList> &stableKeysByPurpose,
    const QHash<QString, int> &stableCyclesByPurpose,
    const QHash<QString, qint64> &stableDurationMsByPurpose)
{
    QList<PurposeSummaryRow> rows;
    for (auto it = summariesByPurpose.constBegin(); it != summariesByPurpose.constEnd(); ++it) {
        const QString purpose = it.key().trimmed();
        const QString summary = it.value().trimmed();
        const int stableCycles = stableCyclesByPurpose.value(purpose);
        const qint64 stableDurationMs = stableDurationMsByPurpose.value(purpose);
        if (purpose.isEmpty() || summary.isEmpty() || stableCycles <= 0 || stableDurationMs <= 0) {
            continue;
        }

        rows.push_back(PurposeSummaryRow{
            .purpose = purpose,
            .summary = summary,
            .stableKeys = stableKeysByPurpose.value(purpose),
            .stableCycles = stableCycles,
            .stableDurationMs = stableDurationMs
        });
    }

    std::sort(rows.begin(), rows.end(), [](const PurposeSummaryRow &left, const PurposeSummaryRow &right) {
        if (left.stableDurationMs != right.stableDurationMs) {
            return left.stableDurationMs > right.stableDurationMs;
        }
        return left.stableCycles > right.stableCycles;
    });

    QList<MemoryRecord> records;
    if (rows.isEmpty()) {
        return records;
    }

    QStringList globalParts;
    int purposeCount = 0;
    for (const PurposeSummaryRow &row : rows) {
        records.push_back(buildPurposeRecord(row));
        globalParts.push_back(QStringLiteral("%1(%2 cycles, %3 ms)")
                                  .arg(row.purpose)
                                  .arg(row.stableCycles)
                                  .arg(row.stableDurationMs));
        ++purposeCount;
        if (purposeCount >= 3) {
            break;
        }
    }

    records.push_front(MemoryRecord{
        .type = QStringLiteral("context"),
        .key = QStringLiteral("compiled_context_history_global"),
        .value = QStringLiteral("Stable compiled context across purposes: %1.")
                     .arg(globalParts.join(QStringLiteral("; "))),
        .confidence = 0.8f,
        .source = QStringLiteral("compiled_context_history"),
        .updatedAt = QString::number(rows.first().stableDurationMs)
    });
    return records;
}

QString CompiledContextHistorySummaryBuilder::buildSelectionHint(const QList<MemoryRecord> &records)
{
    if (records.isEmpty()) {
        return {};
    }

    QStringList parts;
    for (const MemoryRecord &record : records) {
        if (record.key == QStringLiteral("compiled_context_history_global")) {
            parts.push_back(record.value.simplified());
            break;
        }
    }
    for (const MemoryRecord &record : records) {
        if (record.key.startsWith(QStringLiteral("compiled_context_history_"))
            && record.key != QStringLiteral("compiled_context_history_global")) {
            parts.push_back(record.value.simplified());
            if (parts.size() >= 2) {
                break;
            }
        }
    }

    if (parts.isEmpty()) {
        return {};
    }
    return QStringLiteral("Long-horizon compiled context history: %1")
        .arg(parts.join(QStringLiteral(" ")))
        .simplified();
}

QVariantMap CompiledContextHistorySummaryBuilder::buildPlannerMetadata(const QList<MemoryRecord> &records)
{
    QVariantMap metadata;
    metadata.insert(QStringLiteral("compiledContextHistoryCount"), records.size());
    metadata.insert(QStringLiteral("compiledContextHistoryKeys"), historyRecordKeys(records));
    metadata.insert(QStringLiteral("compiledContextHistorySummary"), buildSelectionHint(records));
    metadata.insert(QStringLiteral("compiledContextHistoryHasDocument"),
                    historyContainsToken(records, QStringLiteral("desktop_context_document")));
    metadata.insert(QStringLiteral("compiledContextHistoryHasSchedule"),
                    historyContainsToken(records, QStringLiteral("connector_summary_schedule")));
    metadata.insert(QStringLiteral("compiledContextHistoryHasInbox"),
                    historyContainsToken(records, QStringLiteral("connector_summary_inbox")));
    metadata.insert(QStringLiteral("compiledContextHistoryHasResearch"),
                    historyContainsToken(records, QStringLiteral("connector_summary_research")));
    return metadata;
}
