#pragma once

#include <QString>
#include <QVariantMap>

#include "core/AssistantTypes.h"

class DesktopContextSelectionBuilder
{
public:
    [[nodiscard]] static QString buildSelectionInput(const QString &userInput,
                                                     IntentType intent,
                                                     const QString &desktopSummary,
                                                     const QVariantMap &desktopContext,
                                                     qint64 contextAtMs,
                                                     qint64 nowMs,
                                                     bool privateModeEnabled);
    [[nodiscard]] static double contextRelevanceScore(const QString &userInput,
                                                      IntentType intent,
                                                      const QVariantMap &desktopContext);
    [[nodiscard]] static QString contextInjectionReason(const QString &userInput,
                                                        IntentType intent,
                                                        const QVariantMap &desktopContext);
    [[nodiscard]] static double minimumInjectionScore();

private:
    [[nodiscard]] static QString buildHint(const QString &desktopSummary,
                                           const QVariantMap &desktopContext);
    [[nodiscard]] static QString inferredWorkMode(const QVariantMap &desktopContext);
    [[nodiscard]] static bool isNoisyClipboardContext(const QVariantMap &desktopContext);
    [[nodiscard]] static bool shouldUseDesktopContext(const QString &userInput,
                                                      IntentType intent,
                                                      const QVariantMap &desktopContext);
};
