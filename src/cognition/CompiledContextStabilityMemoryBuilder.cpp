#include "cognition/CompiledContextStabilityMemoryBuilder.h"

MemoryRecord CompiledContextStabilityMemoryBuilder::build(const QString &purpose,
                                                          const QString &summaryText,
                                                          const QStringList &stableKeys,
                                                          int stableCycles,
                                                          qint64 stableDurationMs)
{
    if (summaryText.trimmed().isEmpty() || stableCycles <= 0 || stableDurationMs <= 0) {
        return {};
    }

    QString value = summaryText.simplified();
    if (!stableKeys.isEmpty()) {
        value += QStringLiteral(" Stable keys: %1.").arg(stableKeys.join(QStringLiteral(", ")));
    }

    return MemoryRecord{
        .type = QStringLiteral("context"),
        .key = QStringLiteral("compiled_context_stability_%1").arg(purpose.trimmed().isEmpty()
                                                                       ? QStringLiteral("general")
                                                                       : purpose.trimmed()),
        .value = value.simplified(),
        .confidence = 0.84f,
        .source = QStringLiteral("compiled_context_stability"),
        .updatedAt = QString::number(stableDurationMs)
    };
}
