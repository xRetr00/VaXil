#include <QtTest>

#include "cognition/DesktopContextSelectionBuilder.h"

class DesktopContextSelectionBuilderTests : public QObject
{
    Q_OBJECT

private slots:
    void includesFreshEditorContextForWorkQueries();
    void includesWorkModeAndDocumentMetadata();
    void skipsNoisyClipboardContext();
    void allowsExplicitWorkReferentDespiteNoisyClipboardContext();
    void skipsIrrelevantGeneralChat();
    void skipsStaleTopicForUnrelatedCorrection();
    void includesExplicitCurrentPageReference();
    void skipsPrivateModeContext();
};

void DesktopContextSelectionBuilderTests::includesFreshEditorContextForWorkQueries()
{
    QVariantMap context;
    context.insert(QStringLiteral("taskId"), QStringLiteral("editor_document"));
    context.insert(QStringLiteral("topic"), QStringLiteral("plan md"));
    context.insert(QStringLiteral("appId"), QStringLiteral("Code.exe"));
    context.insert(QStringLiteral("threadId"), QStringLiteral("editor_document::plan-md"));
    context.insert(QStringLiteral("documentContext"), QStringLiteral("PLAN.md"));
    context.insert(QStringLiteral("languageHint"), QStringLiteral("markdown"));
    context.insert(QStringLiteral("metadataClass"), QStringLiteral("editor_document"));

    const QString selectionInput = DesktopContextSelectionBuilder::buildSelectionInput(
        QStringLiteral("summarize this"),
        IntentType::GENERAL_CHAT,
        QStringLiteral("Editor document: PLAN.md"),
        context,
        1000,
        1500,
        false);

    QVERIFY(selectionInput.contains(QStringLiteral("Current desktop context:")));
    QVERIFY(selectionInput.contains(QStringLiteral("PLAN.md")));
    QVERIFY(selectionInput.contains(QStringLiteral("task=editor_document")));
}

void DesktopContextSelectionBuilderTests::includesWorkModeAndDocumentMetadata()
{
    QVariantMap context;
    context.insert(QStringLiteral("taskId"), QStringLiteral("browser_tab"));
    context.insert(QStringLiteral("topic"), QStringLiteral("qt_docs"));
    context.insert(QStringLiteral("appId"), QStringLiteral("edge"));
    context.insert(QStringLiteral("threadId"), QStringLiteral("browser_tab::qt-docs"));
    context.insert(QStringLiteral("documentContext"), QStringLiteral("Qt 6 Signals and Slots"));
    context.insert(QStringLiteral("siteContext"), QStringLiteral("doc.qt.io"));
    context.insert(QStringLiteral("metadataClass"), QStringLiteral("browser_document"));

    const QString selectionInput = DesktopContextSelectionBuilder::buildSelectionInput(
        QStringLiteral("explain this page"),
        IntentType::GENERAL_CHAT,
        QStringLiteral("Browser tab: Qt 6 Signals and Slots"),
        context,
        1000,
        1500,
        false);

    QVERIFY(selectionInput.contains(QStringLiteral("mode=technical_research")));
    QVERIFY(selectionInput.contains(QStringLiteral("document=Qt 6 Signals and Slots")));
    QVERIFY(selectionInput.contains(QStringLiteral("site=doc.qt.io")));
    QVERIFY(selectionInput.contains(QStringLiteral("class=browser_document")));
}

void DesktopContextSelectionBuilderTests::skipsNoisyClipboardContext()
{
    QVariantMap context;
    context.insert(QStringLiteral("taskId"), QStringLiteral("clipboard"));
    context.insert(QStringLiteral("clipboardPreview"), QStringLiteral("non_text:image/png"));

    const QString selectionInput = DesktopContextSelectionBuilder::buildSelectionInput(
        QStringLiteral("what did I copy?"),
        IntentType::GENERAL_CHAT,
        QStringLiteral("Clipboard changed"),
        context,
        1000,
        1500,
        false);

    QCOMPARE(selectionInput, QStringLiteral("what did I copy?"));
}

void DesktopContextSelectionBuilderTests::allowsExplicitWorkReferentDespiteNoisyClipboardContext()
{
    QVariantMap context;
    context.insert(QStringLiteral("taskId"), QStringLiteral("clipboard"));
    context.insert(QStringLiteral("clipboardPreview"), QStringLiteral("non_text:image/png"));
    context.insert(QStringLiteral("topic"), QStringLiteral("current editor task"));

    const QString selectionInput = DesktopContextSelectionBuilder::buildSelectionInput(
        QStringLiteral("what am I doing right now"),
        IntentType::GENERAL_CHAT,
        QStringLiteral("Editor document: PLAN.md"),
        context,
        1000,
        1500,
        false);

    QVERIFY(selectionInput.contains(QStringLiteral("Current desktop context:")));
}

void DesktopContextSelectionBuilderTests::skipsIrrelevantGeneralChat()
{
    QVariantMap context;
    context.insert(QStringLiteral("taskId"), QStringLiteral("editor_document"));

    const QString selectionInput = DesktopContextSelectionBuilder::buildSelectionInput(
        QStringLiteral("tell me a joke"),
        IntentType::GENERAL_CHAT,
        QStringLiteral("Editor document: PLAN.md"),
        context,
        1000,
        1500,
        false);

    QCOMPARE(selectionInput, QStringLiteral("tell me a joke"));
}

void DesktopContextSelectionBuilderTests::skipsStaleTopicForUnrelatedCorrection()
{
    QVariantMap context;
    context.insert(QStringLiteral("taskId"), QStringLiteral("browser_tab"));
    context.insert(QStringLiteral("topic"), QStringLiteral("machine learning youtube"));
    context.insert(QStringLiteral("documentContext"), QStringLiteral("machine learning - YouTube"));

    const QString selectionInput = DesktopContextSelectionBuilder::buildSelectionInput(
        QStringLiteral("No, the latest model released by OpenAI"),
        IntentType::GENERAL_CHAT,
        QStringLiteral("Browser tab: machine learning - YouTube"),
        context,
        1000,
        1500,
        false);

    QCOMPARE(selectionInput, QStringLiteral("No, the latest model released by OpenAI"));
    QVERIFY(DesktopContextSelectionBuilder::contextRelevanceScore(
                QStringLiteral("No, the latest model released by OpenAI"),
                IntentType::GENERAL_CHAT,
                context) < DesktopContextSelectionBuilder::minimumInjectionScore());
}

void DesktopContextSelectionBuilderTests::includesExplicitCurrentPageReference()
{
    QVariantMap context;
    context.insert(QStringLiteral("taskId"), QStringLiteral("browser_tab"));
    context.insert(QStringLiteral("topic"), QStringLiteral("machine learning youtube"));
    context.insert(QStringLiteral("documentContext"), QStringLiteral("machine learning - YouTube"));

    const QString selectionInput = DesktopContextSelectionBuilder::buildSelectionInput(
        QStringLiteral("summarize this page"),
        IntentType::GENERAL_CHAT,
        QStringLiteral("Browser tab: machine learning - YouTube"),
        context,
        1000,
        1500,
        false);

    QVERIFY(selectionInput.contains(QStringLiteral("Current desktop context:")));
    QCOMPARE(DesktopContextSelectionBuilder::contextInjectionReason(
                 QStringLiteral("summarize this page"),
                 IntentType::GENERAL_CHAT,
                 context),
             QStringLiteral("context.explicit_referent"));
}

void DesktopContextSelectionBuilderTests::skipsPrivateModeContext()
{
    QVariantMap context;
    context.insert(QStringLiteral("taskId"), QStringLiteral("browser_tab"));

    const QString selectionInput = DesktopContextSelectionBuilder::buildSelectionInput(
        QStringLiteral("summarize this"),
        IntentType::GENERAL_CHAT,
        QStringLiteral("Browser tab: release notes"),
        context,
        1000,
        1500,
        true);

    QCOMPARE(selectionInput, QStringLiteral("summarize this"));
}

QTEST_APPLESS_MAIN(DesktopContextSelectionBuilderTests)
#include "DesktopContextSelectionBuilderTests.moc"
