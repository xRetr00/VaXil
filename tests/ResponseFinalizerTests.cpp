#include <QtTest>

#include "ai/SpokenReply.h"
#include "core/ResponseFinalizer.h"

class ResponseFinalizerTests : public QObject
{
    Q_OBJECT

private slots:
    void doesNotAppendInternalPlanningHint();
};

void ResponseFinalizerTests::doesNotAppendInternalPlanningHint()
{
    ResponseFinalizer finalizer(nullptr, nullptr, nullptr);

    SpokenReply reply;
    reply.displayText = QStringLiteral("I didn't catch that.");
    reply.shouldSpeak = false;

    ActionSession session;
    session.responseMode = ResponseMode::Recover;
    session.nextStepHint = QStringLiteral("Ground the answer in retrieved evidence before responding.");
    session.successSummary = QStringLiteral("Done.");
    session.failureSummary = QStringLiteral("I couldn't finish that.");

    QString responseText;
    QString status;
    const bool spoke = finalizer.finalizeResponse(
        QStringLiteral("test"),
        QStringLiteral("test_turn"),
        reply,
        session,
        &responseText,
        []() {},
        []() {},
        [](const QString &, const QString &, const QString &) {},
        QString(),
        [&status](const QString &newStatus) { status = newStatus; });

    QVERIFY(!spoke);
    QCOMPARE(responseText, QStringLiteral("I didn't catch that."));
    QCOMPARE(status, QStringLiteral("Recovery response"));
}

QTEST_APPLESS_MAIN(ResponseFinalizerTests)
#include "ResponseFinalizerTests.moc"
