#include "cognition/FocusModePolicy.h"

bool FocusModePolicy::allows(const FocusModeState &state, const QString &priority) const
{
    if (!state.enabled) {
        return true;
    }

    const QString normalized = priority.trimmed().toLower();
    if (normalized == QStringLiteral("critical")) {
        return state.allowCriticalAlerts;
    }

    return false;
}
