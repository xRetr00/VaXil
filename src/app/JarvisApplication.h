#pragma once

#include <QObject>
#include <QPointer>
#include <memory>

class AssistantController;
class AppSettings;
class AgentViewModel;
class BackendFacade;
class IdentityProfileService;
class LoggingService;
#ifdef Q_OS_WIN
class NativeHotkeyFilter;
#endif
class OverlayController;
class QQmlApplicationEngine;
class QQuickWindow;
class SettingsViewModel;
class QSystemTrayIcon;
class TaskViewModel;
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
    std::unique_ptr<AgentViewModel> m_agentViewModel;
    std::unique_ptr<SettingsViewModel> m_settingsViewModel;
    std::unique_ptr<TaskViewModel> m_taskViewModel;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    std::unique_ptr<QSystemTrayIcon> m_trayIcon;
#ifdef Q_OS_WIN
    std::unique_ptr<NativeHotkeyFilter> m_hotkeyFilter;
#endif
    QPointer<QQuickWindow> m_mainWindow;
    QPointer<QQuickWindow> m_settingsWindow;
    QPointer<QQuickWindow> m_setupWindow;
    QPointer<QQuickWindow> m_toolsWindow;
};
