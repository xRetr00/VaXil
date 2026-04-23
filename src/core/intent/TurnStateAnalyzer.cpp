#include "core/intent/TurnStateAnalyzer.h"

#include <QRegularExpression>

namespace {
bool containsAnyWholePhrase(const QString &input, const QStringList &phrases)
{
    for (const QString &phrase : phrases) {
        QString pattern = QRegularExpression::escape(phrase);
        pattern.replace(QStringLiteral("\\ "), QStringLiteral("\\s+"));
        const QRegularExpression regex(QStringLiteral("(^|\\b)%1(\\b|$)").arg(pattern));
        if (regex.match(input).hasMatch()) {
            return true;
        }
    }
    return false;
}

bool startsWithAnyPhrase(const QString &input, const QStringList &phrases)
{
    for (const QString &phrase : phrases) {
        QString pattern = QRegularExpression::escape(phrase);
        pattern.replace(QStringLiteral("\\ "), QStringLiteral("\\s+"));
        const QRegularExpression regex(QStringLiteral("^\\s*%1(\\b|[,.!?;:]|\\s|$)").arg(pattern));
        if (regex.match(input).hasMatch()) {
            return true;
        }
    }
    return false;
}
}

TurnState TurnStateAnalyzer::analyze(const TurnStateInput &input) const
{
    TurnState state;
    const QString lowered = input.normalizedInput.trimmed().toLower();

    state.isConfirmationReply = input.hasPendingConfirmation
        && containsAnyWholePhrase(lowered, {
               QStringLiteral("yes"),
               QStringLiteral("yeah"),
               QStringLiteral("yep"),
               QStringLiteral("go ahead"),
               QStringLiteral("continue"),
               QStringLiteral("no"),
               QStringLiteral("cancel"),
               QStringLiteral("stop")
           });

    const bool correctionStart = startsWithAnyPhrase(lowered, {
        QStringLiteral("no"),
        QStringLiteral("nope"),
        QStringLiteral("not that"),
        QStringLiteral("not this"),
        QStringLiteral("i mean"),
        QStringLiteral("i meant"),
        QStringLiteral("actually"),
        QStringLiteral("instead"),
        QStringLiteral("that is not"),
        QStringLiteral("that's not")
    });
    const bool correctionPhrase = containsAnyWholePhrase(lowered, {
        QStringLiteral("i mean"),
        QStringLiteral("i meant"),
        QStringLiteral("that's wrong"),
        QStringLiteral("that is wrong"),
        QStringLiteral("not that"),
        QStringLiteral("not this"),
        QStringLiteral("instead of")
    });
    state.correctionDetected = correctionStart || correctionPhrase;
    state.correctionConfidence = correctionStart ? 0.88f : (correctionPhrase ? 0.72f : 0.0f);
    state.isCorrection = state.correctionDetected;

    state.refersToPreviousTask = input.hasUsableActionThread
        && (input.turnSignals.hasContinuationCue
            || input.turnSignals.hasContextReference
            || state.isCorrection
            || containsAnyWholePhrase(lowered, {
                   QStringLiteral("it"),
                   QStringLiteral("that"),
                   QStringLiteral("previous"),
                   QStringLiteral("result"),
                   QStringLiteral("what happened")
               }));

    state.isContinuation = state.refersToPreviousTask
        || input.turnSignals.hasContinuationCue
        || (state.isCorrection && input.hasAnyActionThread);
    state.isNewTurn = !state.isContinuation && !state.isConfirmationReply;

    if (state.isConfirmationReply) {
        state.reasonCodes.push_back(QStringLiteral("turn_state.confirmation_reply"));
    }
    if (state.isCorrection) {
        state.reasonCodes.push_back(QStringLiteral("turn_state.correction"));
        if (correctionStart) {
            state.reasonCodes.push_back(QStringLiteral("correction.detected.start_pattern"));
        }
        if (input.hasUsableActionThread || input.hasAnyActionThread) {
            state.reasonCodes.push_back(QStringLiteral("correction.bound_previous_context"));
        }
    }
    if (state.refersToPreviousTask) {
        state.reasonCodes.push_back(QStringLiteral("turn_state.refers_previous_task"));
    }
    if (state.isContinuation) {
        state.reasonCodes.push_back(QStringLiteral("turn_state.continuation"));
    }
    if (state.isNewTurn) {
        state.reasonCodes.push_back(QStringLiteral("turn_state.new_turn"));
    }

    return state;
}
