#include "vision/GestureInterpreter.h"

#include <QDateTime>
#include <QHash>

namespace {
QString mapGestureToAction(const QString &gestureName)
{
    const QString normalized = gestureName.trimmed().toLower();
    if (normalized == QStringLiteral("pinch")) {
        return QStringLiteral("click");
    }
    if (normalized == QStringLiteral("open_hand") || normalized == QStringLiteral("open_palm")) {
        return QStringLiteral("cancel");
    }
    if (normalized == QStringLiteral("two_fingers")) {
        return QStringLiteral("scroll");
    }
    if (normalized == QStringLiteral("start_listening")) {
        return QStringLiteral("start_listening");
    }
    if (normalized == QStringLiteral("stop_listening")) {
        return QStringLiteral("stop_listening");
    }
    if (normalized == QStringLiteral("cancel_request")) {
        return QStringLiteral("cancel");
    }
    return {};
}
}

GestureInterpreter::GestureInterpreter(QObject *parent)
    : QObject(parent)
{
}

void GestureInterpreter::ingestSnapshot(const VisionSnapshot &snapshot)
{
    const qint64 timestampMs = snapshot.timestamp.isValid()
        ? snapshot.timestamp.toUTC().toMSecsSinceEpoch()
        : QDateTime::currentMSecsSinceEpoch();
    QHash<QString, QPair<QString, double>> bestActions;
    for (const auto &gesture : snapshot.gestures) {
        const QString action = mapGestureToAction(gesture.name);
        if (action.isEmpty()) {
            continue;
        }

        const auto existing = bestActions.constFind(action);
        if (existing == bestActions.constEnd() || gesture.confidence > existing->second) {
            bestActions.insert(action, qMakePair(gesture.name, gesture.confidence));
        }
    }

    QList<GestureObservation> observations;
    observations.reserve(bestActions.size());
    for (auto it = bestActions.cbegin(); it != bestActions.cend(); ++it) {
        observations.push_back({
            .actionName = it.key(),
            .sourceGesture = it.value().first,
            .confidence = it.value().second
        });
    }
    emit observationsInterpreted(observations, timestampMs, snapshot.traceId);
}
