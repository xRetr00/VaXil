#include "app/JarvisApplication.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QSharedPointer>
#include <QDateTime>
#include <QDebug>
#include <QCoreApplication>
#include <QEvent>
#include <QMenu>
#include <QIcon>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QWindow>
#include <functional>

#include "diagnostics/CrashDiagnosticsService.h"
#include "diagnostics/StartupMilestones.h"
#include "diagnostics/VaxilErrorCodes.h"
#include "audio/AudioProcessingTypes.h"
#include "core/AssistantController.h"
#include "core/AssistantTypes.h"
#include "gui/AgentViewModel.h"
#include "gui/BackendFacade.h"
#include "gui/SettingsViewModel.h"
#include "gui/TaskViewModel.h"
#include "logging/LoggingService.h"
#include "overlay/OverlayController.h"
#include "perception/DesktopPerceptionMonitor.h"
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

QString graphicsApiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::GraphicsApi::Direct3D11Rhi:
        return QStringLiteral("Direct3D11Rhi");
    case QSGRendererInterface::GraphicsApi::Direct3D12:
        return QStringLiteral("Direct3D12");
    case QSGRendererInterface::GraphicsApi::MetalRhi:
        return QStringLiteral("MetalRhi");
    case QSGRendererInterface::GraphicsApi::NullRhi:
        return QStringLiteral("NullRhi");
    case QSGRendererInterface::GraphicsApi::OpenGL:
        return QStringLiteral("OpenGL");
    case QSGRendererInterface::GraphicsApi::OpenVG:
        return QStringLiteral("OpenVG");
    case QSGRendererInterface::GraphicsApi::Software:
        return QStringLiteral("Software");
    case QSGRendererInterface::GraphicsApi::VulkanRhi:
        return QStringLiteral("VulkanRhi");
    case QSGRendererInterface::GraphicsApi::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

QString sceneGraphErrorName(QQuickWindow::SceneGraphError error)
{
    switch (error) {
    case QQuickWindow::SceneGraphError::ContextNotAvailable:
        return QStringLiteral("ContextNotAvailable");
    default:
        return QStringLiteral("UnknownSceneGraphError");
    }
}

QString formatQmlError(const QQmlError &error)
{
    const QString url = error.url().isValid() ? error.url().toString() : QStringLiteral("<unknown>");
    return QStringLiteral("%1:%2:%3 %4")
        .arg(url)
        .arg(error.line())
        .arg(error.column())
        .arg(error.description().trimmed());
}

bool isOrbRelatedQmlError(const QQmlError &error)
{
    const QString url = error.url().toString().toLower();
    const QString description = error.description().toLower();
    return url.contains(QStringLiteral("orbrenderer.qml"))
        || url.contains(QStringLiteral("orb.frag"))
        || description.contains(QStringLiteral("shadereffect"))
        || description.contains(QStringLiteral("orb renderer"))
        || description.contains(QStringLiteral("orb.frag"));
}

JarvisApplication::JarvisApplication(QObject *parent)
    : QObject(parent)
{
}

JarvisApplication::~JarvisApplication()
{
    CrashDiagnosticsService::instance().markStartupMilestone(StartupMilestones::shutdownCompleted(),
                                                             QStringLiteral("JarvisApplication destroyed"),
                                                             true);
}

bool JarvisApplication::eventFilter(QObject *watched, QEvent *event)
{
    if (event != nullptr && event->type() == QEvent::Hide) {
        auto *window = qobject_cast<QWindow *>(watched);
        const bool isManagedDesktopWindow = window != nullptr
            && (window == m_settingsWindow
                || window == m_setupWindow
                || window == m_toolsWindow
                || window == m_fullUiWindow);
        if (isManagedDesktopWindow
            && event->spontaneous()
            && (window->windowState() & Qt::WindowMinimized)) {
            QPointer<QWindow> guardedWindow(window);
            QMetaObject::invokeMethod(this, [guardedWindow]() {
                if (!guardedWindow || guardedWindow->isVisible()) {
                    return;
                }
                if (!(guardedWindow->windowState() & Qt::WindowMinimized)) {
                    return;
                }
                guardedWindow->showMinimized();
            }, Qt::QueuedConnection);
        }
    }

    return QObject::eventFilter(watched, event);
}

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
    qRegisterMetaType<ConnectorEvent>("ConnectorEvent");
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
        CrashDiagnosticsService::instance().markStartupMilestone(
            StartupMilestones::startupLoggingReady(),
            QStringLiteral("logging.initialize=false"),
            false);
        return false;
    }
    CrashDiagnosticsService::instance().markStartupMilestone(StartupMilestones::startupLoggingReady(),
                                                             QStringLiteral("logging.initialize=true"),
                                                             true);
    m_loggingService->breadcrumb(QStringLiteral("startup"),
                                 StartupMilestones::startupLoggingReady(),
                                 QStringLiteral("Logging service initialized"));
    qInfo() << "Application log file:" << m_loggingService->logFilePath();

    qInfo() << "Building core services";
    m_assistantController = std::make_unique<AssistantController>(
        m_settings.get(),
        m_identityProfileService.get(),
        m_loggingService.get());
    m_overlayController = std::make_unique<OverlayController>();
    m_desktopPerceptionMonitor = std::make_unique<DesktopPerceptionMonitor>(
        m_settings.get(),
        m_loggingService.get(),
        this);
    m_backendFacade = std::make_unique<BackendFacade>(
        m_settings.get(),
        m_identityProfileService.get(),
        m_assistantController.get(),
        m_overlayController.get(),
        m_loggingService.get());
    connect(m_desktopPerceptionMonitor.get(), &DesktopPerceptionMonitor::desktopContextUpdated, this,
            [this](const QString &summary, const QVariantMap &context) {
                if (m_assistantController) {
                    m_assistantController->updateDesktopContext(summary, context);
                }
            });
    m_agentViewModel = std::make_unique<AgentViewModel>(m_backendFacade.get());
    m_settingsViewModel = std::make_unique<SettingsViewModel>(m_backendFacade.get());
    m_taskViewModel = std::make_unique<TaskViewModel>(m_backendFacade.get());
    m_engine = std::make_unique<QQmlApplicationEngine>();
    const QIcon appIcon(QStringLiteral(":/qt/qml/VAXIL/gui/assets/icon.ico"));
    m_trayIcon = std::make_unique<QSystemTrayIcon>(appIcon.isNull() ? qApp->style()->standardIcon(QStyle::SP_ComputerIcon) : appIcon, this);

    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &warnings) {
        if (m_loggingService == nullptr) {
            return;
        }

        for (const QQmlError &warning : warnings) {
            const QString message = QStringLiteral("[orb] qml_warning %1").arg(formatQmlError(warning));
            if (isOrbRelatedQmlError(warning)) {
                m_loggingService->errorFor(QStringLiteral("orb_render"), message);
            } else {
                m_loggingService->warn(message);
            }
        }
    });

    m_engine->rootContext()->setContextProperty(QStringLiteral("backend"), m_backendFacade.get());
    m_engine->rootContext()->setContextProperty(QStringLiteral("agentVm"), m_agentViewModel.get());
    m_engine->rootContext()->setContextProperty(QStringLiteral("settingsVm"), m_settingsViewModel.get());
    m_engine->rootContext()->setContextProperty(QStringLiteral("taskVm"), m_taskViewModel.get());
    CrashDiagnosticsService::instance().markStartupMilestone(StartupMilestones::startupOverlayBegin(),
                                                             QStringLiteral("Loading QML windows"),
                                                             true);
    m_loggingService->breadcrumb(QStringLiteral("startup"),
                                 StartupMilestones::startupOverlayBegin(),
                                 QStringLiteral("qml.load.begin"));
    qInfo() << "Loading QML windows";
    m_engine->load(QUrl(QStringLiteral("qrc:/qt/qml/VAXIL/gui/qml/OverlayWindow.qml")));
    m_engine->load(QUrl(QStringLiteral("qrc:/qt/qml/VAXIL/gui/qml/SettingsWindow.qml")));
    m_engine->load(QUrl(QStringLiteral("qrc:/qt/qml/VAXIL/gui/qml/SetupWizard.qml")));
    m_engine->load(QUrl(QStringLiteral("qrc:/qt/qml/VAXIL/gui/qml/ToolsHubWindow.qml")));
    m_engine->load(QUrl(QStringLiteral("qrc:/qt/qml/VAXIL/gui/qml/FullUiWindow.qml")));
    if (m_engine->rootObjects().isEmpty()) {
        CrashDiagnosticsService::instance().markStartupMilestone(StartupMilestones::startupOverlayFail(),
                                                                 QStringLiteral("No root objects created"),
                                                                 false);
        if (m_loggingService) {
            m_loggingService->errorFor(QStringLiteral("orb_render"),
                                       QStringLiteral("[%1] startup overlay load failed: no root objects")
                                           .arg(VaxilErrorCodes::forKey(VaxilErrorCodes::Key::StartupInitializationFailed)));
        }
        qCritical() << "QML load failed: no root objects were created";
        return false;
    }
    CrashDiagnosticsService::instance().markStartupMilestone(StartupMilestones::startupOverlayOk(),
                                                             QStringLiteral("QML windows loaded"),
                                                             true);
    m_loggingService->breadcrumb(QStringLiteral("startup"),
                                 StartupMilestones::startupOverlayOk(),
                                 QStringLiteral("qml.load.ok"));

    const auto registerWindowDiagnostics = [this](QQuickWindow *window, const QString &windowName) {
        if (window == nullptr || m_loggingService == nullptr) {
            return;
        }

        window->setObjectName(windowName);
        m_loggingService->infoFor(
            QStringLiteral("orb_render"),
            QStringLiteral("[orb] window_registered name=\"%1\" visible=%2")
                .arg(windowName)
                .arg(window->isVisible() ? QStringLiteral("true") : QStringLiteral("false")));

        connect(window, &QQuickWindow::sceneGraphInitialized, this, [this, window, windowName]() {
            if (m_loggingService == nullptr || window == nullptr) {
                return;
            }

            m_loggingService->infoFor(
                QStringLiteral("orb_render"),
                QStringLiteral("[orb] scenegraph_initialized window=\"%1\" graphics_api=\"%2\"")
                    .arg(windowName, graphicsApiName(window->rendererInterface()->graphicsApi())));
        });

        connect(window,
                &QQuickWindow::sceneGraphError,
                this,
                [this, windowName](QQuickWindow::SceneGraphError error, const QString &message) {
                    if (m_loggingService == nullptr) {
                        return;
                    }

                    m_loggingService->errorFor(
                        QStringLiteral("orb_render"),
                        QStringLiteral("[orb] scenegraph_error window=\"%1\" code=\"%2\" message=\"%3\"")
                            .arg(windowName, sceneGraphErrorName(error), message.trimmed()));
                });
    };

    const auto installMinimizeGuard = [this](QQuickWindow *window) {
        if (window == nullptr) {
            return;
        }
        window->installEventFilter(this);
    };

    if (auto *window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first())) {
        m_mainWindow = window;
        registerWindowDiagnostics(window, QStringLiteral("overlay_window"));
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
            registerWindowDiagnostics(m_settingsWindow, QStringLiteral("settings_window"));
            installMinimizeGuard(m_settingsWindow);
            if (!appIcon.isNull()) {
                m_settingsWindow->setIcon(appIcon);
            }
            m_settingsWindow->hide();
        }
    }
    if (m_engine->rootObjects().size() > 2) {
        m_setupWindow = qobject_cast<QQuickWindow *>(m_engine->rootObjects().at(2));
        if (m_setupWindow) {
            registerWindowDiagnostics(m_setupWindow, QStringLiteral("setup_window"));
            installMinimizeGuard(m_setupWindow);
            if (!appIcon.isNull()) {
                m_setupWindow->setIcon(appIcon);
            }
            m_setupWindow->hide();
        }
    }
    if (m_engine->rootObjects().size() > 3) {
        m_toolsWindow = qobject_cast<QQuickWindow *>(m_engine->rootObjects().at(3));
        if (m_toolsWindow) {
            registerWindowDiagnostics(m_toolsWindow, QStringLiteral("tools_window"));
            installMinimizeGuard(m_toolsWindow);
            if (!appIcon.isNull()) {
                m_toolsWindow->setIcon(appIcon);
            }
            m_toolsWindow->hide();
        }
    }
    if (m_engine->rootObjects().size() > 4) {
        m_fullUiWindow = qobject_cast<QQuickWindow *>(m_engine->rootObjects().at(4));
        if (m_fullUiWindow) {
            registerWindowDiagnostics(m_fullUiWindow, QStringLiteral("full_ui_window"));
            installMinimizeGuard(m_fullUiWindow);
            if (!appIcon.isNull()) {
                m_fullUiWindow->setIcon(appIcon);
            }
            m_fullUiWindow->hide();
        }
    }
    if (m_setupWindow) {
        connect(m_setupWindow, &QWindow::visibleChanged, this, [this]() {
            m_overlayController->setSetupVisible(m_setupWindow && m_setupWindow->isVisible());
            if (!m_fullUiWindow || !m_settings) {
                return;
            }
            if (m_setupWindow && m_setupWindow->isVisible()) {
                m_fullUiWindow->hide();
                return;
            }
            const QString mode = m_settings->uiMode().trimmed().toLower();
            if (mode == QStringLiteral("full") && !m_fullUiWindow->isVisible()) {
                m_fullUiWindow->show();
                m_fullUiWindow->raise();
                m_fullUiWindow->requestActivate();
            }
        });
    }

    const auto normalizeUiMode = [](const QString &mode) {
        const QString normalized = mode.trimmed().toLower();
        if (normalized == QStringLiteral("overlay")) {
            return QStringLiteral("overlay");
        }
        return QStringLiteral("full");
    };

    const auto applyUiMode = [this, normalizeUiMode]() {
        if (!m_settings) {
            return;
        }
        const QString mode = normalizeUiMode(m_settings->uiMode());
        if (!m_lastUiMode.isEmpty() && m_lastUiMode == mode) {
            return;
        }
        m_lastUiMode = mode;
        if (mode == QStringLiteral("overlay")) {
            m_overlayController->setOverlayEnabled(true);
            if (m_fullUiWindow) {
                m_fullUiWindow->hide();
            }
            return;
        }

        m_overlayController->setOverlayEnabled(false);
        m_overlayController->hideOverlay();
        if (m_fullUiWindow) {
            if (m_setupWindow && m_setupWindow->isVisible()) {
                m_fullUiWindow->hide();
                return;
            }
            if (!m_fullUiWindow->isVisible()) {
                m_fullUiWindow->show();
                m_fullUiWindow->raise();
                m_fullUiWindow->requestActivate();
            }
        }
    };

    const auto setUiModeAndApply = [this, applyUiMode](const QString &mode) {
        if (!m_settings) {
            return;
        }
        m_settings->setUiMode(mode);
        m_settings->save();
        applyUiMode();
    };

    const auto toggleActiveUi = [this, normalizeUiMode]() {
        if (!m_settings) {
            return;
        }
        const QString mode = normalizeUiMode(m_settings->uiMode());
        if (mode == QStringLiteral("overlay")) {
            m_overlayController->toggleOverlay();
            return;
        }

        if (!m_fullUiWindow) {
            m_overlayController->toggleOverlay();
            return;
        }

        if (m_fullUiWindow->isVisible()) {
            m_fullUiWindow->hide();
        } else {
            m_fullUiWindow->show();
            m_fullUiWindow->raise();
            m_fullUiWindow->requestActivate();
        }
    };

    connect(m_settings.get(), &AppSettings::settingsChanged, this, applyUiMode);

#ifdef Q_OS_WIN
    m_hotkeyFilter = std::make_unique<NativeHotkeyFilter>([toggleActiveUi]() {
        toggleActiveUi();
    });
    QCoreApplication::instance()->installNativeEventFilter(m_hotkeyFilter.get());
#endif
    auto *trayMenu = new QMenu();
    const auto showTrayNotification = [this](const QString &title,
                                             const QString &message,
                                             QSystemTrayIcon::MessageIcon icon,
                                             int timeoutMs,
                                             const QString &priority) {
        if (m_trayIcon) {
            m_trayIcon->showMessage(title, message, icon, timeoutMs);
        }
        if (m_desktopPerceptionMonitor) {
            m_desktopPerceptionMonitor->recordNotification(title, message, priority);
        }
    };
    trayMenu->addAction(QStringLiteral("Toggle UI"), [toggleActiveUi]() {
        toggleActiveUi();
    });
    trayMenu->addAction(QStringLiteral("Switch to Overlay UI"), [setUiModeAndApply]() {
        setUiModeAndApply(QStringLiteral("overlay"));
    });
    trayMenu->addAction(QStringLiteral("Switch to Full UI"), [setUiModeAndApply]() {
        setUiModeAndApply(QStringLiteral("full"));
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
    connect(m_trayIcon.get(), &QSystemTrayIcon::activated, this, [this, toggleActiveUi](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            if (m_setupWindow && m_setupWindow->isVisible()) {
                m_setupWindow->raise();
                m_setupWindow->requestActivate();
                return;
            }
            toggleActiveUi();
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
    const auto updateStartupPresentation = [this, startupAnnouncementSent, lastStartupIssue, lastBlockedIssueNotified, lastBlockedNotificationAtMs, platformCapabilities, showTrayNotification]() {
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
                showTrayNotification(
                    QStringLiteral("Vaxil"),
                    readyMessage,
                    QSystemTrayIcon::Information,
                    5000,
                    QStringLiteral("low"));
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
                showTrayNotification(
                    QStringLiteral("Vaxil"),
                    issue.isEmpty() ? QStringLiteral("Startup is blocked. Open setup to fix the missing components.") : issue,
                    QSystemTrayIcon::Warning,
                    7000,
                    QStringLiteral("high"));
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
    applyUiMode();
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
    connect(m_backendFacade.get(), &BackendFacade::settingsWindowRequested, this, [this]() {
        if (m_settingsWindow) {
            m_settingsWindow->show();
            m_settingsWindow->raise();
            m_settingsWindow->requestActivate();
        }
    });
    connect(m_backendFacade.get(), &BackendFacade::setupWindowRequested, this, [this]() {
        if (m_setupWindow) {
            m_setupWindow->show();
            m_setupWindow->raise();
            m_setupWindow->requestActivate();
        }
    });
    if (m_desktopPerceptionMonitor) {
        m_desktopPerceptionMonitor->start();
    }
    return true;
}
