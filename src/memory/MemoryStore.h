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
    QString userName() const;

private:
    QString transcriptPath() const;
    QString memoryPath() const;
    QList<MemoryRecord> loadMemory() const;
    void saveMemory(const QList<MemoryRecord> &records) const;
};
