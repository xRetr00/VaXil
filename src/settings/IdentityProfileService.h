#pragma once

#include <QObject>
#include <QString>

#include "core/AssistantTypes.h"

class IdentityProfileService : public QObject
{
    Q_OBJECT

public:
    explicit IdentityProfileService(QObject *parent = nullptr);

    bool initialize();

    AssistantIdentity identity() const;
    UserProfile userProfile() const;

    bool setUserName(const QString &userName);
    bool setPreference(const QString &key, const QString &value);

private:
    QString configRoot() const;
    QString identityPath() const;
    QString userProfilePath() const;
    bool ensureDefaults();
    bool loadIdentity();
    bool loadUserProfile();
    bool saveUserProfile() const;

    AssistantIdentity m_identity;
    UserProfile m_userProfile;
};
