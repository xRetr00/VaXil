#include "memory/MemoryStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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

QString normalizedQuery(QString value)
{
    value = value.trimmed().toLower();
    value.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    return value.simplified();
}

QString connectorSummary(const QString &connectorKind, const QVariantMap &state)
{
    const int seenCount = state.value(QStringLiteral("seenCount")).toInt();
    const int presentedCount = state.value(QStringLiteral("presentedCount")).toInt();
    const QString sourceKind = state.value(QStringLiteral("sourceKind")).toString().trimmed();

    if (connectorKind == QStringLiteral("schedule")) {
        return QStringLiteral("Schedule signals seen %1 times and presented %2 times recently.").arg(seenCount).arg(presentedCount);
    }
    if (connectorKind == QStringLiteral("inbox")) {
        return QStringLiteral("Inbox signals seen %1 times and presented %2 times recently.").arg(seenCount).arg(presentedCount);
    }
    if (connectorKind == QStringLiteral("notes")) {
        return QStringLiteral("Notes activity seen %1 times and presented %2 times recently.").arg(seenCount).arg(presentedCount);
    }
    if (connectorKind == QStringLiteral("research")) {
        return QStringLiteral("Research signals seen %1 times and presented %2 times recently.").arg(seenCount).arg(presentedCount);
    }
    return QStringLiteral("%1 signals seen %2 times and presented %3 times recently.")
        .arg(sourceKind.isEmpty() ? QStringLiteral("Connector") : sourceKind, QString::number(seenCount), QString::number(presentedCount));
}

int connectorMemoryScore(const QString &query, const QString &connectorKind, const QVariantMap &state)
{
    int score = 0;
    const QString haystack = QStringList{
        connectorKind,
        state.value(QStringLiteral("sourceKind")).toString(),
        state.value(QStringLiteral("historyKey")).toString()
    }.join(QLatin1Char(' ')).toLower();

    if (query.isEmpty()) {
        score += 10;
    } else if (haystack.contains(query)) {
        score += 80;
    }

    score += std::min(state.value(QStringLiteral("seenCount")).toInt(), 20);
    score += std::min(state.value(QStringLiteral("presentedCount")).toInt() * 2, 20);
    if (state.value(QStringLiteral("historyRecentlySeen")).toBool()) {
        score += 24;
    }
    if (state.value(QStringLiteral("historyRecentlyPresented")).toBool()) {
        score += 18;
    }
    return score;
}
}

MemoryStore::MemoryStore(const QString &storagePath, QObject *parent)
    : QObject(parent)
    , m_memoryManager(std::make_unique<MemoryManager>(storagePath))
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

QList<MemoryRecord> MemoryStore::connectorMemory(const QString &query, int maxCount) const
{
    struct RankedConnectorMemory {
        MemoryRecord record;
        int score = 0;
    };

    const QString normalized = normalizedQuery(query);
    QList<RankedConnectorMemory> ranked;
    const QHash<QString, QVariantMap> stateByKey = connectorStateMap();
    for (auto it = stateByKey.cbegin(); it != stateByKey.cend(); ++it) {
        const QVariantMap &state = it.value();
        const QString connectorKind = state.value(QStringLiteral("connectorKind")).toString().trimmed().toLower();
        if (connectorKind.isEmpty()) {
            continue;
        }

        RankedConnectorMemory entry;
        entry.record.type = QStringLiteral("context");
        entry.record.key = QStringLiteral("connector_history_%1").arg(connectorKind);
        entry.record.value = connectorSummary(connectorKind, state);
        entry.record.confidence = 0.86f;
        entry.record.source = QStringLiteral("connector_memory");
        entry.record.updatedAt = QString::number(state.value(QStringLiteral("lastSeenAtMs")).toLongLong());
        entry.score = connectorMemoryScore(normalized, connectorKind, state);
        if (entry.score <= 0) {
            continue;
        }
        ranked.push_back(entry);
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedConnectorMemory &left, const RankedConnectorMemory &right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.record.updatedAt > right.record.updatedAt;
    });

    QList<MemoryRecord> records;
    for (const RankedConnectorMemory &entry : ranked) {
        records.push_back(entry.record);
        if (records.size() >= maxCount) {
            break;
        }
    }
    return records;
}

bool MemoryStore::upsertConnectorState(const QString &historyKey, const QVariantMap &state)
{
    const QString trimmedKey = historyKey.trimmed();
    if (trimmedKey.isEmpty()) {
        return false;
    }

    const QJsonObject object = QJsonObject::fromVariantMap(state);
    const QJsonDocument document(object);

    MemoryEntry entry;
    entry.type = MemoryType::Context;
    entry.kind = QStringLiteral("context");
    entry.key = connectorStateStorageKey(trimmedKey);
    entry.title = trimmedKey;
    entry.value = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    entry.content = entry.value;
    entry.id = slugify(QStringLiteral("connector-state-") + trimmedKey);
    entry.confidence = 0.95f;
    entry.source = QStringLiteral("connector_memory");
    entry.tags = {
        QStringLiteral("connector_state"),
        state.value(QStringLiteral("connectorKind")).toString().trimmed().toLower(),
        state.value(QStringLiteral("sourceKind")).toString().trimmed()
    };
    entry.createdAt = QDateTime::currentDateTimeUtc();
    entry.updatedAt = entry.createdAt.toUTC().toString(Qt::ISODate);
    return upsertEntry(entry);
}

bool MemoryStore::deleteConnectorState(const QString &historyKey)
{
    const QString trimmedKey = historyKey.trimmed();
    if (trimmedKey.isEmpty()) {
        return false;
    }
    return deleteEntry(connectorStateStorageKey(trimmedKey));
}

QHash<QString, QVariantMap> MemoryStore::connectorStateMap() const
{
    QHash<QString, QVariantMap> stateByKey;
    for (const MemoryEntry &entry : allEntries()) {
        if (entry.source != QStringLiteral("connector_memory")) {
            continue;
        }
        if (!entry.key.startsWith(QStringLiteral("connector_state:"))) {
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(entry.value.toUtf8());
        if (!document.isObject()) {
            continue;
        }

        const QString historyKey = entry.key.mid(QStringLiteral("connector_state:").size()).trimmed();
        if (historyKey.isEmpty()) {
            continue;
        }

        stateByKey.insert(historyKey, document.object().toVariantMap());
    }
    return stateByKey;
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

QString MemoryStore::connectorStateStorageKey(const QString &historyKey) const
{
    return QStringLiteral("connector_state:%1").arg(historyKey.trimmed());
}
