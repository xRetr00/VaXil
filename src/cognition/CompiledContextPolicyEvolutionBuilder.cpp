#include "cognition/CompiledContextPolicyEvolutionBuilder.h"

#include <QDateTime>

namespace {
MemoryRecord makeRecord(const QString &key,
                        const QString &value,
                        double confidence,
                        const QString &updatedAt)
{
    return MemoryRecord{
        .type = QStringLiteral("context"),
        .key = key,
        .value = value.simplified(),
        .confidence = static_cast<float>(confidence),
        .source = QStringLiteral("compiled_history_policy_evolution"),
        .updatedAt = updatedAt
    };
}
}

QList<MemoryRecord> CompiledContextPolicyEvolutionBuilder::build(const QVariantList &history)
{
    if (history.isEmpty()) {
        return {};
    }

    QStringList modes;
    int totalObservations = 0;
    QVariantMap lastState;
    QVariantMap previousState;
    for (const QVariant &entry : history) {
        const QVariantMap state = entry.toMap();
        const QString mode = state.value(QStringLiteral("dominantMode")).toString().trimmed();
        if (mode.isEmpty()) {
            continue;
        }
        modes.push_back(mode);
        totalObservations += qMax(1, state.value(QStringLiteral("observations")).toInt());
        previousState = lastState;
        lastState = state;
    }

    if (modes.isEmpty()) {
        return {};
    }

    const QString updatedAt = QString::number(lastState.value(QStringLiteral("updatedAtMs"),
                                                              QDateTime::currentMSecsSinceEpoch()).toLongLong());
    QList<MemoryRecord> records;
    records.push_back(makeRecord(
        QStringLiteral("compiled_context_policy_evolution"),
        QStringLiteral("Policy evolution: %1. Current mode %2 observed %3 times across %4 mode shifts.")
            .arg(modes.join(QStringLiteral(" -> ")),
                 lastState.value(QStringLiteral("dominantMode")).toString().trimmed(),
                 QString::number(qMax(1, lastState.value(QStringLiteral("observations")).toInt())),
                 QString::number(qMax(0, modes.size() - 1))),
        0.83,
        updatedAt));

    if (!previousState.isEmpty()) {
        records.push_back(makeRecord(
            QStringLiteral("compiled_context_policy_transition"),
            QStringLiteral("Recent transition: %1 -> %2 after %3 total observations.")
                .arg(previousState.value(QStringLiteral("dominantMode")).toString().trimmed(),
                     lastState.value(QStringLiteral("dominantMode")).toString().trimmed(),
                     QString::number(totalObservations)),
            0.8,
            updatedAt));
    }

    return records;
}
