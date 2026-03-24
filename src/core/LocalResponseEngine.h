#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

#include "core/AssistantTypes.h"

class LocalResponseEngine : public QObject
{
    Q_OBJECT

public:
    explicit LocalResponseEngine(QObject *parent = nullptr);

    bool initialize();
    QString respondToIntent(LocalIntent intent, const LocalResponseContext &context);
    QString respondToError(const QString &errorKey, const LocalResponseContext &context);
    QString acknowledgement(const QString &target, const LocalResponseContext &context);

private:
    QString resolveGroup(LocalIntent intent, const LocalResponseContext &context) const;
    QString renderTemplate(const QString &variant, const LocalResponseContext &context, const QString &target = QString()) const;
    QString chooseVariant(const QString &group);

    QHash<QString, QStringList> m_responses;
    QHash<QString, int> m_lastIndexByGroup;
};
