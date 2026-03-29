#include "memory/MemoryManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace {
QMutex &memoryMutex()
{
    static QMutex mutex;
    return mutex;
}

QString appDataRoot()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);
    return root;
}

QString defaultStoragePath()
{
    return appDataRoot() + QStringLiteral("/memory.json");
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

    return value.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
}

QString memoryTypeToKind(MemoryType type)
{
    switch (type) {
    case MemoryType::Preference:
        return QStringLiteral("preference");
    case MemoryType::Fact:
        return QStringLiteral("fact");
    case MemoryType::Context:
        return QStringLiteral("context");
    }

    return QStringLiteral("fact");
}

MemoryType memoryTypeFromString(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("preference")) {
        return MemoryType::Preference;
    }
    if (normalized == QStringLiteral("context")) {
        return MemoryType::Context;
    }
    return MemoryType::Fact;
}

bool containsSecretMarker(const QString &value)
{
    const QString lowered = value.toLower();
    static const QStringList markers = {
        QStringLiteral("password"),
        QStringLiteral("api key"),
        QStringLiteral("apikey"),
        QStringLiteral("token"),
        QStringLiteral("secret"),
        QStringLiteral("private key"),
        QStringLiteral("ssh key"),
        QStringLiteral("bearer ")
    };

    for (const QString &marker : markers) {
        if (lowered.contains(marker)) {
            return true;
        }
    }

    return false;
}
}

MemoryManager::MemoryManager(const QString &storagePath)
    : m_storagePath(storagePath)
{
    loadFromDisk();
}

bool MemoryManager::write(const MemoryEntry &entry)
{
    QMutexLocker locker(&memoryMutex());
    ensureFresh();

    const MemoryEntry normalized = normalize(entry);
    if (shouldReject(normalized)) {
        return false;
    }

    for (MemoryEntry &existing : m_entries) {
        if ((!normalized.id.isEmpty() && existing.id == normalized.id)
            || (existing.type == normalized.type && existing.key == normalized.key && !normalized.key.isEmpty())) {
            existing = normalized;
            return saveToDisk();
        }
    }

    m_entries.push_back(normalized);
    return saveToDisk();
}

QList<MemoryEntry> MemoryManager::search(const QString &query, int maxCount) const
{
    QMutexLocker locker(&memoryMutex());
    ensureFresh();

    const QString lowered = query.trimmed().toLower();
    QList<MemoryEntry> matches;
    for (const MemoryEntry &entry : m_entries) {
        const QString haystack = QStringList{
            entry.kind,
            entry.key,
            entry.title,
            entry.value,
            entry.content,
            entry.tags.join(QLatin1Char(' '))
        }.join(QLatin1Char(' ')).toLower();

        if (lowered.isEmpty() || haystack.contains(lowered)) {
            matches.push_back(entry);
        }
    }

    std::sort(matches.begin(), matches.end(), [](const MemoryEntry &left, const MemoryEntry &right) {
        return left.createdAt > right.createdAt;
    });

    if (matches.size() > maxCount) {
        matches = matches.mid(0, maxCount);
    }

    return matches;
}

QList<MemoryEntry> MemoryManager::entries() const
{
    QMutexLocker locker(&memoryMutex());
    ensureFresh();
    return m_entries;
}

bool MemoryManager::remove(const QString &idOrKey)
{
    QMutexLocker locker(&memoryMutex());
    ensureFresh();

    const int previousSize = m_entries.size();
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [&](const MemoryEntry &entry) {
        return entry.id == idOrKey || entry.key == idOrKey || entry.title == idOrKey;
    }), m_entries.end());

    if (m_entries.size() == previousSize) {
        return false;
    }

    return saveToDisk();
}

QString MemoryManager::userName() const
{
    const QList<MemoryEntry> currentEntries = entries();
    for (const MemoryEntry &entry : currentEntries) {
        if (entry.type == MemoryType::Fact && entry.key == QStringLiteral("name")) {
            return entry.value;
        }
    }

    return {};
}

QString MemoryManager::storagePath() const
{
    return resolvedStoragePath();
}

QString MemoryManager::resolvedStoragePath() const
{
    return m_storagePath.trimmed().isEmpty() ? defaultStoragePath() : m_storagePath;
}

void MemoryManager::ensureFresh() const
{
    const QFileInfo info(resolvedStoragePath());
    const QDateTime currentModified = info.exists() ? info.lastModified().toUTC() : QDateTime{};
    if (currentModified.isValid() && currentModified != m_lastFileModified) {
        loadFromDisk();
    }
}

void MemoryManager::loadFromDisk() const
{
    QFile file(resolvedStoragePath());
    m_entries.clear();
    m_lastFileModified = {};

    if (!file.exists()) {
        return;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isArray()) {
        return;
    }

    const QJsonArray entries = document.array();
    for (const QJsonValue &value : entries) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object = value.toObject();
        MemoryEntry entry;
        entry.id = object.value(QStringLiteral("id")).toString();
        entry.kind = object.value(QStringLiteral("kind")).toString(object.value(QStringLiteral("type")).toString());
        entry.title = object.value(QStringLiteral("title")).toString(object.value(QStringLiteral("key")).toString());
        entry.content = object.value(QStringLiteral("content")).toString(object.value(QStringLiteral("value")).toString());
        entry.key = object.value(QStringLiteral("key")).toString(entry.title);
        entry.value = object.value(QStringLiteral("value")).toString(entry.content);
        entry.type = memoryTypeFromString(object.value(QStringLiteral("memoryType")).toString(entry.kind));
        entry.confidence = static_cast<float>(object.value(QStringLiteral("confidence")).toDouble(0.9));
        entry.secret = object.value(QStringLiteral("secret")).toBool(false);
        entry.source = object.value(QStringLiteral("source")).toString(QStringLiteral("memory"));
        entry.updatedAt = object.value(QStringLiteral("updatedAt")).toString();
        entry.createdAt = QDateTime::fromString(object.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);

        const QJsonArray tags = object.value(QStringLiteral("tags")).toArray();
        for (const QJsonValue &tag : tags) {
            if (tag.isString()) {
                entry.tags.push_back(tag.toString());
            }
        }

        m_entries.push_back(normalize(entry));
    }

    m_lastFileModified = QFileInfo(file).lastModified().toUTC();
}

bool MemoryManager::saveToDisk() const
{
    QJsonArray array;
    for (const MemoryEntry &entry : m_entries) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), entry.id);
        object.insert(QStringLiteral("memoryType"), memoryTypeToKind(entry.type));
        object.insert(QStringLiteral("kind"), entry.kind);
        object.insert(QStringLiteral("type"), entry.kind);
        object.insert(QStringLiteral("key"), entry.key);
        object.insert(QStringLiteral("title"), entry.title);
        object.insert(QStringLiteral("value"), entry.value);
        object.insert(QStringLiteral("content"), entry.content);
        object.insert(QStringLiteral("createdAt"), entry.createdAt.toUTC().toString(Qt::ISODate));
        object.insert(QStringLiteral("updatedAt"), entry.updatedAt);
        object.insert(QStringLiteral("confidence"), entry.confidence);
        object.insert(QStringLiteral("secret"), entry.secret);
        object.insert(QStringLiteral("source"), entry.source);

        QJsonArray tags;
        for (const QString &tag : entry.tags) {
            tags.push_back(tag);
        }
        object.insert(QStringLiteral("tags"), tags);
        array.push_back(object);
    }

    QFileInfo info(resolvedStoragePath());
    QDir().mkpath(info.absolutePath());

    QFile file(resolvedStoragePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    file.close();
    m_lastFileModified = QFileInfo(file).lastModified().toUTC();
    return true;
}

MemoryEntry MemoryManager::normalize(const MemoryEntry &entry) const
{
    MemoryEntry normalized = entry;

    if (normalized.kind.trimmed().isEmpty()) {
        normalized.kind = memoryTypeToKind(normalized.type);
    } else {
        normalized.type = memoryTypeFromString(normalized.kind);
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

    normalized.kind = normalized.kind.trimmed();
    normalized.key = normalized.key.trimmed();
    normalized.title = normalized.title.trimmed();
    normalized.value = normalized.value.trimmed();
    normalized.content = normalized.content.trimmed();

    if (!normalized.createdAt.isValid()) {
        normalized.createdAt = QDateTime::currentDateTimeUtc();
    }
    if (normalized.updatedAt.trimmed().isEmpty()) {
        normalized.updatedAt = normalized.createdAt.toUTC().toString(Qt::ISODate);
    }
    if (normalized.id.trimmed().isEmpty()) {
        normalized.id = slugify(normalized.kind + QStringLiteral("-") + normalized.key);
    }
    if (normalized.source.trimmed().isEmpty()) {
        normalized.source = QStringLiteral("memory");
    }

    return normalized;
}

bool MemoryManager::shouldReject(const MemoryEntry &entry) const
{
    if (entry.secret) {
        return true;
    }

    if (entry.key.isEmpty() || entry.value.isEmpty()) {
        return true;
    }

    if (containsSecretMarker(entry.key) || containsSecretMarker(entry.value)) {
        return true;
    }

    const int lineCount = entry.value.isEmpty()
        ? 0
        : entry.value.split(QLatin1Char('\n'), Qt::KeepEmptyParts).size();

    if (entry.value.size() > 2000 || lineCount > 20) {
        return true;
    }

    return false;
}
