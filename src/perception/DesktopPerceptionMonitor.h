#pragma once

#include <QObject>
#include <QVariantMap>

#include "cognition/CooldownEngine.h"
#include "companion/contracts/CompanionContextSnapshot.h"
#include "companion/contracts/CooldownState.h"

class AppSettings;
class LoggingService;
class QClipboard;
class QTimer;

class DesktopPerceptionMonitor : public QObject
{
    Q_OBJECT

public:
    DesktopPerceptionMonitor(AppSettings *settings, LoggingService *loggingService, QObject *parent = nullptr);

    void start();
    void recordNotification(const QString &title,
                            const QString &message,
                            const QString &priority,
                            const QString &source = QStringLiteral("tray"));

signals:
    void desktopContextUpdated(const QString &summary, const QVariantMap &context);

private:
    struct ActiveWindowSnapshot
    {
        QString appId;
        QString windowTitle;
        QVariantMap metadata;

        [[nodiscard]] QString fingerprint() const
        {
            return appId
                + QStringLiteral("::") + windowTitle
                + QStringLiteral("::") + metadata.value(QStringLiteral("documentContext")).toString()
                + QStringLiteral("::") + metadata.value(QStringLiteral("siteContext")).toString()
                + QStringLiteral("::") + metadata.value(QStringLiteral("workspaceContext")).toString();
        }
    };

    void pollActiveWindow();
    void handleClipboardChanged();
    void recordPerception(const QString &reasonCode,
                          const QString &priority,
                          double confidence,
                          double novelty,
                          const QVariantMap &payload,
                          const CompanionContextSnapshot &context) const;
    void evaluateCooldown(const QString &reasonCode,
                          const QString &priority,
                          double confidence,
                          double novelty,
                          const CompanionContextSnapshot &context);
    [[nodiscard]] bool shouldIgnoreClipboardPreview(const QString &preview) const;
    [[nodiscard]] ActiveWindowSnapshot currentActiveWindow() const;
    [[nodiscard]] ActiveWindowSnapshot privateModeActiveWindow(const QString &appId) const;
    [[nodiscard]] QString clipboardPreview() const;
    [[nodiscard]] QVariantMap basePayload(const QVariantMap &payload) const;
    [[nodiscard]] FocusModeState currentFocusMode() const;

    AppSettings *m_settings = nullptr;
    LoggingService *m_loggingService = nullptr;
    QClipboard *m_clipboard = nullptr;
    QTimer *m_windowPollTimer = nullptr;
    mutable CooldownEngine m_cooldownEngine;
    mutable CooldownState m_cooldownState;
    QString m_sessionId;
    QString m_lastWindowFingerprint;
    QString m_lastClipboardFingerprint;
};
