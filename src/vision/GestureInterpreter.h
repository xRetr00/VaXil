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
    void actionInterpreted(const QString &actionName,
                           const QString &sourceGesture,
                           double confidence,
                           const QString &traceId);
};
