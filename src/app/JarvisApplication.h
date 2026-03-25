#pragma once

#include <QObject>
#include <QPointer>
#include <memory>

class AssistantController;
class AppSettings;
class BackendFacade;
class IdentityProfileService;
class LoggingService;
class NativeHotkeyFilter;
class OverlayController;
class QQmlApplicationEngine;
class QQuickWindow;
class QSystemTrayIcon;
class QWindow;

class JarvisApplication : public QObject
{
    Q_OBJECT

public:
    explicit JarvisApplication(QObject *parent = nullptr);
    ~JarvisApplication() override;
    bool initialize();

private:
    std::unique_ptr<AppSettings> m_settings;
    std::unique_ptr<IdentityProfileService> m_identityProfileService;
    std::unique_ptr<LoggingService> m_loggingService;
    std::unique_ptr<AssistantController> m_assistantController;
    std::unique_ptr<OverlayController> m_overlayController;
    std::unique_ptr<BackendFacade> m_backendFacade;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    std::unique_ptr<QSystemTrayIcon> m_trayIcon;
    std::unique_ptr<NativeHotkeyFilter> m_hotkeyFilter;
    QPointer<QQuickWindow> m_mainWindow;
    QPointer<QQuickWindow> m_settingsWindow;
    QPointer<QQuickWindow> m_setupWindow;
    QPointer<QQuickWindow> m_toolsWindow;
};
