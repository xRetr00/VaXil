#include "app/JarvisApplication.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QCoreApplication>
#include <QMenu>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStyle>
#include <QSystemTrayIcon>
#include <functional>

#include "core/AssistantController.h"
#include "gui/BackendFacade.h"
#include "logging/LoggingService.h"
#include "overlay/OverlayController.h"
#include "settings/AppSettings.h"
#include "settings/IdentityProfileService.h"

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
    m_settings = std::make_unique<AppSettings>();
    m_settings->load();
    m_identityProfileService = std::make_unique<IdentityProfileService>();
    if (!m_identityProfileService->initialize()) {
        return false;
    }

    m_loggingService = std::make_unique<LoggingService>();
    if (!m_loggingService->initialize()) {
        return false;
    }

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
    m_engine = std::make_unique<QQmlApplicationEngine>();
    const QIcon appIcon(QStringLiteral(":/qt/qml/JARVIS/gui/assets/icon.ico"));
    m_trayIcon = std::make_unique<QSystemTrayIcon>(appIcon.isNull() ? qApp->style()->standardIcon(QStyle::SP_ComputerIcon) : appIcon, this);

    m_engine->rootContext()->setContextProperty(QStringLiteral("backend"), m_backendFacade.get());
    m_engine->load(QUrl(QStringLiteral("qrc:/qt/qml/JARVIS/gui/qml/Main.qml")));
    m_engine->load(QUrl(QStringLiteral("qrc:/qt/qml/JARVIS/gui/qml/SettingsWindow.qml")));
    m_engine->load(QUrl(QStringLiteral("qrc:/qt/qml/JARVIS/gui/qml/SetupWizard.qml")));
    if (m_engine->rootObjects().isEmpty()) {
        return false;
    }

    if (auto *window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first())) {
        m_mainWindow = window;
        m_overlayController->attachWindow(window);
        m_overlayController->setClickThrough(m_settings->clickThroughEnabled());
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

    connect(m_overlayController.get(), &OverlayController::visibilityChanged, this, [this](bool visible) {
        if (!visible) {
            m_assistantController->cancelActiveRequest();
        }
    });

    m_assistantController->initialize();
    if (!m_settings->initialSetupCompleted() && m_setupWindow) {
        m_setupWindow->show();
        m_setupWindow->raise();
        m_setupWindow->requestActivate();
    }

    connect(m_backendFacade.get(), &BackendFacade::initialSetupFinished, this, [this]() {
        if (m_setupWindow) {
            m_setupWindow->hide();
        }
        if (m_mainWindow) {
            m_mainWindow->show();
        }
    });
    return true;
}
