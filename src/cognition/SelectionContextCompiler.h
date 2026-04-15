#pragma once

#include <QList>
#include <QVariantMap>

#include "core/AssistantTypes.h"

class AssistantBehaviorPolicy;
class MemoryPolicyHandler;

struct SelectionContextCompilation {
    QString compiledDesktopSummary;
    QString selectionInput;
    QString promptContext;
    QList<MemoryRecord> selectedMemoryRecords;
    QList<MemoryRecord> compiledContextRecords;
    MemoryContext memoryContext;
};

class SelectionContextCompiler
{
public:
    [[nodiscard]] static QString buildCompiledDesktopSummary(const QVariantMap &desktopContext,
                                                             const QString &desktopSummary);
    [[nodiscard]] static QString buildSelectionInput(const QString &input,
                                                     IntentType intent,
                                                     const QVariantMap &desktopContext,
                                                     const QString &desktopSummary,
                                                     qint64 desktopContextAtMs,
                                                     bool privateModeEnabled);
    [[nodiscard]] static QString buildPromptContext(const QString &desktopSummary,
                                                    const QVariantMap &desktopContext);
    [[nodiscard]] static SelectionContextCompilation compile(const QString &query,
                                                             IntentType intent,
                                                             const QVariantMap &desktopContext,
                                                             const QString &desktopSummary,
                                                             qint64 desktopContextAtMs,
                                                             bool privateModeEnabled,
                                                             const MemoryRecord &runtimeRecord,
                                                             const MemoryPolicyHandler *memoryPolicyHandler,
                                                             const AssistantBehaviorPolicy *behaviorPolicy);
};
