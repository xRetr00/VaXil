#include "memory/MemoryStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QStringView>

#include <nlohmann/json.hpp>

namespace {
QString appDataRoot()
{
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);
    return root;
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
        {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()},
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
    QList<MemoryRecord> records = loadMemory();
    const auto lowered = content.toLower();

    auto upsert = [&](const QString &type, const QString &key, const QString &value) {
        for (auto &record : records) {
            if (record.type == type && record.key == key) {
                record.value = value;
                record.confidence = 0.92f;
                record.updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
                saveMemory(records);
                return;
            }
        }

        records.push_back(MemoryRecord{
            .type = type,
            .key = key,
            .value = value,
            .confidence = 0.92f,
            .source = QStringLiteral("conversation"),
            .updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate)
        });
        saveMemory(records);
    };

    if (lowered.startsWith(QStringLiteral("my name is "))) {
        upsert(QStringLiteral("profile"), QStringLiteral("name"), content.mid(11).trimmed());
    } else if (lowered.startsWith(QStringLiteral("i prefer "))) {
        upsert(QStringLiteral("preference"), QStringLiteral("preference"), content.mid(9).trimmed());
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
    const auto lowered = query.toLower();
    for (const auto &record : loadMemory()) {
        if (lowered.contains(record.key.toLower()) || lowered.contains(record.value.toLower())) {
            relevant.push_back(record);
        }
    }

    return relevant;
}

QString MemoryStore::userName() const
{
    for (const auto &record : loadMemory()) {
        if (record.type == QStringLiteral("profile") && record.key == QStringLiteral("name")) {
            return record.value;
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
    QFile file(memoryPath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const auto parsed = nlohmann::json::parse(file.readAll().constData(), nullptr, false);
    if (!parsed.is_array()) {
        return {};
    }

    QList<MemoryRecord> records;
    for (const auto &entry : parsed) {
        records.push_back({
            .type = QString::fromStdString(entry.value("type", std::string{})),
            .key = QString::fromStdString(entry.value("key", std::string{})),
            .value = QString::fromStdString(entry.value("value", std::string{})),
            .confidence = entry.value("confidence", 0.0f),
            .source = QString::fromStdString(entry.value("source", std::string{})),
            .updatedAt = QString::fromStdString(entry.value("updatedAt", std::string{}))
        });
    }

    return records;
}

void MemoryStore::saveMemory(const QList<MemoryRecord> &records) const
{
    nlohmann::json json = nlohmann::json::array();
    for (const auto &record : records) {
        json.push_back({
            {"type", record.type.toStdString()},
            {"key", record.key.toStdString()},
            {"value", record.value.toStdString()},
            {"confidence", record.confidence},
            {"source", record.source.toStdString()},
            {"updatedAt", record.updatedAt.toStdString()}
        });
    }

    QFile file(memoryPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return;
    }

    file.write(QByteArray::fromStdString(json.dump(2)));
}
