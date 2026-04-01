#include "memory/MemoryStore.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>

#include <memory>

#include <nlohmann/json.hpp>

#include "memory/MemoryManager.h"

namespace {
QString appDataRoot()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);
    return root;
}

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString slugify(QString value)
{
    value = value.toLower().trimmed();
    for (QChar &ch : value) {
        if (!ch.isLetterOrNumber()) {
            ch = QChar::fromLatin1('-');
        }
    }
    while (value.contains(QStringLiteral("--"))) {
        value.replace(QStringLiteral("--"), QStringLiteral("-"));
    }
    return value.trimmed().remove(QRegularExpression(QStringLiteral("^-+|-+$")));
}

MemoryType inferMemoryType(const QString &content)
{
    const QString lowered = content.toLower();
    if (lowered.contains(QStringLiteral("prefer"))
        || lowered.contains(QStringLiteral("i like"))
        || lowered.contains(QStringLiteral("short answers"))
        || lowered.contains(QStringLiteral("long answers"))) {
        return MemoryType::Preference;
    }

    return MemoryType::Fact;
}

QString inferMemoryKey(const QString &content, MemoryType type)
{
    const QString lowered = content.toLower();
    if (type == MemoryType::Preference) {
        if (lowered.contains(QStringLiteral("short answers"))) {
            return QStringLiteral("response_style");
        }
        if (lowered.contains(QStringLiteral("long answers"))) {
            return QStringLiteral("response_style");
        }
        if (lowered.contains(QStringLiteral("voice"))) {
            return QStringLiteral("voice_preference");
        }
        return QStringLiteral("general_preference");
    }

    if (lowered.startsWith(QStringLiteral("my name is "))) {
        return QStringLiteral("name");
    }
    if (lowered.contains(QStringLiteral("project"))) {
        return QStringLiteral("project_fact");
    }
    return QStringLiteral("general_fact");
}
}

MemoryStore::MemoryStore(QObject *parent)
    : QObject(parent)
    , m_memoryManager(std::make_unique<MemoryManager>())
{
}

void MemoryStore::appendConversation(const QString &role, const QString &content)
{
    QFile file(transcriptPath());
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }

    nlohmann::json line = {
        {"timestamp", nowIso().toStdString()},
        {"role", role.toStdString()},
        {"content", content.toStdString()}
    };

    file.write(QByteArray::fromStdString(line.dump()));
    file.write("\n");
}

void MemoryStore::extractUserFacts(const QString &content)
{
    QString normalizedText = content.trimmed();
    if (normalizedText.isEmpty()) {
        return;
    }

    normalizedText.remove(QRegularExpression(
        QStringLiteral("^(remember that|remember|save this preference|my name is|i prefer)\\s+"),
        QRegularExpression::CaseInsensitiveOption));
    normalizedText = normalizedText.trimmed();
    if (normalizedText.isEmpty()) {
        return;
    }

    MemoryEntry entry;
    entry.type = inferMemoryType(content);
    entry.kind = entry.type == MemoryType::Preference ? QStringLiteral("preference") : QStringLiteral("fact");
    entry.key = inferMemoryKey(content, entry.type);
    entry.title = entry.key;
    entry.value = normalizedText;
    entry.content = normalizedText;
    entry.id = slugify(entry.kind + QStringLiteral("-") + entry.key);
    entry.confidence = 0.92f;
    entry.source = QStringLiteral("conversation");
    entry.createdAt = QDateTime::currentDateTimeUtc();
    if (entry.type == MemoryType::Context) {
        entry.expiresAt = entry.createdAt.addDays(7);
    }
    upsertEntry(entry);
}

QList<AiMessage> MemoryStore::recentMessages(int maxCount) const
{
    QFile file(transcriptPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QList<AiMessage> result;
    while (!file.atEnd()) {
        const auto line = file.readLine();
        const auto parsed = nlohmann::json::parse(line.constData(), nullptr, false);
        if (parsed.is_discarded()) {
            continue;
        }

        result.push_back({
            .role = QString::fromStdString(parsed.value("role", std::string{})),
            .content = QString::fromStdString(parsed.value("content", std::string{}))
        });
    }

    if (result.size() > maxCount) {
        result = result.mid(result.size() - maxCount);
    }

    return result;
}

QList<MemoryRecord> MemoryStore::relevantMemory(const QString &query) const
{
    QList<MemoryRecord> relevant;
    for (const MemoryEntry &entry : searchEntries(query, 12)) {
        relevant.push_back({
            .type = entry.kind,
            .key = entry.key,
            .value = entry.value,
            .confidence = entry.confidence,
            .source = entry.source,
            .updatedAt = entry.updatedAt
        });
    }
    return relevant;
}

QList<MemoryEntry> MemoryStore::searchEntries(const QString &query, int maxCount) const
{
    return m_memoryManager->search(query, maxCount);
}

bool MemoryStore::upsertEntry(const MemoryEntry &entry)
{
    return m_memoryManager->write(normalizeEntry(entry));
}

bool MemoryStore::deleteEntry(const QString &idOrTitle)
{
    return m_memoryManager->remove(idOrTitle);
}

QList<MemoryEntry> MemoryStore::allEntries() const
{
    return m_memoryManager->entries();
}

QString MemoryStore::userName() const
{
    return m_memoryManager->userName();
}

QString MemoryStore::transcriptPath() const
{
    return appDataRoot() + QStringLiteral("/transcript.jsonl");
}

QList<MemoryRecord> MemoryStore::loadMemory() const
{
    QList<MemoryRecord> records;
    for (const MemoryEntry &entry : allEntries()) {
        records.push_back({
            .type = entry.kind,
            .key = entry.key,
            .value = entry.value,
            .confidence = entry.confidence,
            .source = entry.source,
            .updatedAt = entry.updatedAt
        });
    }
    return records;
}

MemoryEntry MemoryStore::normalizeEntry(const MemoryEntry &entry) const
{
    MemoryEntry normalized = entry;

    if (normalized.kind.trimmed().isEmpty()) {
        normalized.kind = normalized.type == MemoryType::Preference
            ? QStringLiteral("preference")
            : (normalized.type == MemoryType::Context ? QStringLiteral("context") : QStringLiteral("fact"));
    }

    if (normalized.key.trimmed().isEmpty()) {
        normalized.key = normalized.title.trimmed();
    }
    if (normalized.title.trimmed().isEmpty()) {
        normalized.title = normalized.key.trimmed();
    }
    if (normalized.value.trimmed().isEmpty()) {
        normalized.value = normalized.content.trimmed();
    }
    if (normalized.content.trimmed().isEmpty()) {
        normalized.content = normalized.value.trimmed();
    }
    if (!normalized.createdAt.isValid()) {
        normalized.createdAt = QDateTime::currentDateTimeUtc();
    }
    if (normalized.id.trimmed().isEmpty()) {
        normalized.id = slugify(normalized.kind + QStringLiteral("-") + normalized.key);
    }
    if (normalized.updatedAt.trimmed().isEmpty()) {
        normalized.updatedAt = normalized.createdAt.toUTC().toString(Qt::ISODate);
    }
    if (normalized.source.trimmed().isEmpty()) {
        normalized.source = QStringLiteral("agent");
    }

    return normalized;
}
