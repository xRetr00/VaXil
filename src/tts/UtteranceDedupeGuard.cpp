#include "tts/UtteranceDedupeGuard.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDateTime>
#include <QRegularExpression>

namespace {
QString collapseWhitespace(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}

QString canonicalNearDuplicate(QString text)
{
    text = text.toLower();
    text.replace(QRegularExpression(QStringLiteral("[^a-z0-9\\s]")), QStringLiteral(" "));
    return collapseWhitespace(text);
}

QString hashFingerprint(const QStringList &parts)
{
    const QByteArray payload = parts.join(QStringLiteral("|")).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha1).toHex());
}

QString keyFieldSummary(const UtteranceIdentity &identity)
{
    return QStringLiteral("class=%1 source=%2 turn=%3 target=%4")
        .arg(identity.utteranceClass,
             identity.source,
             identity.turnId,
             identity.semanticTarget);
}
}

UtteranceDedupeGuard::UtteranceDedupeGuard(int dedupeWindowMs)
{
    setWindowMs(dedupeWindowMs);
}

void UtteranceDedupeGuard::setWindowMs(int dedupeWindowMs)
{
    m_windowMs = std::clamp(dedupeWindowMs, 1000, 60000);
}

int UtteranceDedupeGuard::windowMs() const
{
    return m_windowMs;
}

void UtteranceDedupeGuard::reset()
{
    m_recentExact.clear();
    m_recentNear.clear();
}

UtteranceDedupeDecision UtteranceDedupeGuard::evaluate(const QString &spokenText,
                                                       const UtteranceIdentity &identity,
                                                       qint64 nowMs)
{
    UtteranceDedupeDecision decision;
    decision.windowMs = m_windowMs;
    decision.keyFields = keyFieldSummary(identity);

    const QString strictText = collapseWhitespace(spokenText);
    if (strictText.isEmpty()) {
        decision.admitted = false;
        decision.reason = QStringLiteral("empty_spoken_text");
        return decision;
    }

    if (nowMs <= 0) {
        nowMs = QDateTime::currentMSecsSinceEpoch();
    }
    pruneExpired(nowMs);

    const QString normalizedClass = identity.utteranceClass.trimmed().isEmpty()
        ? QStringLiteral("assistant_reply")
        : identity.utteranceClass.trimmed().toLower();
    const QString normalizedSource = identity.source.trimmed().toLower();
    const QString normalizedTurnId = identity.turnId.trimmed().toLower();
    const QString normalizedTarget = identity.semanticTarget.trimmed().toLower();
    const QString exactText = strictText.toLower();
    const QString nearText = canonicalNearDuplicate(strictText);

    decision.exactFingerprint = hashFingerprint({
        QStringLiteral("v1"),
        normalizedClass,
        normalizedSource,
        normalizedTurnId,
        normalizedTarget,
        exactText
    });
    decision.nearFingerprint = hashFingerprint({
        QStringLiteral("v1-near"),
        normalizedClass,
        normalizedSource,
        normalizedTurnId,
        normalizedTarget,
        nearText
    });

    if (m_recentExact.contains(decision.exactFingerprint)) {
        decision.admitted = false;
        decision.reason = QStringLiteral("exact_duplicate_in_window");
        return decision;
    }

    if (!nearText.isEmpty() && m_recentNear.contains(decision.nearFingerprint)) {
        decision.admitted = false;
        decision.reason = QStringLiteral("near_duplicate_in_window");
        return decision;
    }

    m_recentExact.insert(decision.exactFingerprint, Entry{nowMs, strictText});
    if (!nearText.isEmpty()) {
        m_recentNear.insert(decision.nearFingerprint, Entry{nowMs, strictText});
    }

    return decision;
}

void UtteranceDedupeGuard::pruneExpired(qint64 nowMs)
{
    const qint64 cutoff = nowMs - static_cast<qint64>(m_windowMs);

    for (auto it = m_recentExact.begin(); it != m_recentExact.end();) {
        if (it->timestampMs < cutoff) {
            it = m_recentExact.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_recentNear.begin(); it != m_recentNear.end();) {
        if (it->timestampMs < cutoff) {
            it = m_recentNear.erase(it);
        } else {
            ++it;
        }
    }
}
