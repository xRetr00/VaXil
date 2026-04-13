#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

struct ContextThreadId
{
    QString value;

    [[nodiscard]] bool isEmpty() const { return value.trimmed().isEmpty(); }

    [[nodiscard]] QVariantMap toVariantMap() const
    {
        return { { QStringLiteral("value"), value } };
    }

    [[nodiscard]] static ContextThreadId fromParts(const QStringList &parts)
    {
        QStringList normalized;
        normalized.reserve(parts.size());
        for (QString part : parts) {
            part = part.trimmed().toLower();
            if (!part.isEmpty()) {
                normalized.push_back(part);
            }
        }

        ContextThreadId id;
        id.value = normalized.join(QStringLiteral("::"));
        return id;
    }
};
