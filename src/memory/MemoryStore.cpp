#include "memory/MemoryStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

#include <nlohmann/json.hpp>

namespace {
QString appDataRoot()
{
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
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
}

MemoryStore::MemoryStore(QObject *parent)
    : QObject(parent)
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

    if (role == QStringLiteral("user")) {
        extractUserFacts(content);
    }
}

void MemoryStore::extractUserFacts(const QString &content)
{
    const QString lowered = content.trimmed().toLower();

    auto saveStructured = [this](const QString &kind, const QString &title, const QString &value, const QStringList &tags = {}) {
        MemoryEntry entry;
        entry.id = slugify(kind + QStringLiteral("-") + title);
        entry.kind = kind;
        entry.title = title;
        entry.content = value.trimmed();
        entry.tags = tags;
        entry.confidence = 0.92f;
        entry.source = QStringLiteral("conversation");
        upsertEntry(entry);
    };

    if (lowered.startsWith(QStringLiteral("my name is "))) {
        saveStructured(QStringLiteral("profile"), QStringLiteral("name"), content.mid(11), {QStringLiteral("identity")});
    } else if (lowered.startsWith(QStringLiteral("i prefer "))) {
        saveStructured(QStringLiteral("preference"), QStringLiteral("general"), content.mid(9), {QStringLiteral("preference")});
    } else if (lowered.startsWith(QStringLiteral("remember that "))) {
        saveStructured(QStringLiteral("project_fact"), QStringLiteral("remembered-fact"), content.mid(12), {QStringLiteral("remembered")});
    } else if (lowered.startsWith(QStringLiteral("my project is "))) {
        saveStructured(QStringLiteral("project_fact"), QStringLiteral("project"), content.mid(14), {QStringLiteral("project")});
    }
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
    for (const auto &entry : searchEntries(query, 12)) {
        relevant.push_back({
            .type = entry.kind,
            .key = entry.title,
            .value = entry.content,
            .confidence = entry.confidence,
            .source = entry.source,
            .updatedAt = entry.updatedAt
        });
    }
    return relevant;
}

QList<MemoryEntry> MemoryStore::searchEntries(const QString &query, int maxCount) const
{
    const QString lowered = query.toLower();
    QList<MemoryEntry> relevant;
    for (const auto &entry : loadEntries()) {
        const QString haystack = QStringList{entry.kind, entry.title, entry.content, entry.tags.join(' ')}.join(' ').toLower();
        if (lowered.isEmpty() || haystack.contains(lowered)) {
            relevant.push_back(entry);
        }
    }

    if (relevant.size() > maxCount) {
        relevant = relevant.mid(0, maxCount);
    }
    return relevant;
}

bool MemoryStore::upsertEntry(const MemoryEntry &entry)
{
    QList<MemoryEntry> entries = loadEntries();
    const MemoryEntry normalized = normalizeEntry(entry);
    for (MemoryEntry &existing : entries) {
        if ((!normalized.id.isEmpty() && existing.id == normalized.id)
            || (!normalized.title.isEmpty() && existing.title == normalized.title && existing.kind == normalized.kind)) {
            existing = normalized;
            return saveEntries(entries);
        }
    }

    entries.push_back(normalized);
    return saveEntries(entries);
}

bool MemoryStore::deleteEntry(const QString &idOrTitle)
{
    QList<MemoryEntry> entries = loadEntries();
    const int previousSize = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const MemoryEntry &entry) {
        return entry.id == idOrTitle || entry.title == idOrTitle;
    }), entries.end());
    if (entries.size() == previousSize) {
        return false;
    }
    return saveEntries(entries);
}

QList<MemoryEntry> MemoryStore::allEntries() const
{
    return loadEntries();
}

QString MemoryStore::userName() const
{
    for (const auto &entry : loadEntries()) {
        if (entry.kind == QStringLiteral("profile") && entry.title == QStringLiteral("name")) {
            return entry.content;
        }
    }

    return {};
}

QString MemoryStore::transcriptPath() const
{
    return appDataRoot() + QStringLiteral("/transcript.jsonl");
}

QString MemoryStore::memoryPath() const
{
    return appDataRoot() + QStringLiteral("/memory.json");
}

QList<MemoryRecord> MemoryStore::loadMemory() const
{
    QList<MemoryRecord> records;
    for (const auto &entry : loadEntries()) {
        records.push_back({
            .type = entry.kind,
            .key = entry.title,
            .value = entry.content,
            .confidence = entry.confidence,
            .source = entry.source,
            .updatedAt = entry.updatedAt
        });
    }
    return records;
}

QList<MemoryEntry> MemoryStore::loadEntries() const
{
    QFile file(memoryPath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const auto parsed = nlohmann::json::parse(file.readAll().constData(), nullptr, false);
    if (!parsed.is_array()) {
        return {};
    }

    QList<MemoryEntry> entries;
    for (const auto &entry : parsed) {
        MemoryEntry memoryEntry;
        if (entry.contains("kind")) {
            memoryEntry.id = QString::fromStdString(entry.value("id", std::string{}));
            memoryEntry.kind = QString::fromStdString(entry.value("kind", std::string{}));
            memoryEntry.title = QString::fromStdString(entry.value("title", std::string{}));
            memoryEntry.content = QString::fromStdString(entry.value("content", std::string{}));
            memoryEntry.confidence = entry.value("confidence", 0.0f);
            memoryEntry.secret = entry.value("secret", false);
            memoryEntry.source = QString::fromStdString(entry.value("source", std::string{}));
            memoryEntry.updatedAt = QString::fromStdString(entry.value("updatedAt", std::string{}));
            if (entry.contains("tags") && entry.at("tags").is_array()) {
                for (const auto &tag : entry.at("tags")) {
                    if (tag.is_string()) {
                        memoryEntry.tags.push_back(QString::fromStdString(tag.get<std::string>()));
                    }
                }
            }
        } else {
            memoryEntry.kind = QString::fromStdString(entry.value("type", std::string{}));
            memoryEntry.title = QString::fromStdString(entry.value("key", std::string{}));
            memoryEntry.content = QString::fromStdString(entry.value("value", std::string{}));
            memoryEntry.confidence = entry.value("confidence", 0.0f);
            memoryEntry.source = QString::fromStdString(entry.value("source", std::string{}));
            memoryEntry.updatedAt = QString::fromStdString(entry.value("updatedAt", std::string{}));
        }
        entries.push_back(normalizeEntry(memoryEntry));
    }

    return entries;
}

bool MemoryStore::saveEntries(const QList<MemoryEntry> &entries) const
{
    nlohmann::json json = nlohmann::json::array();
    for (const auto &entry : entries) {
        json.push_back({
            {"id", entry.id.toStdString()},
            {"kind", entry.kind.toStdString()},
            {"title", entry.title.toStdString()},
            {"content", entry.content.toStdString()},
            {"tags", nlohmann::json::array()},
            {"confidence", entry.confidence},
            {"secret", entry.secret},
            {"source", entry.source.toStdString()},
            {"updatedAt", entry.updatedAt.toStdString()}
        });
        auto &last = json.back();
        for (const QString &tag : entry.tags) {
            last["tags"].push_back(tag.toStdString());
        }
    }

    QFile file(memoryPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    file.write(QByteArray::fromStdString(json.dump(2)));
    return true;
}

MemoryEntry MemoryStore::normalizeEntry(const MemoryEntry &entry) const
{
    MemoryEntry normalized = entry;
    normalized.kind = normalized.kind.trimmed();
    normalized.title = normalized.title.trimmed();
    normalized.content = normalized.content.trimmed();
    if (normalized.id.trimmed().isEmpty()) {
        normalized.id = slugify(normalized.kind + QStringLiteral("-") + normalized.title);
    }
    if (normalized.updatedAt.trimmed().isEmpty()) {
        normalized.updatedAt = nowIso();
    }
    if (normalized.source.trimmed().isEmpty()) {
        normalized.source = QStringLiteral("agent");
    }
    return normalized;
}
