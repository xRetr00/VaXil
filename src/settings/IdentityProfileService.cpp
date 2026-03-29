#include "settings/IdentityProfileService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <nlohmann/json.hpp>

namespace {
QString normalizeConfigRoot(const QString &candidate)
{
    QDir dir(candidate);
    if (dir.exists(QStringLiteral("config"))) {
        return dir.absoluteFilePath(QStringLiteral("config"));
    }
    return {};
}

QString writableFallbackConfigRoot()
{
    QDir sourceRoot(QStringLiteral(JARVIS_SOURCE_DIR));
    sourceRoot.mkpath(QStringLiteral("config"));
    return sourceRoot.absoluteFilePath(QStringLiteral("config"));
}
}

IdentityProfileService::IdentityProfileService(QObject *parent)
    : QObject(parent)
    , m_identity({
          .assistantName = QStringLiteral("Vaxil"),
          .personality = QStringLiteral("precise, calm, and highly capable"),
          .tone = QStringLiteral("concise, confident, non-robotic"),
          .addressingStyle = QStringLiteral("direct")
      })
{
}

bool IdentityProfileService::initialize()
{
    if (!ensureDefaults()) {
        return false;
    }

    return loadIdentity() && loadUserProfile();
}

AssistantIdentity IdentityProfileService::identity() const
{
    return m_identity;
}

UserProfile IdentityProfileService::userProfile() const
{
    return m_userProfile;
}

bool IdentityProfileService::setUserName(const QString &userName)
{
    const QString normalized = userName.trimmed();
    m_userProfile.userName = normalized;
    return saveUserProfile();
}

bool IdentityProfileService::setPreference(const QString &key, const QString &value)
{
    if (key.trimmed().isEmpty()) {
        return false;
    }

    m_userProfile.preferences[key.toStdString()] = value.toStdString();
    return saveUserProfile();
}

QString IdentityProfileService::configRoot() const
{
    const QString currentRoot = normalizeConfigRoot(QDir::currentPath());
    if (!currentRoot.isEmpty()) {
        return currentRoot;
    }

    const QString appRoot = normalizeConfigRoot(QCoreApplication::applicationDirPath());
    if (!appRoot.isEmpty()) {
        return appRoot;
    }

    return writableFallbackConfigRoot();
}

QString IdentityProfileService::identityPath() const
{
    return QDir(configRoot()).absoluteFilePath(QStringLiteral("identity.json"));
}

QString IdentityProfileService::userProfilePath() const
{
    return QDir(configRoot()).absoluteFilePath(QStringLiteral("user_profile.json"));
}

bool IdentityProfileService::ensureDefaults()
{
    QDir().mkpath(configRoot());

    QFile identityFile(identityPath());
    if (!identityFile.exists()) {
        const nlohmann::json identity = {
            {"assistant_name", m_identity.assistantName.toStdString()},
            {"personality", m_identity.personality.toStdString()},
            {"tone", m_identity.tone.toStdString()},
            {"addressing_style", m_identity.addressingStyle.toStdString()}
        };
        if (!identityFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            return false;
        }
        identityFile.write(QByteArray::fromStdString(identity.dump(2)));
    }

    QFile profileFile(userProfilePath());
    if (!profileFile.exists()) {
        const nlohmann::json profile = {
            {"user_name", ""},
            {"preferences", nlohmann::json::object()}
        };
        if (!profileFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            return false;
        }
        profileFile.write(QByteArray::fromStdString(profile.dump(2)));
    }

    return true;
}

bool IdentityProfileService::loadIdentity()
{
    QFile file(identityPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const auto json = nlohmann::json::parse(file.readAll().constData(), nullptr, false);
    if (json.is_discarded()) {
        return false;
    }

    m_identity.assistantName = QString::fromStdString(json.value("assistant_name", m_identity.assistantName.toStdString()));
    m_identity.personality = QString::fromStdString(json.value("personality", m_identity.personality.toStdString()));
    m_identity.tone = QString::fromStdString(json.value("tone", m_identity.tone.toStdString()));
    m_identity.addressingStyle = QString::fromStdString(json.value("addressing_style", m_identity.addressingStyle.toStdString()));
    return true;
}

bool IdentityProfileService::loadUserProfile()
{
    QFile file(userProfilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const auto json = nlohmann::json::parse(file.readAll().constData(), nullptr, false);
    if (json.is_discarded()) {
        return false;
    }

    m_userProfile.userName = QString::fromStdString(json.value("user_name", std::string{}));
    m_userProfile.preferences = json.contains("preferences") ? json.at("preferences") : nlohmann::json::object();
    return true;
}

bool IdentityProfileService::saveUserProfile() const
{
    const nlohmann::json profile = {
        {"user_name", m_userProfile.userName.toStdString()},
        {"preferences", m_userProfile.preferences}
    };

    QFile file(userProfilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    file.write(QByteArray::fromStdString(profile.dump(2)));
    return true;
}
