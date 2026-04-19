#include <QtTest>

#include "perception/DesktopContextThreadBuilder.h"
#include "perception/DesktopContextFilter.h"

class DesktopContextThreadBuilderTests : public QObject
{
    Q_OBJECT

private slots:
    void buildsActiveWindowContext();
    void buildsBrowserWindowContext();
    void cleansBrowserShellSuffixFromPageContext();
    void prefersUiAutomationMetadataWhenAvailable();
    void preservesMetadataConfidenceAndRedactionMarkers();
    void marksPrivateModeContextAsRedacted();
    void buildsClipboardContext();
    void buildsNotificationContext();
    void filtersVaxilCommandDeckAsDiagnosticOnly();
    void filtersVaxilNotificationAsDiagnosticOnly();
    void doesNotFilterExternalVaxilDocumentTitle();
};

void DesktopContextThreadBuilderTests::buildsActiveWindowContext()
{
    const CompanionContextSnapshot snapshot = DesktopContextThreadBuilder::fromActiveWindow(
        QStringLiteral("C:/Program Files/Microsoft VS Code/Code.exe"),
        QStringLiteral("PLAN.md - Visual Studio Code"));

    QCOMPARE(snapshot.appId, QStringLiteral("vscode"));
    QCOMPARE(snapshot.taskId, QStringLiteral("editor_document"));
    QVERIFY(snapshot.threadId.value.startsWith(QStringLiteral("desktop::editor_document::vscode::")));
    QCOMPARE(snapshot.topic, QStringLiteral("plan_md"));
    QCOMPARE(snapshot.recentIntent, QStringLiteral("reference current file"));
    QCOMPARE(snapshot.metadata.value(QStringLiteral("languageHint")).toString(), QStringLiteral("markdown"));
    QVERIFY(DesktopContextThreadBuilder::describeContext(snapshot).contains(QStringLiteral("editor file")));
}

void DesktopContextThreadBuilderTests::buildsBrowserWindowContext()
{
    const CompanionContextSnapshot snapshot = DesktopContextThreadBuilder::fromActiveWindow(
        QStringLiteral("C:/Program Files/Google/Chrome/Application/chrome.exe"),
        QStringLiteral("ChatGPT | OpenAI - Google Chrome"));

    QCOMPARE(snapshot.appId, QStringLiteral("chrome"));
    QCOMPARE(snapshot.taskId, QStringLiteral("browser_tab"));
    QCOMPARE(snapshot.metadata.value(QStringLiteral("documentContext")).toString(), QStringLiteral("ChatGPT"));
    QCOMPARE(snapshot.metadata.value(QStringLiteral("siteContext")).toString(), QStringLiteral("OpenAI"));
    QCOMPARE(snapshot.metadata.value(QStringLiteral("metadataClass")).toString(), QStringLiteral("browser_document"));
    QCOMPARE(snapshot.metadata.value(QStringLiteral("documentKind")).toString(), QStringLiteral("browser_page"));
    QCOMPARE(snapshot.topic, QStringLiteral("chatgpt"));
    QVERIFY(DesktopContextThreadBuilder::describeContext(snapshot).contains(QStringLiteral("on OpenAI")));
}

void DesktopContextThreadBuilderTests::cleansBrowserShellSuffixFromPageContext()
{
    const CompanionContextSnapshot snapshot = DesktopContextThreadBuilder::fromActiveWindow(
        QStringLiteral("C:/Program Files/Microsoft/Edge/Application/msedge.exe"),
        QStringLiteral("Qt signals and slots (3) - Microsoft Edge"));

    QCOMPARE(snapshot.appId, QStringLiteral("edge"));
    QCOMPARE(snapshot.metadata.value(QStringLiteral("documentContext")).toString(),
             QStringLiteral("Qt signals and slots"));
    QVERIFY(snapshot.metadata.value(QStringLiteral("siteContext")).toString().isEmpty());
    QCOMPARE(snapshot.topic, QStringLiteral("qt_signals_and_slots"));
}

void DesktopContextThreadBuilderTests::prefersUiAutomationMetadataWhenAvailable()
{
    QVariantMap metadata;
    metadata.insert(QStringLiteral("documentContext"), QStringLiteral("AssistantController.cpp"));
    metadata.insert(QStringLiteral("workspaceContext"), QStringLiteral("Vaxil"));
    metadata.insert(QStringLiteral("automationSource"), QStringLiteral("uia"));

    const CompanionContextSnapshot snapshot = DesktopContextThreadBuilder::fromActiveWindow(
        QStringLiteral("C:/Program Files/Microsoft VS Code/Code.exe"),
        QStringLiteral("README.md - Visual Studio Code"),
        metadata);

    QCOMPARE(snapshot.metadata.value(QStringLiteral("documentContext")).toString(), QStringLiteral("AssistantController.cpp"));
    QCOMPARE(snapshot.metadata.value(QStringLiteral("workspaceContext")).toString(), QStringLiteral("Vaxil"));
    QCOMPARE(snapshot.metadata.value(QStringLiteral("metadataClass")).toString(), QStringLiteral("editor_document"));
    QCOMPARE(snapshot.topic, QStringLiteral("assistantcontroller_cpp"));
    QVERIFY(snapshot.confidence > 0.8);
}

void DesktopContextThreadBuilderTests::preservesMetadataConfidenceAndRedactionMarkers()
{
    QVariantMap metadata;
    metadata.insert(QStringLiteral("documentContext"), QStringLiteral("ChatGPT"));
    metadata.insert(QStringLiteral("metadataConfidence"), 0.67);
    metadata.insert(QStringLiteral("metadataQuality"), QStringLiteral("medium"));
    metadata.insert(QStringLiteral("metadataRedacted"), true);
    metadata.insert(QStringLiteral("redactionReason"), QStringLiteral("phrase_too_long"));

    const CompanionContextSnapshot snapshot = DesktopContextThreadBuilder::fromActiveWindow(
        QStringLiteral("C:/Program Files/Google/Chrome/Application/chrome.exe"),
        QStringLiteral("ChatGPT | OpenAI - Google Chrome"),
        metadata);

    QCOMPARE(snapshot.confidence, 0.67);
    QVERIFY(snapshot.metadata.value(QStringLiteral("metadataRedacted")).toBool());
    QCOMPARE(snapshot.metadata.value(QStringLiteral("metadataQuality")).toString(), QStringLiteral("medium"));
    QCOMPARE(snapshot.metadata.value(QStringLiteral("redactionReason")).toString(), QStringLiteral("phrase_too_long"));
}

void DesktopContextThreadBuilderTests::marksPrivateModeContextAsRedacted()
{
    QVariantMap metadata;
    metadata.insert(QStringLiteral("documentContext"), QStringLiteral("private_mode_redacted"));
    metadata.insert(QStringLiteral("metadataClass"), QStringLiteral("private_app_only"));
    metadata.insert(QStringLiteral("metadataRedacted"), true);
    metadata.insert(QStringLiteral("redactionReason"), QStringLiteral("private_mode"));

    const CompanionContextSnapshot snapshot = DesktopContextThreadBuilder::fromActiveWindow(
        QStringLiteral("C:/Program Files/Google/Chrome/Application/chrome.exe"),
        QStringLiteral("private_mode_redacted"),
        metadata);

    QCOMPARE(snapshot.taskId, QStringLiteral("browser_tab"));
    QCOMPARE(snapshot.topic, QStringLiteral("private_mode"));
    QVERIFY(snapshot.threadId.value.endsWith(QStringLiteral("::private_mode")));
    QCOMPARE(snapshot.metadata.value(QStringLiteral("redactionReason")).toString(), QStringLiteral("private_mode"));
}

void DesktopContextThreadBuilderTests::buildsClipboardContext()
{
    const CompanionContextSnapshot snapshot = DesktopContextThreadBuilder::fromClipboard(
        QStringLiteral("chrome.exe"),
        QStringLiteral("ChatGPT | OpenAI"),
        QStringLiteral("OpenAI docs and structured output examples"));

    QCOMPARE(snapshot.appId, QStringLiteral("chrome"));
    QCOMPARE(snapshot.taskId, QStringLiteral("clipboard"));
    QVERIFY(snapshot.threadId.value.startsWith(QStringLiteral("desktop::clipboard::chrome::")));
    QCOMPARE(snapshot.topic, QStringLiteral("openai_docs_and_structured_output_exampl"));
}

void DesktopContextThreadBuilderTests::buildsNotificationContext()
{
    const CompanionContextSnapshot snapshot = DesktopContextThreadBuilder::fromNotification(
        QStringLiteral("Startup blocked"),
        QStringLiteral("Whisper executable missing"),
        QStringLiteral("high"));

    QCOMPARE(snapshot.appId, QStringLiteral("vaxil"));
    QCOMPARE(snapshot.taskId, QStringLiteral("notification"));
    QCOMPARE(snapshot.topic, QStringLiteral("startup_blocked"));
    QVERIFY(snapshot.confidence > 0.8);
}

void DesktopContextThreadBuilderTests::filtersVaxilCommandDeckAsDiagnosticOnly()
{
    const DesktopContextFilterDecision decision = DesktopContextFilter::evaluate({
        .sourceKind = QStringLiteral("active_window"),
        .appId = QStringLiteral("D:/Vaxil/bin/vaxil.exe"),
        .windowTitle = QStringLiteral("Vaxil Command Deck"),
        .metadata = {{QStringLiteral("documentContext"), QStringLiteral("Vaxil Command Deck")}}
    });

    QVERIFY(!decision.accepted);
    QVERIFY(decision.diagnosticOnly);
    QCOMPARE(decision.reasonCode, QStringLiteral("desktop_context.filtered_self_window"));
}

void DesktopContextThreadBuilderTests::filtersVaxilNotificationAsDiagnosticOnly()
{
    const DesktopContextFilterDecision decision = DesktopContextFilter::evaluate({
        .sourceKind = QStringLiteral("notification"),
        .appId = QStringLiteral("vaxil"),
        .notificationTitle = QStringLiteral("Vaxil"),
        .notificationMessage = QStringLiteral("Task finished")
    });

    QVERIFY(!decision.accepted);
    QVERIFY(decision.diagnosticOnly);
    QCOMPARE(decision.reasonCode, QStringLiteral("desktop_context.filtered_self_notification"));
}

void DesktopContextThreadBuilderTests::doesNotFilterExternalVaxilDocumentTitle()
{
    const DesktopContextFilterDecision decision = DesktopContextFilter::evaluate({
        .sourceKind = QStringLiteral("active_window"),
        .appId = QStringLiteral("C:/Program Files/Microsoft VS Code/Code.exe"),
        .windowTitle = QStringLiteral("Vaxil behavior spec - Visual Studio Code"),
        .metadata = {{QStringLiteral("documentContext"), QStringLiteral("Vaxil behavior spec")}}
    });

    QVERIFY(decision.accepted);
    QVERIFY(!decision.diagnosticOnly);
}

QTEST_APPLESS_MAIN(DesktopContextThreadBuilderTests)

#include "DesktopContextThreadBuilderTests.moc"
