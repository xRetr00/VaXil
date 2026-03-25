#pragma once

#include <QObject>

#include "core/AssistantTypes.h"

class AppSettings;
class AiBackendClient;

class ModelCatalogService : public QObject
{
    Q_OBJECT

public:
    ModelCatalogService(AppSettings *settings, AiBackendClient *client, QObject *parent = nullptr);

    QList<ModelInfo> models() const;
    AiAvailability availability() const;
    void refresh();
    bool selectedModelValid() const;

signals:
    void modelsChanged();
    void availabilityChanged();

private:
    AppSettings *m_settings = nullptr;
    AiBackendClient *m_client = nullptr;
    QList<ModelInfo> m_models;
    AiAvailability m_availability;
};
