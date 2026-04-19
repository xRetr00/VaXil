#include "perception/DesktopPerceptionMonitor.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QMimeData>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>

#include "companion/contracts/BehaviorTraceEvent.h"
#include "companion/contracts/CompanionContextSnapshot.h"
#include "companion/contracts/FocusModeState.h"
#include "logging/LoggingService.h"
#include "perception/DesktopContextThreadBuilder.h"
#include "perception/FocusModeExpiryRuntime.h"
#include "perception/DesktopSourceContextAdapter.h"
#include "perception/WindowsUiAutomationProbe.h"
#include "settings/AppSettings.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QString currentTraceId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}

DesktopPerceptionMonitor::DesktopPerceptionMonitor(AppSettings *settings,
                                                   LoggingService *loggingService,
                                                   QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_loggingService(loggingService)
    , m_clipboard(QApplication::clipboard())
    , m_windowPollTimer(new QTimer(this))
    , m_sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_windowPollTimer->setInterval(1500);
    connect(m_windowPollTimer, &QTimer::timeout, this, &DesktopPerceptionMonitor::pollActiveWindow);

    if (m_clipboard != nullptr) {
        connect(m_clipboard, &QClipboard::dataChanged, this, &DesktopPerceptionMonitor::handleClipboardChanged);
    }
}

void DesktopPerceptionMonitor::start()
{
    reconcileTimedFocusModeExpiry(QDateTime::currentMSecsSinceEpoch());
    m_windowPollTimer->start();
    pollActiveWindow();
}

void DesktopPerceptionMonitor::recordNotification(const QString &title,
                                                  const QString &message,
                                                  const QString &priority,
                                                  const QString &source)
{
    reconcileTimedFocusModeExpiry(QDateTime::currentMSecsSinceEpoch());
    if (m_loggingService == nullptr) {
        return;
    }

    const DesktopContextFilterDecision filterDecision = DesktopContextFilter::evaluate({
        .sourceKind = QStringLiteral("notification"),
        .appId = source.trimmed().isEmpty() ? QStringLiteral("tray") : source.trimmed(),
        .notificationTitle = title,
        .notificationMessage = message
    });
    if (!filterDecision.accepted) {
        recordFilteredContext(filterDecision.reasonCode, {
            {QStringLiteral("source"), source.trimmed().isEmpty() ? QStringLiteral("tray") : source.trimmed()},
            {QStringLiteral("title"), title.simplified()},
            {QStringLiteral("message"), message.simplified()},
            {QStringLiteral("priority"), priority.trimmed().toLower()}
        });
        return;
    }

    const CompanionContextSnapshot context = DesktopContextThreadBuilder::fromNotification(title, message, priority);
    QVariantMap payload = context.toVariantMap();
    payload.insert(QStringLiteral("source"), source.trimmed().isEmpty() ? QStringLiteral("tray") : source.trimmed());
    recordPerception(QStringLiteral("perception.notification"), priority, context.confidence, 0.82, payload, context);
    evaluateCooldown(QStringLiteral("perception.notification"), priority, context.confidence, 0.82, context);
    const QString summary = DesktopContextThreadBuilder::describeContext(context);
    const QVariantMap contextMap = context.toVariantMap();
    rememberAcceptedExternalContext(summary, contextMap);
    emit desktopContextUpdated(summary, contextMap);
}

void DesktopPerceptionMonitor::pollActiveWindow()
{
    reconcileTimedFocusModeExpiry(QDateTime::currentMSecsSinceEpoch());
    const ActiveWindowSnapshot snapshot = currentActiveWindow();
    if (snapshot.appId.isEmpty() || snapshot.windowTitle.isEmpty()) {
        return;
    }

    const QString fingerprint = snapshot.fingerprint();
    if (fingerprint == m_lastWindowFingerprint) {
        return;
    }
    m_lastWindowFingerprint = fingerprint;

    const DesktopContextFilterDecision filterDecision = DesktopContextFilter::evaluate({
        .sourceKind = QStringLiteral("active_window"),
        .appId = snapshot.appId,
        .windowTitle = snapshot.windowTitle,
        .metadata = snapshot.metadata
    });
    if (!filterDecision.accepted) {
        recordFilteredContext(filterDecision.reasonCode, {
            {QStringLiteral("appId"), snapshot.appId},
            {QStringLiteral("windowTitle"), snapshot.windowTitle},
            {QStringLiteral("windowMetadata"), snapshot.metadata},
            {QStringLiteral("previousExternalContextAgeMs"),
             m_lastAcceptedExternalContextAtMs > 0
                 ? QDateTime::currentMSecsSinceEpoch() - m_lastAcceptedExternalContextAtMs
                 : -1}
        });
        return;
    }

    const CompanionContextSnapshot context = DesktopContextThreadBuilder::fromActiveWindow(
        snapshot.appId,
        snapshot.windowTitle,
        snapshot.metadata);
    QVariantMap payload = context.toVariantMap();
    payload.insert(QStringLiteral("appId"), snapshot.appId);
    payload.insert(QStringLiteral("windowTitle"), snapshot.windowTitle);
    payload.insert(QStringLiteral("windowMetadata"), snapshot.metadata);
    const double novelty = context.threadId.value == m_cooldownState.threadId ? 0.38 : 0.92;
    recordPerception(QStringLiteral("perception.active_window.changed"), QStringLiteral("low"), context.confidence, novelty, payload, context);
    evaluateCooldown(QStringLiteral("perception.active_window.changed"), QStringLiteral("low"), context.confidence, novelty, context);
    const QString summary = DesktopContextThreadBuilder::describeContext(context);
    const QVariantMap contextMap = context.toVariantMap();
    rememberAcceptedExternalContext(summary, contextMap);
    emit desktopContextUpdated(summary, contextMap);
}

void DesktopPerceptionMonitor::handleClipboardChanged()
{
    reconcileTimedFocusModeExpiry(QDateTime::currentMSecsSinceEpoch());
    if (m_loggingService == nullptr || m_clipboard == nullptr) {
        return;
    }

    const QString preview = clipboardPreview();
    if (shouldIgnoreClipboardPreview(preview)) {
        return;
    }

    if (preview == m_lastClipboardFingerprint) {
        return;
    }
    m_lastClipboardFingerprint = preview;

    const ActiveWindowSnapshot activeWindow = currentActiveWindow();
    const CompanionContextSnapshot context = DesktopContextThreadBuilder::fromClipboard(
        activeWindow.appId,
        activeWindow.windowTitle,
        preview);
    QVariantMap payload = context.toVariantMap();
    payload.insert(QStringLiteral("formats"), m_clipboard->mimeData()->formats());
    recordPerception(QStringLiteral("perception.clipboard.changed"), QStringLiteral("medium"), context.confidence, 0.76, payload, context);
    evaluateCooldown(QStringLiteral("perception.clipboard.changed"), QStringLiteral("medium"), context.confidence, 0.76, context);
    const QString summary = DesktopContextThreadBuilder::describeContext(context);
    const QVariantMap contextMap = context.toVariantMap();
    rememberAcceptedExternalContext(summary, contextMap);
    emit desktopContextUpdated(summary, contextMap);
}

bool DesktopPerceptionMonitor::shouldIgnoreClipboardPreview(const QString &preview) const
{
    if (preview.isEmpty()) {
        return true;
    }

    if (m_settings != nullptr && m_settings->privateModeEnabled()) {
        return true;
    }

    if (preview.startsWith(QStringLiteral("non_text:application/x-"), Qt::CaseInsensitive)
        || preview.startsWith(QStringLiteral("non_text:com.apple."), Qt::CaseInsensitive)) {
        return true;
    }

    if (preview.size() < 4 && !preview.contains(QRegularExpression(QStringLiteral("\\d")))) {
        return true;
    }

    return false;
}

void DesktopPerceptionMonitor::recordPerception(const QString &reasonCode,
                                                const QString &priority,
                                                double confidence,
                                                double novelty,
                                                const QVariantMap &payload,
                                                const CompanionContextSnapshot &context) const
{
    if (m_loggingService == nullptr) {
        return;
    }

    BehaviorTraceEvent event = BehaviorTraceEvent::create(
        QStringLiteral("perception"),
        QStringLiteral("observed"),
        reasonCode,
        basePayload(payload),
        QStringLiteral("system"));
    event.sessionId = m_sessionId;
    event.traceId = currentTraceId();
    event.threadId = context.threadId.value;
    event.capabilityId = QStringLiteral("desktop_perception");
    event.payload.insert(QStringLiteral("priority"), priority);
    event.payload.insert(QStringLiteral("confidence"), confidence);
    event.payload.insert(QStringLiteral("novelty"), novelty);
    const bool recordedEvent = m_loggingService->logBehaviorEvent(event);
    (void)recordedEvent;
}

void DesktopPerceptionMonitor::evaluateCooldown(const QString &reasonCode,
                                                const QString &priority,
                                                double confidence,
                                                double novelty,
                                                const CompanionContextSnapshot &context)
{
    if (m_loggingService == nullptr) {
        return;
    }

    const CooldownEngine::Input input{
        context,
        m_cooldownState,
        currentFocusMode(),
        priority,
        confidence,
        novelty,
        QDateTime::currentMSecsSinceEpoch()
    };
    const BehaviorDecision decision = m_cooldownEngine.evaluate(input);
    const QString previousThreadId = m_cooldownState.threadId;
    m_cooldownState = m_cooldownEngine.advanceState(input, decision);

    BehaviorTraceEvent event = BehaviorTraceEvent::create(
        QStringLiteral("cooldown"),
        QStringLiteral("evaluated"),
        decision.reasonCode,
        decision.toVariantMap(),
        QStringLiteral("system"));
    event.sessionId = m_sessionId;
    event.traceId = currentTraceId();
    event.threadId = context.threadId.value;
    event.capabilityId = QStringLiteral("cooldown_engine");
    event.payload.insert(QStringLiteral("triggerReason"), reasonCode);
    event.payload.insert(QStringLiteral("context"), context.toVariantMap());
    event.payload.insert(QStringLiteral("state"), m_cooldownState.toVariantMap());
    const bool recordedEvent = m_loggingService->logBehaviorEvent(event);
    (void)recordedEvent;

    if (previousThreadId != context.threadId.value) {
        BehaviorTraceEvent threadEvent = BehaviorTraceEvent::create(
            QStringLiteral("context_thread"),
            QStringLiteral("updated"),
            QStringLiteral("context_thread.changed"),
            context.toVariantMap(),
            QStringLiteral("system"));
        threadEvent.sessionId = m_sessionId;
        threadEvent.traceId = currentTraceId();
        threadEvent.threadId = context.threadId.value;
        threadEvent.capabilityId = QStringLiteral("desktop_perception");
        const bool recorded = m_loggingService->logBehaviorEvent(threadEvent);
        (void)recorded;
    }
}

void DesktopPerceptionMonitor::recordFilteredContext(const QString &reasonCode,
                                                     const QVariantMap &payload) const
{
    if (m_loggingService == nullptr) {
        return;
    }

    QVariantMap diagnosticPayload = basePayload(payload);
    diagnosticPayload.insert(QStringLiteral("diagnosticOnly"), true);
    diagnosticPayload.insert(QStringLiteral("previousExternalContextPreserved"),
                             m_lastAcceptedExternalContextAtMs > 0
                                 && (QDateTime::currentMSecsSinceEpoch() - m_lastAcceptedExternalContextAtMs) <= 90000);
    if (!m_lastAcceptedExternalSummary.trimmed().isEmpty()) {
        diagnosticPayload.insert(QStringLiteral("previousExternalSummary"), m_lastAcceptedExternalSummary);
    }

    BehaviorTraceEvent event = BehaviorTraceEvent::create(
        QStringLiteral("perception"),
        QStringLiteral("filtered"),
        reasonCode,
        diagnosticPayload,
        QStringLiteral("system"));
    event.sessionId = m_sessionId;
    event.traceId = currentTraceId();
    event.capabilityId = QStringLiteral("desktop_context_filter");
    const bool recordedEvent = m_loggingService->logBehaviorEvent(event);
    (void)recordedEvent;
}

void DesktopPerceptionMonitor::rememberAcceptedExternalContext(const QString &summary, const QVariantMap &context)
{
    m_lastAcceptedExternalSummary = summary;
    m_lastAcceptedExternalContext = context;
    m_lastAcceptedExternalContextAtMs = QDateTime::currentMSecsSinceEpoch();
}

void DesktopPerceptionMonitor::reconcileTimedFocusModeExpiry(qint64 nowMs)
{
    const bool didExpire = FocusModeExpiryRuntime::reconcile(
        m_settings,
        m_loggingService,
        nowMs,
        QStringLiteral("perception_monitor"));
    (void)didExpire;
}

DesktopPerceptionMonitor::ActiveWindowSnapshot DesktopPerceptionMonitor::currentActiveWindow() const
{
    ActiveWindowSnapshot snapshot;
#ifdef Q_OS_WIN
    HWND windowHandle = GetForegroundWindow();
    if (windowHandle == nullptr) {
        return snapshot;
    }

    wchar_t titleBuffer[512];
    const int titleLength = GetWindowTextW(windowHandle, titleBuffer, static_cast<int>(std::size(titleBuffer)));
    if (titleLength <= 0) {
        return snapshot;
    }
    snapshot.windowTitle = QString::fromWCharArray(titleBuffer, titleLength).simplified();

    DWORD processId = 0;
    GetWindowThreadProcessId(windowHandle, &processId);
    if (processId == 0) {
        return snapshot;
    }

    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (processHandle == nullptr) {
        return snapshot;
    }

    wchar_t pathBuffer[MAX_PATH];
    DWORD pathLength = MAX_PATH;
    if (QueryFullProcessImageNameW(processHandle, 0, pathBuffer, &pathLength) != 0) {
        snapshot.appId = QString::fromWCharArray(pathBuffer, static_cast<int>(pathLength));
    }
    CloseHandle(processHandle);

    if (m_settings != nullptr && m_settings->privateModeEnabled()) {
        return privateModeActiveWindow(snapshot.appId);
    }

    snapshot.metadata = WindowsUiAutomationProbe::probeWindowMetadata(
        reinterpret_cast<quintptr>(windowHandle),
        snapshot.appId);
    snapshot.metadata = DesktopSourceContextAdapter::augmentMetadata(
        snapshot.appId,
        snapshot.windowTitle,
        snapshot.metadata);
#endif
    return snapshot;
}

DesktopPerceptionMonitor::ActiveWindowSnapshot DesktopPerceptionMonitor::privateModeActiveWindow(const QString &appId) const
{
    ActiveWindowSnapshot snapshot;
    snapshot.appId = appId;
    snapshot.windowTitle = QStringLiteral("private_mode_redacted");
    snapshot.metadata = {
        {QStringLiteral("documentContext"), QStringLiteral("private_mode_redacted")},
        {QStringLiteral("metadataRedacted"), true},
        {QStringLiteral("redactionReason"), QStringLiteral("private_mode")},
        {QStringLiteral("metadataQuality"), QStringLiteral("redacted")},
        {QStringLiteral("metadataClass"), QStringLiteral("private_app_only")},
        {QStringLiteral("metadataSource"), QStringLiteral("private_mode")}
    };
    return snapshot;
}

QString DesktopPerceptionMonitor::clipboardPreview() const
{
    if (m_clipboard == nullptr || m_clipboard->mimeData() == nullptr) {
        return {};
    }

    if (m_settings != nullptr && m_settings->privateModeEnabled()) {
        return QStringLiteral("private_mode_redacted");
    }

    const QString text = m_clipboard->text(QClipboard::Clipboard).simplified();
    if (!text.isEmpty()) {
        return text.left(160);
    }

    const QStringList formats = m_clipboard->mimeData()->formats();
    if (formats.isEmpty()) {
        return {};
    }
    return QStringLiteral("non_text:%1").arg(formats.first());
}

QVariantMap DesktopPerceptionMonitor::basePayload(const QVariantMap &payload) const
{
    QVariantMap merged = payload;
    merged.insert(QStringLiteral("focusModeEnabled"), m_settings != nullptr && m_settings->focusModeEnabled());
    merged.insert(QStringLiteral("privateModeEnabled"), m_settings != nullptr && m_settings->privateModeEnabled());
    return merged;
}

FocusModeState DesktopPerceptionMonitor::currentFocusMode() const
{
    FocusModeState state;
    if (m_settings == nullptr) {
        return state;
    }

    state.enabled = m_settings->focusModeEnabled();
    state.allowCriticalAlerts = m_settings->focusModeAllowCriticalAlerts();
    state.durationMinutes = m_settings->focusModeDurationMinutes();
    state.untilEpochMs = m_settings->focusModeUntilEpochMs();
    state.source = QStringLiteral("settings");
    return state;
}
