#pragma once

#include <QHash>
#include <QString>

struct UtteranceIdentity
{
    QString utteranceClass;
    QString source;
    QString turnId;
    QString semanticTarget;
};

struct UtteranceDedupeDecision
{
    bool admitted = true;
    QString reason = QStringLiteral("admitted");
    QString exactFingerprint;
    QString nearFingerprint;
    QString keyFields;
    int windowMs = 0;
};

class UtteranceDedupeGuard
{
public:
    explicit UtteranceDedupeGuard(int dedupeWindowMs = 7000);

    void setWindowMs(int dedupeWindowMs);
    int windowMs() const;
    void reset();

    UtteranceDedupeDecision evaluate(const QString &spokenText,
                                     const UtteranceIdentity &identity,
                                     qint64 nowMs = 0);

private:
    struct Entry
    {
        qint64 timestampMs = 0;
        QString sampleText;
    };

    void pruneExpired(qint64 nowMs);

    int m_windowMs = 7000;
    QHash<QString, Entry> m_recentExact;
    QHash<QString, Entry> m_recentNear;
};
