#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/CurrentContextReferentResolver.h"

class CurrentContextReferentResolverTests : public QObject
{
    Q_OBJECT

private slots:
    void resolvesCurrentEditorFileToFileRead();
    void asksWhenCurrentEditorFileIsAmbiguous();
    void resolvesCurrentPageWithUrlToBrowserFetch();
    void usesSearchFallbackWhenPageUrlUnavailable();
};

void CurrentContextReferentResolverTests::resolvesCurrentEditorFileToFileRead()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("README.md")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("# Vaxil\n");
    file.close();

    const CurrentContextResolution result = CurrentContextReferentResolver::resolve({
        .userInput = QStringLiteral("read the current file"),
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("editor_document")},
            {QStringLiteral("documentContext"), QStringLiteral("README.md")}
        },
        .desktopContextAtMs = 1000,
        .nowMs = 1200,
        .workspaceRoot = dir.path()
    });

    QCOMPARE(result.kind, CurrentContextResolutionKind::Task);
    QCOMPARE(result.decision.intent, IntentType::READ_FILE);
    QCOMPARE(result.decision.tasks.first().type, QStringLiteral("file_read"));
    QCOMPARE(result.decision.tasks.first().args.value(QStringLiteral("path")).toString(),
             QDir::cleanPath(file.fileName()));
}

void CurrentContextReferentResolverTests::asksWhenCurrentEditorFileIsAmbiguous()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir(dir.path()).mkpath(QStringLiteral("docs"));
    QDir(dir.path()).mkpath(QStringLiteral("src"));
    QFile docs(dir.filePath(QStringLiteral("docs/README.md")));
    QVERIFY(docs.open(QIODevice::WriteOnly | QIODevice::Text));
    docs.write("docs\n");
    docs.close();
    QFile src(dir.filePath(QStringLiteral("src/README.md")));
    QVERIFY(src.open(QIODevice::WriteOnly | QIODevice::Text));
    src.write("src\n");
    src.close();

    const CurrentContextResolution result = CurrentContextReferentResolver::resolve({
        .userInput = QStringLiteral("what is in this file"),
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("editor_document")},
            {QStringLiteral("documentContext"), QStringLiteral("README.md")}
        },
        .desktopContextAtMs = 1000,
        .nowMs = 1200,
        .workspaceRoot = dir.path()
    });

    QCOMPARE(result.kind, CurrentContextResolutionKind::Clarify);
    QVERIFY(result.message.contains(QStringLiteral("multiple files")));
}

void CurrentContextReferentResolverTests::resolvesCurrentPageWithUrlToBrowserFetch()
{
    const CurrentContextResolution result = CurrentContextReferentResolver::resolve({
        .userInput = QStringLiteral("read the current page"),
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("browser_tab")},
            {QStringLiteral("url"), QStringLiteral("https://example.com/course")}
        },
        .desktopContextAtMs = 1000,
        .nowMs = 1200,
        .workspaceRoot = QDir::currentPath()
    });

    QCOMPARE(result.kind, CurrentContextResolutionKind::Task);
    QCOMPARE(result.decision.tasks.first().type, QStringLiteral("browser_fetch_text"));
    QCOMPARE(result.decision.tasks.first().args.value(QStringLiteral("url")).toString(),
             QStringLiteral("https://example.com/course"));
}

void CurrentContextReferentResolverTests::usesSearchFallbackWhenPageUrlUnavailable()
{
    const CurrentContextResolution result = CurrentContextReferentResolver::resolve({
        .userInput = QStringLiteral("best course in the result"),
        .desktopContext = {
            {QStringLiteral("taskId"), QStringLiteral("browser_tab")},
            {QStringLiteral("documentContext"), QStringLiteral("machine learning courses")},
            {QStringLiteral("siteContext"), QStringLiteral("youtube.com")}
        },
        .desktopContextAtMs = 1000,
        .nowMs = 1200,
        .workspaceRoot = QDir::currentPath()
    });

    QCOMPARE(result.kind, CurrentContextResolutionKind::Task);
    QCOMPARE(result.decision.tasks.first().type, QStringLiteral("web_search"));
    QCOMPARE(result.decision.tasks.first().args.value(QStringLiteral("evidenceNote")).toString(),
             QStringLiteral("not_direct_page_evidence"));
}

QTEST_APPLESS_MAIN(CurrentContextReferentResolverTests)
#include "CurrentContextReferentResolverTests.moc"
