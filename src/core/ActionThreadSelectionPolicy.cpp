#include "core/ActionThreadSelectionPolicy.h"

#include <algorithm>

namespace {
QString normalized(QString text)
{
    return text.simplified().toLower();
}

QStringList wordsFor(const QString &text)
{
    return normalized(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

bool containsAny(const QString &text, const QStringList &needles)
{
    for (const QString &needle : needles) {
        if (text.contains(needle)) {
            return true;
        }
    }
    return false;
}

bool startsWithAny(const QString &text, const QStringList &prefixes)
{
    for (const QString &prefix : prefixes) {
        if (text.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

bool isReferential(const QString &text)
{
    return containsAny(text, {
        QStringLiteral(" it"),
        QStringLiteral(" that"),
        QStringLiteral(" those"),
        QStringLiteral(" them"),
        QStringLiteral(" the result"),
        QStringLiteral(" results"),
        QStringLiteral("from the result"),
        QStringLiteral("what did you see"),
        QStringLiteral("what happened"),
        QStringLiteral("what have you done"),
        QStringLiteral("what were you doing"),
        QStringLiteral("what did you do"),
        QStringLiteral("continue"),
        QStringLiteral("retry"),
        QStringLiteral("try again"),
        QStringLiteral("open it"),
        QStringLiteral("use it"),
        QStringLiteral("do that")
    }) || text == QStringLiteral("open it")
        || text == QStringLiteral("continue")
        || text == QStringLiteral("retry that")
        || text == QStringLiteral("try again");
}

bool isRetryRequest(const QString &text)
{
    return text == QStringLiteral("retry")
        || text == QStringLiteral("retry that")
        || text == QStringLiteral("try again")
        || text.startsWith(QStringLiteral("retry "))
        || text.startsWith(QStringLiteral("try that again"));
}

bool isAuditQuestion(const QString &text)
{
    return containsAny(text, {
        QStringLiteral("what were you doing"),
        QStringLiteral("what have you done"),
        QStringLiteral("what did you do"),
        QStringLiteral("what happened"),
        QStringLiteral("what were we doing"),
        QStringLiteral("what was the task")
    });
}

bool isCancelOnly(const QString &text)
{
    const QStringList words = wordsFor(text);
    if (words.size() > 8) {
        return false;
    }
    return containsAny(text, {
        QStringLiteral("never mind"),
        QStringLiteral("nevermind"),
        QStringLiteral("cancel"),
        QStringLiteral("stop"),
        QStringLiteral("skip this task"),
        QStringLiteral("skip that task"),
        QStringLiteral("forget it")
    });
}

bool isClearlyFreshRequest(const QString &text, const InputRouteDecision &decision)
{
    if (decision.kind == InputRouteKind::LocalResponse
        || decision.kind == InputRouteKind::AgentCapabilityError
        || decision.kind == InputRouteKind::None) {
        return true;
    }

    if (startsWithAny(text, {
            QStringLiteral("create "),
            QStringLiteral("make "),
            QStringLiteral("write "),
            QStringLiteral("search "),
            QStringLiteral("look up "),
            QStringLiteral("find "),
            QStringLiteral("summarize "),
            QStringLiteral("tell me "),
            QStringLiteral("explain "),
            QStringLiteral("read "),
            QStringLiteral("set "),
            QStringLiteral("build ")
        })) {
        return true;
    }

    if (text.startsWith(QStringLiteral("open "))
        && text != QStringLiteral("open it")
        && text != QStringLiteral("open that")
        && text != QStringLiteral("open the result")) {
        return true;
    }

    return false;
}

QString threadLabel(const ActionThread &thread)
{
    QString label = thread.userGoal.trimmed();
    if (label.isEmpty()) {
        label = thread.taskType.trimmed();
    }
    if (label.size() > 80) {
        label = label.left(80).trimmed() + QStringLiteral("...");
    }
    return label.isEmpty() ? QStringLiteral("recent task") : label;
}

QList<ActionThread> sortedUsableThreads(const QList<ActionThread> &threads, qint64 nowMs)
{
    QList<ActionThread> usable;
    for (const ActionThread &thread : threads) {
        if (thread.isUsable(nowMs)) {
            usable.push_back(thread);
        }
    }
    std::sort(usable.begin(), usable.end(), [](const ActionThread &left, const ActionThread &right) {
        return left.updatedAtMs > right.updatedAtMs;
    });
    return usable;
}

std::optional<ActionThread> firstThreadInState(const QList<ActionThread> &threads, ActionThreadState state)
{
    for (const ActionThread &thread : threads) {
        if (thread.state == state) {
            return thread;
        }
    }
    return std::nullopt;
}

QList<ActionThread> resumableThreads(const QList<ActionThread> &threads)
{
    QList<ActionThread> resumable;
    for (const ActionThread &thread : threads) {
        if (thread.state != ActionThreadState::Canceled
            && thread.state != ActionThreadState::None) {
            resumable.push_back(thread);
        }
    }
    return resumable;
}
}

ActionThreadSelectionResult ActionThreadSelectionPolicy::select(const ActionThreadSelectionInput &input)
{
    ActionThreadSelectionResult result;
    const QString text = normalized(input.userInput);
    if (text.isEmpty()) {
        result.reasonCode = QStringLiteral("action_thread.empty_input");
        return result;
    }

    const QList<ActionThread> recent = sortedUsableThreads(input.recentThreads, input.nowMs);
    if (recent.isEmpty()) {
        result.reasonCode = QStringLiteral("action_thread.no_recent_threads");
        return result;
    }

    if (input.privateMode && isReferential(text)) {
        result.kind = ActionThreadSelectionKind::PrivateContextBlocked;
        result.reasonCode = QStringLiteral("action_thread.private_context_blocked");
        result.userMessage = QStringLiteral("I need your permission before using private desktop context for that. Which item should I use?");
        return result;
    }

    if (isCancelOnly(text)) {
        result.kind = ActionThreadSelectionKind::AuditOnlyCanceled;
        result.thread = recent.first();
        result.reasonCode = QStringLiteral("action_thread.cancel_requested");
        result.userMessage = QStringLiteral("Canceled the current task. I can explain what was in progress if you ask.");
        return result;
    }

    if (isAuditQuestion(text)) {
        const std::optional<ActionThread> canceled = firstThreadInState(recent, ActionThreadState::Canceled);
        const ActionThread thread = canceled.has_value() ? *canceled : recent.first();
        result.kind = canceled.has_value()
            ? ActionThreadSelectionKind::AuditOnlyCanceled
            : ActionThreadSelectionKind::Attach;
        result.thread = thread;
        result.reasonCode = canceled.has_value()
            ? QStringLiteral("action_thread.canceled_audit")
            : QStringLiteral("action_thread.audit_recent");
        result.userMessage = QStringLiteral("The recent task was: %1").arg(threadLabel(thread));
        return result;
    }

    if (isRetryRequest(text)) {
        const std::optional<ActionThread> failed = firstThreadInState(recent, ActionThreadState::Failed);
        if (failed.has_value()) {
            result.kind = ActionThreadSelectionKind::RetryFailed;
            result.thread = *failed;
            result.reasonCode = QStringLiteral("action_thread.retry_failed");
            return result;
        }
        result.kind = ActionThreadSelectionKind::AskClarification;
        result.ambiguousThreads = recent.mid(0, std::min<int>(3, static_cast<int>(recent.size())));
        result.reasonCode = QStringLiteral("action_thread.retry_without_failed_thread");
        result.userMessage = QStringLiteral("Which task should I retry?");
        return result;
    }

    if (isClearlyFreshRequest(text, input.routeDecision)) {
        result.reasonCode = QStringLiteral("action_thread.fresh_explicit_request");
        return result;
    }

    if (!isReferential(text)) {
        const std::optional<ActionThread> running = firstThreadInState(recent, ActionThreadState::Running);
        const bool shortTurn = wordsFor(text).size() <= 5;
        if (running.has_value() && shortTurn && containsAny(text, {QStringLiteral("continue"), QStringLiteral("go on")})) {
            result.kind = ActionThreadSelectionKind::Attach;
            result.thread = *running;
            result.reasonCode = QStringLiteral("action_thread.running_short_continue");
            return result;
        }
        result.reasonCode = QStringLiteral("action_thread.non_referential_fresh");
        return result;
    }

    const QList<ActionThread> candidates = resumableThreads(recent);
    if (candidates.isEmpty()) {
        result.kind = ActionThreadSelectionKind::AuditOnlyCanceled;
        result.thread = recent.first();
        result.reasonCode = QStringLiteral("action_thread.only_canceled_threads");
        result.userMessage = QStringLiteral("That task was canceled. I will only resume it if you explicitly ask me to retry it.");
        return result;
    }

    if (candidates.size() > 1 && wordsFor(text).size() <= 4) {
        result.kind = ActionThreadSelectionKind::AskClarification;
        result.ambiguousThreads = candidates.mid(0, std::min<int>(3, static_cast<int>(candidates.size())));
        result.reasonCode = QStringLiteral("action_thread.ambiguous_referent");
        QStringList labels;
        for (const ActionThread &candidate : result.ambiguousThreads) {
            labels.push_back(threadLabel(candidate));
        }
        result.userMessage = QStringLiteral("Which recent task do you mean: %1?").arg(labels.join(QStringLiteral(" / ")));
        return result;
    }

    result.kind = ActionThreadSelectionKind::Attach;
    result.thread = candidates.first();
    result.reasonCode = QStringLiteral("action_thread.single_clear_referent");
    return result;
}
