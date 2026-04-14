#include "cognition/ConnectorResultSignal.h"

#include <QJsonArray>

namespace {
QString normalizedType(const QString &taskType)
{
    return taskType.trimmed().toLower();
}

bool hasAnyKey(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        if (object.contains(QString::fromUtf8(key))) {
            return true;
        }
    }
    return false;
}

int arrayCount(const QJsonObject &object, const QString &key)
{
    return object.value(key).toArray().size();
}

QString firstString(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const QString value = object.value(QString::fromUtf8(key)).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}
}

ConnectorResultSignal ConnectorResultSignalBuilder::fromBackgroundTaskResult(const BackgroundTaskResult &result)
{
    const QJsonObject payload = result.payload;
    const QString taskType = normalizedType(result.type);

    if (payload.value(QStringLiteral("connectorKind")).toString().trimmed() == QStringLiteral("schedule")
        || taskType.contains(QStringLiteral("calendar"))
        || taskType.contains(QStringLiteral("schedule"))
        || payload.contains(QStringLiteral("events"))
        || payload.contains(QStringLiteral("meetings"))) {
        return buildScheduleSignal(result, payload);
    }

    if (payload.value(QStringLiteral("connectorKind")).toString().trimmed() == QStringLiteral("inbox")
        || taskType.contains(QStringLiteral("email"))
        || taskType.contains(QStringLiteral("mail"))
        || taskType.contains(QStringLiteral("message"))
        || payload.contains(QStringLiteral("messages"))
        || payload.contains(QStringLiteral("unreadCount"))) {
        return buildInboxSignal(result, payload);
    }

    if (payload.value(QStringLiteral("connectorKind")).toString().trimmed() == QStringLiteral("notes")
        || taskType.contains(QStringLiteral("note"))
        || taskType.contains(QStringLiteral("memo"))
        || payload.contains(QStringLiteral("noteId"))
        || payload.contains(QStringLiteral("documentId"))) {
        return buildNoteSignal(result, payload);
    }

    if (payload.value(QStringLiteral("connectorKind")).toString().trimmed() == QStringLiteral("research")
        || hasAnyKey(payload, {"provider", "query", "sources"})) {
        return buildResearchSignal(result, payload);
    }

    return {};
}

ConnectorResultSignal ConnectorResultSignalBuilder::buildScheduleSignal(const BackgroundTaskResult &result,
                                                                        const QJsonObject &payload)
{
    ConnectorResultSignal signal;
    signal.sourceKind = QStringLiteral("connector_result");
    signal.taskType = QStringLiteral("calendar_review");
    signal.connectorKind = QStringLiteral("schedule");
    signal.itemCount = std::max(arrayCount(payload, QStringLiteral("events")),
                                arrayCount(payload, QStringLiteral("meetings")));

    const QString title = firstString(payload, {"title", "eventTitle", "nextTitle", "summary"});
    signal.summary = title.isEmpty()
        ? (result.summary.trimmed().isEmpty() ? result.title.trimmed() : result.summary.trimmed())
        : QStringLiteral("Schedule updated: %1").arg(title);
    return signal;
}

ConnectorResultSignal ConnectorResultSignalBuilder::buildInboxSignal(const BackgroundTaskResult &result,
                                                                     const QJsonObject &payload)
{
    ConnectorResultSignal signal;
    signal.sourceKind = QStringLiteral("connector_result");
    signal.taskType = QStringLiteral("email_fetch");
    signal.connectorKind = QStringLiteral("inbox");
    signal.itemCount = payload.value(QStringLiteral("unreadCount")).toInt(arrayCount(payload, QStringLiteral("messages")));

    const QString sender = firstString(payload, {"sender", "from", "primarySender"});
    signal.summary = sender.isEmpty()
        ? (result.summary.trimmed().isEmpty() ? result.title.trimmed() : result.summary.trimmed())
        : QStringLiteral("Inbox updated: %1 needs attention.").arg(sender);
    return signal;
}

ConnectorResultSignal ConnectorResultSignalBuilder::buildNoteSignal(const BackgroundTaskResult &result,
                                                                    const QJsonObject &payload)
{
    ConnectorResultSignal signal;
    signal.sourceKind = QStringLiteral("connector_result");
    signal.taskType = QStringLiteral("note_capture");
    signal.connectorKind = QStringLiteral("notes");
    signal.itemCount = 1;

    const QString title = firstString(payload, {"title", "noteTitle", "documentTitle"});
    signal.summary = title.isEmpty()
        ? (result.summary.trimmed().isEmpty() ? result.title.trimmed() : result.summary.trimmed())
        : QStringLiteral("Notes updated: %1").arg(title);
    return signal;
}

ConnectorResultSignal ConnectorResultSignalBuilder::buildResearchSignal(const BackgroundTaskResult &result,
                                                                        const QJsonObject &payload)
{
    ConnectorResultSignal signal;
    signal.sourceKind = QStringLiteral("connector_result");
    signal.taskType = QStringLiteral("web_search");
    signal.connectorKind = QStringLiteral("research");
    signal.itemCount = arrayCount(payload, QStringLiteral("sources"));

    const QString query = firstString(payload, {"query", "title", "summary"});
    signal.summary = query.isEmpty()
        ? (result.summary.trimmed().isEmpty() ? result.title.trimmed() : result.summary.trimmed())
        : QStringLiteral("Research updated: %1").arg(query);
    return signal;
}
