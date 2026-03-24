#include "overlay/OverlayController.h"

#include <algorithm>

#include <QCursor>
#include <QGuiApplication>
#include <QPropertyAnimation>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QDateTime>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

OverlayController::OverlayController(QObject *parent)
    : QObject(parent)
{
    m_monitorTimer = new QTimer(this);
    m_monitorTimer->setInterval(450);
    connect(m_monitorTimer, &QTimer::timeout, this, [this]() {
        reevaluateVisibility();
    });
    m_monitorTimer->start();
}

void OverlayController::attachWindow(QQuickWindow *window)
{
    m_window = window;
    if (!m_window) {
        return;
    }

    m_window->setOpacity(0.0);
    m_opacityAnimation = new QPropertyAnimation(m_window, "opacity", this);
    m_opacityAnimation->setDuration(240);
    m_opacityAnimation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(m_opacityAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (!m_window) {
            return;
        }

        if (!m_visible && m_window->opacity() <= 0.01) {
            m_window->hide();
        }
    });
    positionWindow();
}

bool OverlayController::isVisible() const
{
    return m_visible;
}

void OverlayController::showOverlay()
{
    m_manualRequested = true;
    m_ignoreUserUntilMs = nowMs() + 2500;
    reevaluateVisibility();
}

void OverlayController::hideOverlay()
{
    m_manualRequested = false;
    reevaluateVisibility();
}

void OverlayController::toggleOverlay()
{
    if (m_manualRequested) {
        hideOverlay();
    } else {
        showOverlay();
    }
}

void OverlayController::setClickThrough(bool enabled)
{
    if (!m_window) {
        return;
    }

#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (enabled) {
        exStyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
    } else {
        exStyle &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
#else
    Q_UNUSED(enabled)
#endif
}

void OverlayController::setAssistantState(const QString &stateName)
{
    const QString normalized = stateName.trimmed().toUpper();
    m_assistantSpeaking = normalized == QStringLiteral("SPEAKING");
    m_assistantBusy = normalized == QStringLiteral("LISTENING")
        || normalized == QStringLiteral("PROCESSING")
        || m_assistantSpeaking;

    if (m_assistantBusy) {
        m_ignoreUserUntilMs = (std::max)(m_ignoreUserUntilMs, nowMs() + 1400);
    }

    if (normalized == QStringLiteral("IDLE")) {
        m_assistantBusy = false;
        m_assistantSpeaking = false;
    }

    reevaluateVisibility();
}

void OverlayController::setSetupVisible(bool visible)
{
    m_setupVisible = visible;
    reevaluateVisibility();
}

void OverlayController::reevaluateVisibility()
{
    if (!m_window) {
        return;
    }

    const qint64 now = nowMs();
    const bool fullscreen = isFullscreenForeground();
    const bool userActive = isUserActive();
    const bool ignoreActivity = now < m_ignoreUserUntilMs;

    if (userActive && !ignoreActivity && !m_assistantSpeaking) {
        m_manualRequested = false;
    }

    const bool shouldShow = !m_setupVisible
        && !fullscreen
        && (m_assistantSpeaking || m_manualRequested || (m_assistantBusy && (!userActive || ignoreActivity)));

    animateToVisible(shouldShow);
}

void OverlayController::animateToVisible(bool visible)
{
    if (!m_window) {
        return;
    }

    if (m_visible == visible && ((!visible) || m_window->isVisible())) {
        return;
    }

    m_visible = visible;
    emit visibilityChanged(m_visible);

    if (!m_opacityAnimation) {
        if (visible) {
            positionWindow();
            m_window->show();
            m_window->raise();
            m_window->requestActivate();
        } else {
            m_window->hide();
        }
        return;
    }

    m_opacityAnimation->stop();
    if (visible) {
        positionWindow();
        m_window->show();
        m_window->raise();
        m_window->setOpacity((std::max)(0.0, m_window->opacity()));
        m_opacityAnimation->setStartValue(m_window->opacity());
        m_opacityAnimation->setEndValue(1.0);
        m_opacityAnimation->start();
    } else {
        m_opacityAnimation->setStartValue(m_window->opacity());
        m_opacityAnimation->setEndValue(0.0);
        m_opacityAnimation->start();
    }
}

void OverlayController::positionWindow() const
{
    if (!m_window) {
        return;
    }

    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const QRect area = screen->geometry();
    m_window->setGeometry(area);
}

bool OverlayController::isUserActive() const
{
#ifdef Q_OS_WIN
    LASTINPUTINFO inputInfo;
    inputInfo.cbSize = sizeof(LASTINPUTINFO);
    if (!GetLastInputInfo(&inputInfo)) {
        return false;
    }

    const DWORD elapsedMs = GetTickCount() - inputInfo.dwTime;
    return elapsedMs < 1200;
#else
    return false;
#endif
}

bool OverlayController::isFullscreenForeground() const
{
#ifdef Q_OS_WIN
    HWND foreground = GetForegroundWindow();
    if (!foreground || !IsWindowVisible(foreground)) {
        return false;
    }

    if (m_window && foreground == reinterpret_cast<HWND>(m_window->winId())) {
        return false;
    }

    RECT windowRect;
    if (!GetWindowRect(foreground, &windowRect)) {
        return false;
    }

    MONITORINFO monitorInfo;
    monitorInfo.cbSize = sizeof(MONITORINFO);
    const HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfo(monitor, &monitorInfo)) {
        return false;
    }

    const RECT monitorRect = monitorInfo.rcMonitor;
    const int tolerance = 2;
    return windowRect.left <= monitorRect.left + tolerance
        && windowRect.top <= monitorRect.top + tolerance
        && windowRect.right >= monitorRect.right - tolerance
        && windowRect.bottom >= monitorRect.bottom - tolerance;
#else
    return false;
#endif
}

qint64 OverlayController::nowMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}
