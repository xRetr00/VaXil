#include "cognition/SuggestionProposalBuilder.h"

namespace {
QString normalizedTaskType(const QString &taskType)
{
    return taskType.trimmed().toLower();
}

bool containsAny(const QString &text, std::initializer_list<const char *> needles)
{
    for (const char *needle : needles) {
        if (text.contains(QString::fromUtf8(needle))) {
            return true;
        }
    }
    return false;
}
}

QList<ActionProposal> SuggestionProposalBuilder::build(const Input &input)
{
    QList<ActionProposal> proposals;
    const QString taskType = normalizedTaskType(input.taskType);

    if (!input.sourceUrls.isEmpty()) {
        appendUnique(proposals,
                     makeProposal(QStringLiteral("source_review"),
                                  QStringLiteral("Review sources"),
                                  QStringLiteral("I can open one of the sources or summarize the findings."),
                                  QStringLiteral("medium"),
                                  input));
    }

    if (taskType == QStringLiteral("clipboard")) {
        appendUnique(proposals,
                     makeProposal(QStringLiteral("clipboard_follow_up"),
                                  QStringLiteral("Use clipboard"),
                                  QStringLiteral("I can summarize what you copied or turn it into a quick note."),
                                  QStringLiteral("medium"),
                                  input));
    } else if (taskType == QStringLiteral("notification")) {
        appendUnique(proposals,
                     makeProposal(QStringLiteral("notification_triage"),
                                  QStringLiteral("Triage notification"),
                                  QStringLiteral("I can summarize this notification or help you decide what matters."),
                                  QStringLiteral("medium"),
                                  input));
    } else if (taskType == QStringLiteral("web_search") || taskType == QStringLiteral("web_fetch")) {
        appendUnique(proposals,
                     makeProposal(QStringLiteral("web_follow_up"),
                                  QStringLiteral("Compare findings"),
                                  QStringLiteral("I can compare the sources, extract the key points, or open one."),
                                  QStringLiteral("medium"),
                                  input));
    } else if (containsAny(taskType, {"calendar", "schedule", "meeting", "timer", "reminder"})) {
        appendUnique(proposals,
                     makeProposal(QStringLiteral("schedule_follow_up"),
                                  QStringLiteral("Review schedule"),
                                  QStringLiteral("I can turn this into a reminder, a short plan, or a quick schedule summary."),
                                  QStringLiteral("medium"),
                                  input));
    } else if (containsAny(taskType, {"email", "mail", "message", "inbox"})) {
        appendUnique(proposals,
                     makeProposal(QStringLiteral("inbox_follow_up"),
                                  QStringLiteral("Triage messages"),
                                  QStringLiteral("I can summarize the important messages or help you decide what needs a reply."),
                                  QStringLiteral("medium"),
                                  input));
    } else if (containsAny(taskType, {"note", "memo", "draft", "write"})) {
        appendUnique(proposals,
                     makeProposal(QStringLiteral("note_follow_up"),
                                  QStringLiteral("Capture note"),
                                  QStringLiteral("I can turn this into a short note, checklist, or saved summary."),
                                  QStringLiteral("medium"),
                                  input));
    } else if (taskType.contains(QStringLiteral("file")) || taskType.contains(QStringLiteral("code")) || taskType == QStringLiteral("dir_list")) {
        appendUnique(proposals,
                     makeProposal(QStringLiteral("document_follow_up"),
                                  QStringLiteral("Inspect files"),
                                  QStringLiteral("I can pull out the important parts, compare files, or turn them into a short summary."),
                                  QStringLiteral("medium"),
                                  input));
    } else if (taskType.contains(QStringLiteral("browser")) || taskType.contains(QStringLiteral("page")) || taskType.contains(QStringLiteral("computer_open"))) {
        appendUnique(proposals,
                     makeProposal(QStringLiteral("browser_follow_up"),
                                  QStringLiteral("Continue in browser"),
                                  QStringLiteral("I can open the relevant page again or extract the useful details."),
                                  QStringLiteral("medium"),
                                  input));
    } else if (!taskType.isEmpty()) {
        appendUnique(proposals,
                     makeProposal(QStringLiteral("task_follow_up"),
                                  QStringLiteral("Continue task"),
                                  QStringLiteral("I can take the next step or turn this into a short summary."),
                                  QStringLiteral("low"),
                                  input));
    }

    if (!input.success) {
        appendUnique(proposals,
                     makeProposal(QStringLiteral("failure_recovery"),
                                  QStringLiteral("Recover from failure"),
                                  QStringLiteral("I can try a different approach or troubleshoot what failed."),
                                  QStringLiteral("high"),
                                  input));
    } else if (!input.resultSummary.trimmed().isEmpty()) {
        appendUnique(proposals,
                     makeProposal(QStringLiteral("result_summary"),
                                  QStringLiteral("Summarize result"),
                                  QStringLiteral("I can turn this result into a short summary or checklist."),
                                  QStringLiteral("low"),
                                  input));
    }

    return proposals;
}

void SuggestionProposalBuilder::appendUnique(QList<ActionProposal> &proposals, const ActionProposal &proposal)
{
    for (const ActionProposal &existing : proposals) {
        if (existing.summary.compare(proposal.summary, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    proposals.push_back(proposal);
}

ActionProposal SuggestionProposalBuilder::makeProposal(const QString &capabilityId,
                                                       const QString &title,
                                                       const QString &summary,
                                                       const QString &priority,
                                                       const Input &input)
{
    ActionProposal proposal;
    proposal.proposalId = QStringLiteral("%1:%2:%3")
                              .arg(capabilityId,
                                   input.taskType.trimmed().isEmpty() ? QStringLiteral("task") : input.taskType.trimmed(),
                                   input.sourceKind.trimmed().isEmpty() ? QStringLiteral("default") : input.sourceKind.trimmed());
    proposal.capabilityId = capabilityId;
    proposal.title = title;
    proposal.summary = summary;
    proposal.priority = priority;
    proposal.arguments = {
        {QStringLiteral("sourceKind"), input.sourceKind},
        {QStringLiteral("taskType"), input.taskType},
        {QStringLiteral("success"), input.success},
        {QStringLiteral("sourceCount"), input.sourceUrls.size()}
    };
    return proposal;
}
