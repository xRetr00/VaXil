#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "core/AssistantTypes.h"

class MemoryStore : public QObject
{
    Q_OBJECT

public:
    explicit MemoryStore(QObject *parent = nullptr);

    void appendConversation(const QString &role, const QString &content);
    void extractUserFacts(const QString &content);
    QList<AiMessage> recentMessages(int maxCount) const;
    QList<MemoryRecord> relevantMemory(const QString &query) const;
    QList<MemoryEntry> searchEntries(const QString &query, int maxCount = 8) const;
    bool upsertEntry(const MemoryEntry &entry);
    bool deleteEntry(const QString &idOrTitle);
    QList<MemoryEntry> allEntries() const;
    QString userName() const;

private:
    QString transcriptPath() const;
    QString memoryPath() const;
    QList<MemoryRecord> loadMemory() const;
    QList<MemoryEntry> loadEntries() const;
    bool saveEntries(const QList<MemoryEntry> &entries) const;
    MemoryEntry normalizeEntry(const MemoryEntry &entry) const;
};
