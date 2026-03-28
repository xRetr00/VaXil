#pragma once

#include <QObject>

#include "core/AssistantTypes.h"

class GestureInterpreter final : public QObject
{
    Q_OBJECT

public:
    explicit GestureInterpreter(QObject *parent = nullptr);

public slots:
    void ingestSnapshot(const VisionSnapshot &snapshot);

signals:
    void observationsInterpreted(const QList<GestureObservation> &observations,
                                 qint64 timestampMs,
                                 const QString &traceId);
};
