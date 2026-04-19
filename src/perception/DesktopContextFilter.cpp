#include "perception/DesktopContextFilter.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace {
QString normalizeSegment(QString value)
{
    value = value.trimmed().toLower();
    if (value.isEmpty()) {
        return {};
    }
    value.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    value.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    while (value.startsWith(QLatin1Char('_'))) {
        value.remove(0, 1);
    }
    while (value.endsWith(QLatin1Char('_'))) {
        value.chop(1);
    }
    return value;
}

QString appFamily(const QString &appId)
{
    const QString baseName = QFileInfo(appId.trimmed()).completeBaseName();
    return normalizeSegment(baseName.isEmpty() ? appId : baseName);
}

bool isVaxilProcess(const QString &appId)
{
    const QString family = appFamily(appId);
    return family == QStringLiteral("vaxil")
        || family == QStringLiteral("vaxil_ii")
        || family == QStringLiteral("jarvis")
        || family == QStringLiteral("vaxil_python_runtime")
        || family == QStringLiteral("vaxil_action_runner");
}

bool hasVaxilSurfaceName(const QString &value)
{
    const QString normalized = normalizeSegment(value);
    if (normalized.isEmpty()) {
        return false;
    }
    return normalized == QStringLiteral("vaxil")
        || normalized == QStringLiteral("vaxil_command_deck")
        || normalized.contains(QStringLiteral("vaxil_command_deck"))
        || normalized.contains(QStringLiteral("vaxil_overlay"))
        || normalized.contains(QStringLiteral("vaxil_settings"))
        || normalized.contains(QStringLiteral("vaxil_setup"))
        || normalized.contains(QStringLiteral("vaxil_notification"));
}

bool metadataMarksVaxilSurface(const QVariantMap &metadata)
{
    static const QStringList keys{
        QStringLiteral("documentContext"),
        QStringLiteral("windowTitle"),
        QStringLiteral("focusedElementName"),
        QStringLiteral("metadataClass"),
        QStringLiteral("source")
    };
    for (const QString &key : keys) {
        if (hasVaxilSurfaceName(metadata.value(key).toString())) {
            return true;
        }
    }
    return false;
}
}

DesktopContextFilterDecision DesktopContextFilter::evaluate(const DesktopContextFilterInput &input)
{
    DesktopContextFilterDecision decision;
    const QString sourceKind = input.sourceKind.trimmed().toLower();

    const bool appOwnedByVaxil = isVaxilProcess(input.appId);
    const bool namedVaxilSurface = hasVaxilSurfaceName(input.windowTitle)
        || hasVaxilSurfaceName(input.notificationTitle)
        || metadataMarksVaxilSurface(input.metadata);

    if (sourceKind == QStringLiteral("notification")
        && (appOwnedByVaxil || hasVaxilSurfaceName(input.notificationTitle))) {
        decision.accepted = false;
        decision.diagnosticOnly = true;
        decision.reasonCode = QStringLiteral("desktop_context.filtered_self_notification");
        return decision;
    }

    if (sourceKind == QStringLiteral("active_window")
        && (appOwnedByVaxil || namedVaxilSurface)) {
        decision.accepted = false;
        decision.diagnosticOnly = true;
        decision.reasonCode = QStringLiteral("desktop_context.filtered_self_window");
        return decision;
    }

    decision.reasonCode = QStringLiteral("desktop_context.accepted");
    return decision;
}
