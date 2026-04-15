#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "core/AssistantTypes.h"

class CompiledContextHistorySummaryBuilder
{
public:
    [[nodiscard]] static QList<MemoryRecord> build(const QHash<QString, QString> &summariesByPurpose,
                                                   const QHash<QString, QStringList> &stableKeysByPurpose,
                                                   const QHash<QString, int> &stableCyclesByPurpose,
                                                   const QHash<QString, qint64> &stableDurationMsByPurpose);
    [[nodiscard]] static QString buildSelectionHint(const QList<MemoryRecord> &records);
    [[nodiscard]] static QVariantMap buildPlannerMetadata(const QList<MemoryRecord> &records);
};
