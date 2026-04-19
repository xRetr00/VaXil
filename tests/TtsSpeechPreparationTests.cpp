#include <QtTest>

#include "tts/SpeechPreparationPipeline.h"
#include "tts/SpeechPunctuationShaper.h"
#include "tts/SpeechTextNormalizer.h"
#include "tts/UtteranceDedupeGuard.h"

class TtsSpeechPreparationTests : public QObject
{
    Q_OBJECT

private slots:
    void dedupeSuppressesExactWithinWindow();
    void dedupeAllowsAfterWindow();
    void dedupeSuppressesTrivialPunctuationVariants();
    void dedupeAllowsMateriallyDifferentText();
    void dedupeAllowsSameTextForDifferentTargets();

    void technicalTokenReadmeIsSpokenNaturally();
    void technicalTokenAcronymIsNotExploded();
    void technicalTokenDllDoesNotCreateBrokenPunctuation();
    void technicalTokenCppIsStable();
    void technicalTokenExtensionsAreConsistent();

    void punctuationShaperHandlesQuotedSearchPhrase();
    void punctuationShaperDoesNotInjectFakeCommas();
    void punctuationShaperRemovesEmbeddedQuestionMarkLeak();
    void punctuationShaperKeepsSentenceBoundariesNatural();

    void pipelineIsDeterministic();
    void pipelinePreservesTechnicalFormattingAfterShaping();
};

void TtsSpeechPreparationTests::dedupeSuppressesExactWithinWindow()
{
    UtteranceDedupeGuard guard(7000);
    UtteranceIdentity identity{QStringLiteral("assistant_reply"), QStringLiteral("finalizer"), QStringLiteral("turn-1"), QStringLiteral("goal")};

    const auto first = guard.evaluate(QStringLiteral("Working through it now."), identity, 1000);
    const auto second = guard.evaluate(QStringLiteral("Working through it now."), identity, 2000);

    QVERIFY(first.admitted);
    QVERIFY(!second.admitted);
    QCOMPARE(second.reason, QStringLiteral("exact_duplicate_in_window"));
}

void TtsSpeechPreparationTests::dedupeAllowsAfterWindow()
{
    UtteranceDedupeGuard guard(5000);
    UtteranceIdentity identity{QStringLiteral("assistant_reply"), QStringLiteral("finalizer"), QStringLiteral("turn-1"), QStringLiteral("goal")};

    QVERIFY(guard.evaluate(QStringLiteral("Done."), identity, 1000).admitted);
    QVERIFY(guard.evaluate(QStringLiteral("Done."), identity, 7005).admitted);
}

void TtsSpeechPreparationTests::dedupeSuppressesTrivialPunctuationVariants()
{
    UtteranceDedupeGuard guard(7000);
    UtteranceIdentity identity{QStringLiteral("assistant_reply"), QStringLiteral("finalizer"), QStringLiteral("turn-1"), QStringLiteral("goal")};

    QVERIFY(guard.evaluate(QStringLiteral("I opened README.md for you."), identity, 1000).admitted);
    const auto second = guard.evaluate(QStringLiteral("I opened README.md for you"), identity, 1300);
    QVERIFY(!second.admitted);
    QCOMPARE(second.reason, QStringLiteral("near_duplicate_in_window"));
}

void TtsSpeechPreparationTests::dedupeAllowsMateriallyDifferentText()
{
    UtteranceDedupeGuard guard(7000);
    UtteranceIdentity identity{QStringLiteral("assistant_reply"), QStringLiteral("finalizer"), QStringLiteral("turn-1"), QStringLiteral("goal")};

    QVERIFY(guard.evaluate(QStringLiteral("I opened README.md for you."), identity, 1000).admitted);
    QVERIFY(guard.evaluate(QStringLiteral("I updated build.bat and opened README.md."), identity, 1500).admitted);
}

void TtsSpeechPreparationTests::dedupeAllowsSameTextForDifferentTargets()
{
    UtteranceDedupeGuard guard(7000);
    UtteranceIdentity firstIdentity{QStringLiteral("assistant_reply"), QStringLiteral("finalizer"), QStringLiteral("turn-1"), QStringLiteral("file:A")};
    UtteranceIdentity secondIdentity{QStringLiteral("assistant_reply"), QStringLiteral("finalizer"), QStringLiteral("turn-1"), QStringLiteral("file:B")};

    QVERIFY(guard.evaluate(QStringLiteral("Opened it."), firstIdentity, 1000).admitted);
    QVERIFY(guard.evaluate(QStringLiteral("Opened it."), secondIdentity, 1100).admitted);
}

void TtsSpeechPreparationTests::technicalTokenReadmeIsSpokenNaturally()
{
    SpeechTextNormalizer normalizer;
    const QString spoken = normalizer.normalize(QStringLiteral("README.md"));

    QVERIFY(spoken.contains(QStringLiteral("readme dot M D")));
    QVERIFY(!spoken.contains(QStringLiteral("R E A D M E")));
    QVERIFY(!spoken.contains(QStringLiteral(". Md")));
}

void TtsSpeechPreparationTests::technicalTokenAcronymIsNotExploded()
{
    SpeechTextNormalizer normalizer;
    const QString spoken = normalizer.normalize(QStringLiteral("AI core"));

    QVERIFY(spoken.contains(QStringLiteral("AI core")));
    QVERIFY(!spoken.contains(QStringLiteral("A I core")));
}

void TtsSpeechPreparationTests::technicalTokenDllDoesNotCreateBrokenPunctuation()
{
    SpeechTextNormalizer normalizer;
    const QString spoken = normalizer.normalize(QStringLiteral("avcodec-61.dll"));

    QVERIFY(spoken.contains(QStringLiteral("avcodec 61 dot D L L")));
    QVERIFY(!spoken.contains(QStringLiteral("61. Dll")));
}

void TtsSpeechPreparationTests::technicalTokenCppIsStable()
{
    SpeechTextNormalizer normalizer;
    const QString spoken = normalizer.normalize(QStringLiteral("PromptAdapter.cpp"));

    QVERIFY(spoken.contains(QStringLiteral("Prompt Adapter dot C plus plus")));
    QVERIFY(!spoken.contains(QStringLiteral("Prompt Adapter. Cpp")));
}

void TtsSpeechPreparationTests::technicalTokenExtensionsAreConsistent()
{
    SpeechTextNormalizer normalizer;
    const QString spoken = normalizer.normalize(QStringLiteral("Open index.ts then run build.bat"));

    QVERIFY(spoken.contains(QStringLiteral("index dot T S")));
    QVERIFY(spoken.contains(QStringLiteral("build dot bat")));
}

void TtsSpeechPreparationTests::punctuationShaperHandlesQuotedSearchPhrase()
{
    SpeechPunctuationShaper shaper;
    const QString shaped = shaper.shape(QStringLiteral("I searched for \"best gpu?\" for you."));

    QVERIFY(shaped.contains(QStringLiteral("I searched for best gpu for you.")));
    QVERIFY(!shaped.contains(QStringLiteral("? for")));
}

void TtsSpeechPreparationTests::punctuationShaperDoesNotInjectFakeCommas()
{
    SpeechPunctuationShaper shaper;
    const QString shaped = shaper.shape(QStringLiteral("Working through it now."));

    QCOMPARE(shaped, QStringLiteral("Working through it now."));
}

void TtsSpeechPreparationTests::punctuationShaperRemovesEmbeddedQuestionMarkLeak()
{
    SpeechPunctuationShaper shaper;
    const QString shaped = shaper.shape(QStringLiteral("Search query \"what is GPT-5?\" is ready."));

    QVERIFY(!shaped.contains(QStringLiteral("? is ready")));
    QVERIFY(shaped.contains(QStringLiteral("Search query what is GPT-5 is ready.")));
}

void TtsSpeechPreparationTests::punctuationShaperKeepsSentenceBoundariesNatural()
{
    SpeechPunctuationShaper shaper;
    const QString shaped = shaper.shape(QStringLiteral("First result. Second result."));

    QCOMPARE(shaped, QStringLiteral("First result. Second result."));
}

void TtsSpeechPreparationTests::pipelineIsDeterministic()
{
    SpeechPreparationPipeline pipeline(7000);
    TtsUtteranceContext context;
    context.utteranceClass = QStringLiteral("assistant_reply");
    context.source = QStringLiteral("finalizer");
    context.turnId = QStringLiteral("turn-1");
    context.semanticTarget = QStringLiteral("goal");

    const auto first = pipeline.prepare(QStringLiteral("Open README.md and report status"), context, 1000);

    TtsUtteranceContext nextTurnContext = context;
    nextTurnContext.turnId = QStringLiteral("turn-2");
    const auto second = pipeline.prepare(QStringLiteral("Open README.md and report status"), nextTurnContext, 2000);

    QCOMPARE(first.normalizedText, second.normalizedText);
    QCOMPARE(first.punctuationShapedText, second.punctuationShapedText);
    QCOMPARE(first.finalSpokenText, second.finalSpokenText);
}

void TtsSpeechPreparationTests::pipelinePreservesTechnicalFormattingAfterShaping()
{
    SpeechPreparationPipeline pipeline(7000);
    TtsUtteranceContext context;
    context.utteranceClass = QStringLiteral("assistant_reply");
    context.source = QStringLiteral("finalizer");
    context.turnId = QStringLiteral("turn-1");
    context.semanticTarget = QStringLiteral("goal");

    const auto trace = pipeline.prepare(QStringLiteral("I opened \"README.md\" and PromptAdapter.cpp for you."), context, 1000);

    QVERIFY(trace.dedupeDecision.admitted);
    QVERIFY(trace.finalSpokenText.contains(QStringLiteral("readme dot M D")));
    QVERIFY(trace.finalSpokenText.contains(QStringLiteral("Prompt Adapter dot C plus plus")));
    QVERIFY(!trace.finalSpokenText.contains(QStringLiteral(". Md")));
}

QTEST_APPLESS_MAIN(TtsSpeechPreparationTests)
#include "TtsSpeechPreparationTests.moc"
