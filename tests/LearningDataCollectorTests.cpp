#include <QtTest>

#include "learning_data/LearningDataTypes.h"

class LearningDataCollectorTests : public QObject
{
    Q_OBJECT

private slots:
    void toolDecisionJsonRoundTripsSchema();
};

void LearningDataCollectorTests::toolDecisionJsonRoundTripsSchema()
{
    LearningData::ToolDecisionEvent input;
    input.sessionId = QStringLiteral("session-1");
    input.turnId = QStringLiteral("1");
    input.eventId = QStringLiteral("tool_decision_1");
    input.timestamp = LearningData::toIsoUtcNow();
    input.userInputText = QStringLiteral("open calculator");
    input.inputMode = QStringLiteral("voice");
    input.availableTools = {QStringLiteral("calculator"), QStringLiteral("notepad")};
    input.selectedTool = QStringLiteral("calculator");
    input.candidateToolsWithScores = {
        LearningData::ToolCandidateScore{QStringLiteral("calculator"), 0.91},
        LearningData::ToolCandidateScore{QStringLiteral("notepad"), 0.32}
    };
    input.decisionSource = QStringLiteral("heuristic");
    input.expectedConfirmationLevel = QStringLiteral("none");

    const QJsonObject json = LearningData::toJson(input);
    QCOMPARE(json.value(QStringLiteral("schema_version")).toString(), LearningData::kSchemaVersion);

    const LearningData::ToolDecisionEvent output = LearningData::toolDecisionEventFromJson(json);
    QCOMPARE(output.schemaVersion, LearningData::kSchemaVersion);
    QCOMPARE(output.sessionId, input.sessionId);
    QCOMPARE(output.turnId, input.turnId);
    QCOMPARE(output.eventId, input.eventId);
    QCOMPARE(output.selectedTool, input.selectedTool);
    QCOMPARE(output.availableTools.size(), input.availableTools.size());
    QCOMPARE(output.candidateToolsWithScores.size(), input.candidateToolsWithScores.size());
}

QTEST_APPLESS_MAIN(LearningDataCollectorTests)
#include "LearningDataCollectorTests.moc"
