#include "app/JarvisApplication.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QSharedPointer>
#include <QDateTime>
#include <QDebug>
#include <QCoreApplication>
#include <QMenu>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QWindow>
#include <functional>

#include "audio/AudioProcessingTypes.h"
#include "core/AssistantController.h"
#include "core/AssistantTypes.h"
#include "gui/AgentViewModel.h"
#include "gui/BackendFacade.h"
#include "gui/SettingsViewModel.h"
#include "gui/TaskViewModel.h"
#include "logging/LoggingService.h"
#include "overlay/OverlayController.h"
#include "platform/PlatformRuntime.h"
#include "settings/AppSettings.h"
#include "settings/IdentityProfileService.h"
#include "tools/ToolManager.h"

#ifdef Q_OS_WIN
#include <windows.h>

class NativeHotkeyFilter final : public QAbstractNativeEventFilter
{
public:
    explicit NativeHotkeyFilter(std::function<void()> callback)
        : m_callback(std::move(callback))
    {
        RegisterHotKey(nullptr, 1, MOD_CONTROL | MOD_ALT, 0x4A);
    }

    ~NativeHotkeyFilter() override
    {
        UnregisterHotKey(nullptr, 1);
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
    {
        Q_UNUSED(eventType)
        auto *msg = static_cast<MSG *>(message);
        if (msg->message == WM_HOTKEY && msg->wParam == 1) {
            m_callback();
            if (result) {
                *result = 0;
            }
            return true;
        }
        return false;
    }

private:
    std::function<void()> m_callback;
};
#endif

JarvisApplication::JarvisApplication(QObject *parent)
    : QObject(parent)
{
}

JarvisApplication::~JarvisApplication() = default;

bool JarvisApplication::initialize()
{
    qRegisterMetaType<AudioFrame>("AudioFrame");
    qRegisterMetaType<AudioProcessingConfig>("AudioProcessingConfig");
    qRegisterMetaType<AiMessage>("AiMessage");
    qRegisterMetaType<QList<AiMessage>>("QList<AiMessage>");
    qRegisterMetaType<VisionObjectDetection>("VisionObjectDetection");
    qRegisterMetaType<QList<VisionObjectDetection>>("QList<VisionObjectDetection>");
    qRegisterMetaType<VisionGestureDetection>("VisionGestureDetection");
    qRegisterMetaType<QList<VisionGestureDetection>>("QList<VisionGestureDetection>");
    qRegisterMetaType<VisionSnapshot>("VisionSnapshot");
    qRegisterMetaType<GestureLifecycleState>("GestureLifecycleState");
    qRegisterMetaType<GestureEventType>("GestureEventType");
    qRegisterMetaType<GestureObservation>("GestureObservation");
    qRegisterMetaType<QList<GestureObservation>>("QList<GestureObservation>");
    qRegisterMetaType<GestureEvent>("GestureEvent");
    qRegisterMetaType<ModelInfo>("ModelInfo");
    qRegisterMetaType<QList<ModelInfo>>("QList<ModelInfo>");
    qRegisterMetaType<AiAvailability>("AiAvailability");
    qRegisterMetaType<TranscriptionResult>("TranscriptionResult");
    qRegisterMetaType<AiRequestOptions>("AiRequestOptions");
    qRegisterMetaType<ToolInfo>("ToolInfo");

    qInfo() << "Loading settings";
    m_settings = std::make_unique<AppSettings>();
    if (!m_settings->load()) {
        qCritical() << "Failed to load settings";
        return false;
    }
    qInfo() << "Settings loaded from" << m_settings->storagePath();

    qInfo() << "Loading identity/profile configuration";
    m_identityProfileService = std::make_unique<IdentityProfileService>();
    if (!m_identityProfileService->initialize()) {
        qCritical() << "Failed to load identity/profile configuration";
        return false;
    }

    qInfo() << "Initializing logging";
    m_loggingService = std::make_unique<LoggingService>();
    if (!m_loggingService->initialize()) {
        qCritical() << "Failed to initialize logging";
        return false;
    }
    qInfo() << "Application log file:" << m_loggingService->logFilePath();

    qInfo() << "Building core services";
    m_assistantController = std::make_unique<AssistantController>(
        m_settings.get(),
        m_identityProfileService.get(),
        m_loggingService.get());
    m_overlayController = std::make_unique<OverlayController>();
    m_backendFacade = std::make_unique<BackendFacade>(
        m_settings.get(),
        m_identityProfileService.get(),
        m_assistantController.get(),
        m_overlayController.get());
    m_agentViewModel = std::make_unique<AgentViewModel>(m_backendFacade.get());
    m_settingsViewModel = std::make_unique<SettingsViewModel>(m_backendFacade.get());
    m_taskViewModel = std::make_unique<TaskViewModel>(m_backendFacade.get());
    m_engine = std::make_unique<QQmlApplicationEngine>();
    const QIcon appIcon(QStringLiteral(":/qt/qml/JARVIS/gui/assets/icon.ico"));
    m_trayIcon = std::make_unique<QSystemTrayIcon>(appIcon.isNull() ? qApp->style()->standardIcon(QStyle::SP_ComputerIcon) : appIcon, this);

    m_engine->rootContext()->setContextProperty(QStringLiteral("backend"), m_backendFacade.get());
    m_engine->rootContext()->setContextProperty(QStringLiteral("agentVm"), m_agentViewModel.get());
    m_engine->rootContext()->setContextProperty(QStringLiteral("settingsVm"), m_settingsViewModel.get());
    m_engine->rootContext()->setContextProperty(QStringLiteral("taskVm"), m_taskViewModel.get());
    qInfo() << "Loading QML windows";
    m_engine->load(QUrl(QStringLiteral("qrc:/qt/qml/JARVIS/gui/qml/OverlayWindow.qml")));
    m_engine->load(QUrl(QStringLiteral("qrc:/qt/qml/JARVIS/gui/qml/SettingsWindow.qml")));
    m_engine->load(QUrl(QStringLiteral("qrc:/qt/qml/JARVIS/gui/qml/SetupWizard.qml")));
    m_engine->load(QUrl(QStringLiteral("qrc:/qt/qml/JARVIS/gui/qml/ToolsHubWindow.qml")));
    if (m_engine->rootObjects().isEmpty()) {
        qCritical() << "QML load failed: no root objects were created";
        return false;
    }

    if (auto *window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first())) {
        m_mainWindow = window;
        m_overlayController->attachWindow(window);
        m_overlayController->setClickThrough(true);
        if (!appIcon.isNull()) {
            window->setIcon(appIcon);
        }
        window->setColor(Qt::transparent);
        window->hide();
    }
    if (m_engine->rootObjects().size() > 1) {
        m_settingsWindow = qobject_cast<QQuickWindow *>(m_engine->rootObjects().at(1));
        if (m_settingsWindow) {
            if (!appIcon.isNull()) {
                m_settingsWindow->setIcon(appIcon);
            }
            m_settingsWindow->hide();
        }
    }
    if (m_engine->rootObjects().size() > 2) {
        m_setupWindow = qobject_cast<QQuickWindow *>(m_engine->rootObjects().at(2));
        if (m_setupWindow) {
            if (!appIcon.isNull()) {
                m_setupWindow->setIcon(appIcon);
            }
            m_setupWindow->hide();
        }
    }
    if (m_engine->rootObjects().size() > 3) {
        m_toolsWindow = qobject_cast<QQuickWindow *>(m_engine->rootObjects().at(3));
        if (m_toolsWindow) {
            if (!appIcon.isNull()) {
                m_toolsWindow->setIcon(appIcon);
            }
            m_toolsWindow->hide();
        }
    }
    if (m_setupWindow) {
        connect(m_setupWindow, &QWindow::visibleChanged, this, [this]() {
            m_overlayController->setSetupVisible(m_setupWindow && m_setupWindow->isVisible());
        });
    }

#ifdef Q_OS_WIN
    m_hotkeyFilter = std::make_unique<NativeHotkeyFilter>([this]() {
        m_overlayController->toggleOverlay();
    });
    QCoreApplication::instance()->installNativeEventFilter(m_hotkeyFilter.get());
#endif
    auto *trayMenu = new QMenu();
    trayMenu->addAction(QStringLiteral("Toggle Overlay"), [this]() {
        m_overlayController->toggleOverlay();
    });
    trayMenu->addAction(QStringLiteral("Settings"), [this]() {
        if (m_settingsWindow) {
            m_settingsWindow->show();
            m_settingsWindow->raise();
            m_settingsWindow->requestActivate();
        }
    });
    trayMenu->addAction(QStringLiteral("Setup"), [this]() {
        if (m_setupWindow) {
            m_setupWindow->show();
            m_setupWindow->raise();
            m_setupWindow->requestActivate();
        }
    });
    trayMenu->addAction(QStringLiteral("Tools && Stores"), [this]() {
        if (m_toolsWindow) {
            m_toolsWindow->show();
            m_toolsWindow->raise();
            m_toolsWindow->requestActivate();
        }
    });
    trayMenu->addSeparator();
    trayMenu->addAction(QStringLiteral("Quit"), qApp, &QCoreApplication::quit);
    m_trayIcon->setContextMenu(trayMenu);
    m_trayIcon->show();
    connect(m_trayIcon.get(), &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            if (m_setupWindow && m_setupWindow->isVisible()) {
                m_setupWindow->raise();
                m_setupWindow->requestActivate();
                return;
            }
            m_overlayController->toggleOverlay();
        }
    });

    connect(m_assistantController.get(), &AssistantController::stateChanged, this, [this]() {
        m_overlayController->setAssistantState(m_assistantController->stateName());
    });

    auto startupAnnouncementSent = QSharedPointer<bool>::create(false);
    auto lastStartupIssue = QSharedPointer<QString>::create(QString());
    auto lastBlockedIssueNotified = QSharedPointer<QString>::create(QString());
    auto lastBlockedNotificationAtMs = QSharedPointer<qint64>::create(0);
    const PlatformCapabilities platformCapabilities = PlatformRuntime::currentCapabilities();
    const auto updateStartupPresentation = [this, startupAnnouncementSent, lastStartupIssue, lastBlockedIssueNotified, lastBlockedNotificationAtMs, platformCapabilities]() {
        if (!m_settings->initialSetupCompleted()) {
            return;
        }

        constexpr qint64 kBlockedNotificationCooldownMs = 15000;

        const QString issue = m_assistantController->startupBlockingIssue().trimmed();
        if (m_assistantController->startupReady()) {
            if (!*startupAnnouncementSent) {
                const QString readyMessage = platformCapabilities.supportsGlobalHotkey
                    ? QStringLiteral("Running in tray. Use Ctrl+Alt+J or the tray icon.")
                    : QStringLiteral("Running in tray. Use the tray icon to toggle the overlay.");
                qInfo() << "Startup complete. App is running in the tray." << readyMessage;
                m_trayIcon->showMessage(
                    QStringLiteral("Vaxil"),
                    readyMessage,
                    QSystemTrayIcon::Information,
                    5000);
                *startupAnnouncementSent = true;
                *lastStartupIssue = QString();
            }
            if (m_setupWindow && m_setupWindow->isVisible()) {
                m_setupWindow->hide();
            }
            return;
        }

        // Once startup is complete, avoid re-entering startup-pending/startup-blocked UX
        // for transient backend health fluctuations.
        if (*startupAnnouncementSent) {
            return;
        }

        if (m_assistantController->startupBlocked()) {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const bool issueChanged = (*lastBlockedIssueNotified != issue);
            const bool cooldownElapsed = (nowMs - *lastBlockedNotificationAtMs) >= kBlockedNotificationCooldownMs;
            if (issueChanged || cooldownElapsed) {
                qWarning() << "Startup blocked:" << issue;
                m_trayIcon->showMessage(
                    QStringLiteral("Vaxil"),
                    issue.isEmpty() ? QStringLiteral("Startup is blocked. Open setup to fix the missing components.") : issue,
                    QSystemTrayIcon::Warning,
                    7000);
                *lastBlockedIssueNotified = issue;
                *lastBlockedNotificationAtMs = nowMs;
            }
            *lastStartupIssue = issue;
            return;
        }

        if (*lastStartupIssue != issue) {
            qInfo() << "Startup pending:" << issue;
            *lastStartupIssue = issue;
        }
    };
    connect(m_assistantController.get(), &AssistantController::startupStateChanged, this, updateStartupPresentation);

    m_assistantController->initialize();
    m_overlayController->setAssistantState(m_assistantController->stateName());
    if (!m_settings->initialSetupCompleted() && m_setupWindow) {
        qInfo() << "First run detected. Opening setup wizard.";
        m_setupWindow->show();
        m_setupWindow->raise();
        m_setupWindow->requestActivate();
    } else {
        qInfo() << "Startup checks running. Waiting for wake and AI backend readiness.";
        updateStartupPresentation();
    }

    connect(m_backendFacade.get(), &BackendFacade::initialSetupFinished, this, [this]() {
        if (m_setupWindow) {
            m_setupWindow->hide();
        }
        m_overlayController->showOverlay();
        m_assistantController->startWakeMonitor();
        qInfo() << "Initial setup completed. Waiting for services readiness.";
    });
    connect(m_backendFacade.get(), &BackendFacade::toolsWindowRequested, this, [this]() {
        if (m_toolsWindow) {
            m_toolsWindow->show();
            m_toolsWindow->raise();
            m_toolsWindow->requestActivate();
        }
    });
    return true;
}
